// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/ProtocolStream.h"

#include "gtest/gtest.h"

#include <string_view>

namespace {
    using namespace TS35;

    size_t ingest(std::string_view bytes, ProtocolStream& stream, MachineModel& model) {
        size_t accepted = 0;
        for (uint8_t byte : bytes) {
            accepted += stream.ingest(byte, model) ? 1U : 0U;
        }
        return accepted;
    }

    TEST(TS35ProtocolStream, BuffersPartialLineUntilTerminator) {
        ProtocolStream stream;
        MachineModel   model;

        EXPECT_EQ(ingest("<Idle|WPos:1,2", stream, model), 0U);
    EXPECT_EQ(stream.bufferedBytes(), 14U);
        EXPECT_FALSE(model.snapshot().connected);

        EXPECT_EQ(ingest(",3>\n", stream, model), 1U);
        EXPECT_TRUE(model.snapshot().connected);
        EXPECT_EQ(model.snapshot().state, MachineState::Idle);
        EXPECT_EQ(stream.bufferedBytes(), 0U);
    }

    TEST(TS35ProtocolStream, TreatsCrLfAsOneTerminator) {
        ProtocolStream stream;
        MachineModel   model;

        EXPECT_EQ(ingest("<Idle|WPos:1,2,3>\r\n", stream, model), 1U);
        EXPECT_EQ(stream.malformedLines(), 0U);
    }

    TEST(TS35ProtocolStream, ProcessesSeveralReportsInOneChunk) {
        ProtocolStream stream;
        MachineModel   model;

        EXPECT_EQ(ingest("<Idle|WPos:0,0,0>\n<Jog|WPos:1,0,0>\n<Hold:0|WPos:2,0,0>\n", stream, model), 3U);
        EXPECT_EQ(model.snapshot().state, MachineState::Hold);
        EXPECT_EQ(model.snapshot().generation, 3U);
    }

    TEST(TS35ProtocolStream, DropsOversizeLineAndRecoversAtNextBoundary) {
        ProtocolStream stream;
        MachineModel   model;
        std::string    oversized(MaxReportSize + 20, 'x');
        oversized += "\n<Idle|WPos:1,2,3>\n";

        EXPECT_EQ(ingest(oversized, stream, model), 1U);
        EXPECT_EQ(stream.droppedLines(), 1U);
        EXPECT_EQ(stream.malformedLines(), 0U);
        EXPECT_EQ(model.snapshot().state, MachineState::Idle);
    }

    TEST(TS35ProtocolStream, CountsMalformedLineWithoutLosingNextReport) {
        ProtocolStream stream;
        MachineModel   model;

        EXPECT_EQ(ingest("<Run|WPos:oops,2,3>\n<Idle|WPos:1,2,3>\n", stream, model), 1U);
        EXPECT_EQ(stream.malformedLines(), 1U);
        EXPECT_EQ(model.snapshot().state, MachineState::Idle);
    }

    TEST(TS35ProtocolStream, ResetDiscardsPartialAndOverflowState) {
        ProtocolStream stream;
        MachineModel   model;
        std::string    oversized(MaxReportSize + 1, 'x');
        EXPECT_EQ(ingest(oversized, stream, model), 0U);
        ASSERT_TRUE(stream.discarding());

        stream.reset();
        EXPECT_FALSE(stream.discarding());
        EXPECT_EQ(stream.bufferedBytes(), 0U);
        EXPECT_EQ(ingest("<Idle|WPos:1,2,3>\n", stream, model), 1U);
    }

}  // namespace
