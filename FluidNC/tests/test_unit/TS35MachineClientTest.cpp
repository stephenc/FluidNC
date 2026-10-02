// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/MachineClient.h"

#include "RealtimeCmd.h"
#include "gtest/gtest.h"

#include <limits>
#include <string>
#include <vector>

namespace {
    using namespace TS35;

    class RecordingSink : public CommandSink {
    public:
        bool sendRealtime(uint8_t command) override {
            if (!accept) {
                return false;
            }
            realtime.push_back(command);
            return true;
        }

        bool sendLine(std::string_view command) override {
            if (!accept) {
                return false;
            }
            lines.emplace_back(command);
            return true;
        }

        bool                     accept = true;
        std::vector<uint8_t>     realtime;
        std::vector<std::string> lines;
    };

    MachineSnapshot connected(MachineState state) {
        MachineSnapshot machine;
        machine.connected = true;
        machine.state     = state;
        return machine;
    }

    TEST(TS35MachineClient, RejectedAndUnconfirmedActionsEmitNothing) {
        RecordingSink   sink;
        MachineClient   client(sink);
        MachineSnapshot disconnected;

        EXPECT_EQ(client.invoke(MachineAction::FeedHold, disconnected).status, DispatchStatus::Rejected);
        EXPECT_EQ(client.invoke(MachineAction::ResetAbort, connected(MachineState::Run)).status, DispatchStatus::ConfirmationRequired);
        EXPECT_TRUE(sink.realtime.empty());
        EXPECT_TRUE(sink.lines.empty());
    }

