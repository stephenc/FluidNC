// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/StatusPresenter.h"

#include "gtest/gtest.h"

namespace {
    using namespace TS35;

    TEST(TS35StatusPresenter, DisconnectedNeverDisplaysCachedPosition) {
        MachineSnapshot machine;
        machine.work_position_valid = true;
        machine.axis_count          = 3;
        machine.work_position       = { 1, 2, 3 };

        StatusView view = composeStatusView(machine);

        EXPECT_EQ(view.state, "Disconnected");
        EXPECT_EQ(view.tone, StatusTone::Disconnected);
        EXPECT_EQ(view.position_caption, "POSITION UNAVAILABLE");
        EXPECT_EQ(view.axis_values[0], "--");
    }

    TEST(TS35StatusPresenter, ShowsWorkPositionWithUnits) {
        MachineSnapshot machine;
        machine.connected           = true;
        machine.state               = MachineState::Idle;
        machine.state_text          = "Idle";
        machine.work_position_valid = true;
        machine.axis_count          = 3;
        machine.work_position       = { 1.25F, -2.5F, 3.0F };

        StatusView view = composeStatusView(machine);

        EXPECT_EQ(view.state, "Idle");
        EXPECT_EQ(view.tone, StatusTone::Ready);
        EXPECT_EQ(view.position_caption, "WORK POSITION (MM)");
        EXPECT_EQ(view.axis_values[0], "1.250");
        EXPECT_EQ(view.axis_values[1], "-2.500");
        EXPECT_EQ(view.axis_values[2], "3.000");
    }

    TEST(TS35StatusPresenter, FallsBackToClearlyLabelledMachinePosition) {
        MachineSnapshot machine;
        machine.connected              = true;
        machine.state                  = MachineState::Homing;
        machine.state_text             = "Home";
        machine.machine_position_valid = true;
        machine.axis_count             = 2;
        machine.machine_position       = { 10.0F, 20.0F };

        StatusView view = composeStatusView(machine);

        EXPECT_EQ(view.tone, StatusTone::Active);
        EXPECT_EQ(view.position_caption, "MACHINE POSITION (MM)");
        EXPECT_EQ(view.axis_values[0], "10.000");
        EXPECT_EQ(view.axis_values[1], "20.000");
        EXPECT_EQ(view.axis_values[2], "--");
    }

    TEST(TS35StatusPresenter, FormatsInchesWithFourDecimalPlaces) {
        MachineSnapshot machine;
        machine.connected           = true;
        machine.state               = MachineState::Jog;
        machine.state_text          = "Jog";
        machine.inch_mode           = true;
        machine.work_position_valid = true;
        machine.axis_count          = 1;
        machine.work_position[0]    = 1.23456F;

        StatusView view = composeStatusView(machine);

        EXPECT_EQ(view.position_caption, "WORK POSITION (IN)");
        EXPECT_EQ(view.axis_values[0], "1.2346");
    }

    TEST(TS35StatusPresenter, MakesLimitProbeToolAndJobStateExplicit) {
        MachineSnapshot machine;
        machine.connected     = true;
        machine.state         = MachineState::Run;
        machine.state_text    = "Run";
        machine.limits[0]     = true;
        machine.limits[2]     = true;
        machine.probe         = true;
        machine.feed_rate     = 500;
        machine.spindle_speed = 1000;
        machine.selected_tool = 2;
        machine.accessories   = "SF";
        machine.sd_job_active = true;
        machine.sd_percent    = 25.25F;
        machine.sd_filename   = "plot.nc";

        StatusView view = composeStatusView(machine);

        EXPECT_EQ(view.tone, StatusTone::Active);
        EXPECT_EQ(view.limit_text, "LIMITS X,Z,PROBE");
        EXPECT_EQ(view.feed_text, "FEED 500  SPINDLE 1000");
        EXPECT_EQ(view.tool_text, "TOOL 2  ACTIVE SF");
        EXPECT_EQ(view.job_text, "25.2%  plot.nc");
    }

    TEST(TS35StatusPresenter, ShowsClearLimitsAndNoJobWithoutAmbiguity) {
        MachineSnapshot machine;
        machine.connected  = true;
        machine.state      = MachineState::Hold;
        machine.state_text = "Hold:0";

        StatusView view = composeStatusView(machine);

        EXPECT_EQ(view.tone, StatusTone::Paused);
        EXPECT_EQ(view.limit_text, "LIMITS CLEAR");
        EXPECT_EQ(view.job_text, "NO ACTIVE SD JOB");
    }

    TEST(TS35StatusPresenter, EqualityTracksEveryRenderedField) {
        MachineModel model;
        ASSERT_TRUE(model.ingestLine("<Idle|WPos:1,2,3|FS:0,0>"));
        StatusView first = composeStatusView(model.snapshot());
        ASSERT_TRUE(model.ingestLine("<Idle|WPos:1,2,3|FS:0,0>"));
        StatusView identical = composeStatusView(model.snapshot());
        EXPECT_EQ(first, identical);

        ASSERT_TRUE(model.ingestLine("<Idle|WPos:1,2,4|FS:0,0>"));
        StatusView changed = composeStatusView(model.snapshot());
        EXPECT_NE(first, changed);

        changed.alarm_text = "LAST ERROR 9";
        EXPECT_NE(identical, changed);
    }

    TEST(TS35StatusPresenter, AlarmCodeTakesPriorityOverLastError) {
        MachineSnapshot machine;
        machine.connected  = true;
        machine.state      = MachineState::Alarm;
        machine.state_text = "Alarm";
        machine.last_alarm = 14;
        machine.last_error = 20;

        StatusView view = composeStatusView(machine);

        EXPECT_EQ(view.tone, StatusTone::Warning);
        EXPECT_EQ(view.alarm_text, "ALARM 14");
    }

}  // namespace
