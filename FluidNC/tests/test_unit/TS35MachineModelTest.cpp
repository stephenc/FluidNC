// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/MachineModel.h"

#include "gtest/gtest.h"

namespace {
    using namespace TS35;

    TEST(TS35MachineModel, StartsDisconnected) {
        MachineModel model;

        EXPECT_FALSE(model.snapshot().connected);
        EXPECT_EQ(model.snapshot().state, MachineState::Disconnected);
        EXPECT_STREQ(stateName(model.snapshot().state), "Disconnected");
    }

    TEST(TS35MachineModel, DecodesRequiredStatesAndSubstates) {
        struct Case {
            const char*  report;
            MachineState state;
        } cases[] = {
            { "<Idle|WPos:0,0,0>", MachineState::Idle },   { "<Jog|WPos:0,0,0>", MachineState::Jog },
            { "<Run|WPos:0,0,0>", MachineState::Run },     { "<Hold:0|WPos:0,0,0>", MachineState::Hold },
            { "<Home|MPos:0,0,0>", MachineState::Homing }, { "<Alarm|WPos:0,0,0>", MachineState::Alarm },
            { "<Door:1|WPos:0,0,0>", MachineState::Door }, { "<Check|WPos:0,0,0>", MachineState::Check },
        };

        MachineModel model;
        for (const auto& item : cases) {
            ASSERT_TRUE(model.ingestLine(item.report)) << item.report;
            EXPECT_EQ(model.snapshot().state, item.state) << item.report;
            EXPECT_TRUE(model.snapshot().connected);
        }
    }

    TEST(TS35MachineModel, ParsesCompleteWorkPositionStatus) {
        MachineModel model;

        ASSERT_TRUE(model.ingestLine("<Run|WPos:1.25,-2.5,3|FS:420,12000|Pn:PXZ|Ov:95,50,110|A:SFM|SD:12.5,drawings/test.nc>"));
        const auto& snapshot = model.snapshot();

        EXPECT_EQ(snapshot.axis_count, 3U);
        EXPECT_TRUE(snapshot.work_position_valid);
        EXPECT_FALSE(snapshot.machine_position_valid);
        EXPECT_FLOAT_EQ(snapshot.work_position[0], 1.25F);
        EXPECT_FLOAT_EQ(snapshot.work_position[1], -2.5F);
        EXPECT_FLOAT_EQ(snapshot.work_position[2], 3.0F);
        EXPECT_FLOAT_EQ(snapshot.feed_rate, 420.0F);
        EXPECT_FLOAT_EQ(snapshot.spindle_speed, 12000.0F);
        EXPECT_TRUE(snapshot.probe);
        EXPECT_TRUE(snapshot.limits[0]);
        EXPECT_FALSE(snapshot.limits[1]);
        EXPECT_TRUE(snapshot.limits[2]);
        EXPECT_EQ(snapshot.feed_override, 95);
        EXPECT_EQ(snapshot.rapid_override, 50);
        EXPECT_EQ(snapshot.spindle_override, 110);
        EXPECT_EQ(snapshot.accessories, "SFM");
        EXPECT_TRUE(snapshot.sd_job_active);
        EXPECT_FLOAT_EQ(snapshot.sd_percent, 12.5F);
        EXPECT_EQ(snapshot.sd_filename, "drawings/test.nc");
    }

    TEST(TS35MachineModel, DerivesWorkPositionFromMachinePositionAndWco) {
        MachineModel model;

        ASSERT_TRUE(model.ingestLine("<Idle|MPos:11,22,33|WCO:1,2,3>"));
        const auto& snapshot = model.snapshot();

        ASSERT_TRUE(snapshot.machine_position_valid);
        ASSERT_TRUE(snapshot.work_position_valid);
        EXPECT_FLOAT_EQ(snapshot.work_position[0], 10.0F);
        EXPECT_FLOAT_EQ(snapshot.work_position[1], 20.0F);
        EXPECT_FLOAT_EQ(snapshot.work_position[2], 30.0F);
    }

    TEST(TS35MachineModel, ReusesLastWcoWithoutMakingPositionStale) {
        MachineModel model;
        ASSERT_TRUE(model.ingestLine("<Idle|MPos:11,22,33|WCO:1,2,3>"));
        ASSERT_TRUE(model.ingestLine("<Jog|MPos:12,24,36>"));

        const auto& snapshot = model.snapshot();
        ASSERT_TRUE(snapshot.work_position_valid);
        EXPECT_FLOAT_EQ(snapshot.work_position[0], 11.0F);
        EXPECT_FLOAT_EQ(snapshot.work_position[1], 22.0F);
        EXPECT_FLOAT_EQ(snapshot.work_position[2], 33.0F);

        ASSERT_TRUE(model.ingestLine("<Hold:1|FS:0,0>"));
        EXPECT_FALSE(model.snapshot().machine_position_valid);
        EXPECT_FALSE(model.snapshot().work_position_valid);
    }