    TEST(TS35MachineClient, EmitsFluidNCRealtimeCommands) {
        RecordingSink sink;
        MachineClient client(sink);

        EXPECT_EQ(client.invoke(MachineAction::FeedHold, connected(MachineState::Run)).status, DispatchStatus::Sent);
        EXPECT_EQ(client.invoke(MachineAction::JogCancel, connected(MachineState::Jog)).status, DispatchStatus::Sent);
        EXPECT_EQ(client.invoke(MachineAction::ResetAbort, connected(MachineState::Run), true).status, DispatchStatus::Sent);

        ASSERT_EQ(sink.realtime.size(), 3U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::FeedHold));
        EXPECT_EQ(sink.realtime[1], static_cast<uint8_t>(Cmd::JogCancel));
        EXPECT_EQ(sink.realtime[2], static_cast<uint8_t>(Cmd::Reset));
    }

    TEST(TS35MachineClient, PenControlsUseDedicatedConfiguredMacros) {
        RecordingSink sink;
        MachineClient client(sink);
        auto idle = connected(MachineState::Idle);

        EXPECT_EQ(client.invoke(MachineAction::PenUp, idle).status, DispatchStatus::Sent);
        EXPECT_EQ(client.invoke(MachineAction::PenDown, idle).status, DispatchStatus::Sent);
        ASSERT_EQ(sink.realtime.size(), 2U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::Macro0));
        EXPECT_EQ(sink.realtime[1], static_cast<uint8_t>(Cmd::Macro1));

        EXPECT_EQ(client.invoke(MachineAction::PenDown, connected(MachineState::Run)).status, DispatchStatus::Rejected);
        EXPECT_EQ(sink.realtime.size(), 2U);
    }

    TEST(TS35MachineClient, EmitsConfirmedHomeAndUnlockLines) {
        RecordingSink sink;
        MachineClient client(sink);

        EXPECT_EQ(client.invoke(MachineAction::Home, connected(MachineState::Idle), true).status, DispatchStatus::Sent);
        EXPECT_EQ(client.invoke(MachineAction::Unlock, connected(MachineState::Alarm), true).status, DispatchStatus::Sent);

        EXPECT_EQ(sink.lines, (std::vector<std::string> { "$H", "$X" }));
    }

    TEST(TS35MachineClient, ConfirmationDoesNotOverrideChangedState) {
        RecordingSink sink;
        MachineClient client(sink);

        EXPECT_EQ(client.invoke(MachineAction::Home, connected(MachineState::Run), true).status, DispatchStatus::Rejected);
        EXPECT_EQ(client.startFile("job.nc", connected(MachineState::Hold), true).status, DispatchStatus::Rejected);
        EXPECT_TRUE(sink.lines.empty());
    }

    TEST(TS35MachineClient, JogIsFiniteFormattedAndBounded) {
        RecordingSink sink;
        MachineClient client(sink, { 2.0f, 1000.0f });
        auto          idle = connected(MachineState::Idle);

        EXPECT_EQ(client.startJog({ 'X', -1, 1.25f, 600.0f }, idle).status, DispatchStatus::Sent);
        ASSERT_EQ(sink.lines.size(), 1U);
        EXPECT_EQ(sink.lines[0], "$J=G91G21F600.000X-1.250");

        EXPECT_EQ(client.startJog({ 'Q', 1, 1.0f, 100.0f }, idle).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.startJog({ 'X', 0, 1.0f, 100.0f }, idle).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.startJog({ 'X', 1, 2.01f, 100.0f }, idle).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.startJog({ 'X', 1, 1.0f, 1000.01f }, idle).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.startJog({ 'X', 1, std::numeric_limits<float>::quiet_NaN(), 100.0f }, idle).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(sink.lines.size(), 1U);
    }

    TEST(TS35MachineClient, JogPolicyStillRejectsActiveJobsAndNonIdleState) {
        RecordingSink sink;
        MachineClient client(sink);
        auto          machine = connected(MachineState::Idle);
        machine.sd_job_active = true;

        EXPECT_EQ(client.startJog({ 'Y', 1, 1.0f, 100.0f }, machine).status, DispatchStatus::Rejected);
        machine.sd_job_active = false;
        machine.state         = MachineState::Jog;
        EXPECT_EQ(client.startJog({ 'Y', 1, 1.0f, 100.0f }, machine).status, DispatchStatus::Rejected);
        EXPECT_EQ(client.continueJog({ 'Y', 1, 1.0f, 100.0f }, machine).status, DispatchStatus::Sent);

        machine.state         = MachineState::Hold;
        EXPECT_EQ(client.startJog({ 'Y', 1, 1.0f, 100.0f }, machine).status, DispatchStatus::Rejected);
        EXPECT_EQ(client.continueJog({ 'Y', 1, 1.0f, 100.0f }, machine).status, DispatchStatus::Rejected);
        ASSERT_EQ(sink.lines.size(), 1U);
        EXPECT_EQ(sink.lines[0], "$J=G91G21F100.000Y1.000");
    }

    TEST(TS35MachineClient, FileStartRequiresConfirmationAndSanitizesPath) {
        RecordingSink sink;
        MachineClient client(sink);
        auto          idle = connected(MachineState::Idle);

        EXPECT_EQ(client.startFile("plots/test job.nc", idle).status, DispatchStatus::ConfirmationRequired);
        EXPECT_EQ(client.startFile("plots/test job.nc", idle, true).status, DispatchStatus::Sent);
        ASSERT_EQ(sink.lines.size(), 1U);
        EXPECT_EQ(sink.lines[0], "$SD/Run=plots/test job.nc");

        EXPECT_EQ(client.startFile("", idle, true).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.startFile("safe.nc\n$H", idle, true).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.startFile(std::string(247, 'a'), idle, true).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(sink.lines.size(), 1U);
    }

    TEST(TS35MachineClient, FileCancelUsesConfirmedRealtimeReset) {
        RecordingSink sink;
        MachineClient client(sink);
        auto          running = connected(MachineState::Run);
        running.sd_job_active = true;

        EXPECT_EQ(client.invoke(MachineAction::FileCancel, running).status, DispatchStatus::ConfirmationRequired);
        EXPECT_EQ(client.invoke(MachineAction::FileCancel, running, true).status, DispatchStatus::Sent);
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::Reset));
    }

    TEST(TS35MachineClient, ReportsTransportBackpressure) {
        RecordingSink sink;
        sink.accept = false;
        MachineClient client(sink);

        EXPECT_EQ(client.invoke(MachineAction::FeedHold, connected(MachineState::Run)).status, DispatchStatus::TransportFull);
        EXPECT_EQ(client.invoke(MachineAction::Home, connected(MachineState::Idle), true).status, DispatchStatus::TransportFull);
    }

    TEST(TS35MachineClient, ArgumentActionsCannotUseGenericInvoke) {
        RecordingSink sink;
        MachineClient client(sink);

        EXPECT_EQ(client.invoke(MachineAction::JogStart, connected(MachineState::Idle)).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.invoke(MachineAction::FileStart, connected(MachineState::Idle), true).status, DispatchStatus::InvalidArgument);
        EXPECT_TRUE(sink.lines.empty());
    }

    TEST(TS35MachineClient, FileListUsesSupportedCommandAndStaysOnSdVolume) {
        RecordingSink sink;
        MachineClient client(sink);
        auto          idle = connected(MachineState::Idle);

        EXPECT_EQ(client.requestFileList("/sd", idle).status, DispatchStatus::Sent);
        EXPECT_EQ(client.requestFileList("/sd/jobs", idle).status, DispatchStatus::Sent);
        EXPECT_EQ(sink.lines, (std::vector<std::string> { "$Files/ListGCode=/sd", "$Files/ListGCode=/sd/jobs" }));

        EXPECT_EQ(client.requestFileList("/localfs", idle).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.requestFileList("/sd/../localfs", idle).status, DispatchStatus::InvalidArgument);
        EXPECT_EQ(client.requestFileList("/sd\n$H", idle).status, DispatchStatus::InvalidArgument);
        MachineSnapshot disconnected;
        EXPECT_EQ(client.requestFileList("/sd", disconnected).status, DispatchStatus::Rejected);
        EXPECT_EQ(sink.lines.size(), 2U);
    }

}  // namespace