    TEST(TS35MachineModel, ClearsTransientPinsAndJobOnNextStatus) {
        MachineModel model;
        ASSERT_TRUE(model.ingestLine("<Run|WPos:1,2,3|Pn:XY|SD:50,file.nc>"));
        ASSERT_TRUE(model.ingestLine("<Idle|WPos:1,2,3>"));

        EXPECT_FALSE(model.snapshot().limits[0]);
        EXPECT_FALSE(model.snapshot().limits[1]);
        EXPECT_FALSE(model.snapshot().sd_job_active);
        EXPECT_TRUE(model.snapshot().sd_filename.empty());
    }

    TEST(TS35MachineModel, MalformedRecognizedReportIsAtomic) {
        MachineModel model;
        ASSERT_TRUE(model.ingestLine("<Idle|WPos:1,2,3|FS:0,0>"));
        auto before = model.snapshot();

        EXPECT_FALSE(model.ingestLine("<Run|WPos:1,not-a-number,3>"));
        EXPECT_EQ(model.snapshot().generation, before.generation);
        EXPECT_EQ(model.snapshot().state, before.state);
        EXPECT_EQ(model.snapshot().work_position, before.work_position);

        EXPECT_FALSE(model.ingestLine("<Run|WPos:1,2,3"));
        EXPECT_EQ(model.snapshot().generation, before.generation);
    }

    TEST(TS35MachineModel, RejectsOversizeAndUnknownLines) {
        MachineModel model;
        std::string  oversized(MaxReportSize + 1, 'x');

        EXPECT_FALSE(model.ingestLine(oversized));
        EXPECT_FALSE(model.ingestLine("ok"));
        EXPECT_FALSE(model.snapshot().connected);
    }

    TEST(TS35MachineModel, TracksAlarmErrorAndGcodeModes) {
        MachineModel model;

        ASSERT_TRUE(model.ingestLine("ALARM:14 (Homing required)"));
        EXPECT_EQ(model.snapshot().state, MachineState::Alarm);
        EXPECT_EQ(model.snapshot().last_alarm, 14);

        ASSERT_TRUE(model.ingestLine("error:20 (Unsupported command)"));
        EXPECT_EQ(model.snapshot().last_error, 20);

        ASSERT_TRUE(model.ingestLine("[GC:G0 G54 G20 G90 M5 T7 F0 S0]"));
        EXPECT_TRUE(model.snapshot().inch_mode);
        EXPECT_EQ(model.snapshot().selected_tool, 7U);

        ASSERT_TRUE(model.ingestLine("[GC:G0 G54 G21 G90 M5 T0 F0 S0]"));
        EXPECT_FALSE(model.snapshot().inch_mode);
        EXPECT_EQ(model.snapshot().selected_tool, 0U);
    }

    TEST(TS35MachineModel, DisconnectInvalidatesVolatileDisplayData) {
        MachineModel model;
        ASSERT_TRUE(model.ingestLine("<Run|WPos:1,2,3|FS:500,12000|Ov:95,50,110|A:SFM|Pn:PX|SD:50,file.nc>"));
        ASSERT_TRUE(model.ingestLine("ALARM:14"));
        ASSERT_TRUE(model.ingestLine("error:20"));
        ASSERT_TRUE(model.ingestLine("[GC:G0 G54 G20 G90 M5 T7 F0 S0]"));

        model.markDisconnected();

        const auto& snapshot = model.snapshot();
        EXPECT_FALSE(snapshot.connected);
        EXPECT_EQ(snapshot.state, MachineState::Disconnected);
        EXPECT_FALSE(snapshot.machine_position_valid);
        EXPECT_FALSE(snapshot.work_position_valid);
        EXPECT_FALSE(snapshot.offset_valid);
        EXPECT_EQ(snapshot.axis_count, 0U);
        EXPECT_FALSE(snapshot.sd_job_active);
        EXPECT_TRUE(snapshot.sd_filename.empty());
        EXPECT_FALSE(snapshot.limits[0]);
        EXPECT_FALSE(snapshot.probe);
        EXPECT_FLOAT_EQ(snapshot.feed_rate, 0.0f);
        EXPECT_FLOAT_EQ(snapshot.spindle_speed, 0.0f);
        EXPECT_EQ(snapshot.feed_override, 100);
        EXPECT_EQ(snapshot.rapid_override, 100);
        EXPECT_EQ(snapshot.spindle_override, 100);
        EXPECT_TRUE(snapshot.accessories.empty());
        EXPECT_EQ(snapshot.selected_tool, 0U);
        EXPECT_EQ(snapshot.last_alarm, 0);
        EXPECT_EQ(snapshot.last_error, 0);
        EXPECT_FALSE(snapshot.inch_mode);
    }

}  // namespace
