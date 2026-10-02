// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/TouchUi.h"

#include "RealtimeCmd.h"
#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace {
    using namespace TS35;

    class RecordingSink : public CommandSink {
    public:
        bool sendRealtime(uint8_t command) override {
            realtime.push_back(command);
            return accept_realtime;
        }

        bool sendLine(std::string_view command) override {
            lines.emplace_back(command);
            return accept_lines;
        }

        std::vector<uint8_t>     realtime;
        std::vector<std::string> lines;
        bool                     accept_realtime = true;
        bool                     accept_lines    = true;
    };

    MachineSnapshot connected(MachineState state) {
        MachineSnapshot machine;
        machine.connected = true;
        machine.state = state;
        return machine;
    }

    TouchEvent touch(TouchEventKind kind, int16_t x, int16_t y) {
        return { kind, { x, y, true } };
    }

    void tap(TouchUiController& ui,
             int16_t x,
             int16_t y,
             uint32_t now,
             const MachineSnapshot& machine,
             const FileListSnapshot* files = nullptr) {
        ui.ingest(touch(TouchEventKind::Press, x, y), now, machine, files);
        ui.ingest(touch(TouchEventKind::Release, x, y), now + 1, machine, files);
    }

    TEST(TS35TouchUi, NavigationNeverEmitsMachineCommands) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto machine = connected(MachineState::Idle);

        tap(ui, 60, 280, 10, machine);
        EXPECT_EQ(ui.state().page, UiPage::Jog);
        tap(ui, 40, 30, 20, machine);
        EXPECT_EQ(ui.state().page, UiPage::Status);
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_TRUE(sink.realtime.empty());
    }

    TEST(TS35TouchUi, TapMustStayOnTheSameControl) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto running = connected(MachineState::Run);

        ui.ingest(touch(TouchEventKind::Press, 240, 280), 10, running);
        ui.ingest(touch(TouchEventKind::Move, 50, 200), 20, running);
        ui.ingest(touch(TouchEventKind::Move, 240, 280), 30, running);
        ui.ingest(touch(TouchEventKind::Release, 240, 280), 40, running);
        EXPECT_TRUE(sink.realtime.empty());

        tap(ui, 240, 280, 50, running);
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::FeedHold));
    }

    TEST(TS35TouchUi, ConfirmedActionRechecksMachineContext) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);

        tap(ui, 300, 280, 10, idle);
        ASSERT_TRUE(ui.state().confirmation_visible);
        EXPECT_EQ(ui.state().pending_action, MachineAction::Home);

        auto running = connected(MachineState::Run);
        ui.poll(20, running);
        EXPECT_FALSE(ui.state().confirmation_visible);
        tap(ui, 320, 260, 30, running);
        EXPECT_TRUE(sink.lines.empty());

        tap(ui, 300, 280, 40, idle);
        tap(ui, 320, 260, 50, idle);
        EXPECT_EQ(sink.lines, (std::vector<std::string> { "$H" }));
    }

    TEST(TS35TouchUi, ResetRequiresConfirmationAndDismissEmitsNothing) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto running = connected(MachineState::Run);

        tap(ui, 430, 280, 10, running);
        ASSERT_TRUE(ui.state().confirmation_visible);
        EXPECT_EQ(ui.state().pending_action, MachineAction::ResetAbort);
        tap(ui, 100, 260, 20, running);
        EXPECT_FALSE(ui.state().confirmation_visible);
        EXPECT_TRUE(sink.realtime.empty());

        tap(ui, 430, 280, 30, running);
        tap(ui, 320, 260, 40, running);
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::Reset));
    }

    TEST(TS35TouchUi, UnlockRequiresAlarmStateAndExplicitConfirmation) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto alarm = connected(MachineState::Alarm);
        tap(ui, 60, 280, 0, alarm);

        tap(ui, 80, 280, 10, alarm);
        ASSERT_TRUE(ui.state().confirmation_visible);
        EXPECT_EQ(ui.state().pending_action, MachineAction::Unlock);
        EXPECT_TRUE(sink.lines.empty());
        tap(ui, 320, 260, 20, alarm);
        EXPECT_EQ(sink.lines, (std::vector<std::string> { "$X" }));

        auto idle = connected(MachineState::Idle);
        tap(ui, 80, 280, 30, idle);
        EXPECT_FALSE(ui.state().confirmation_visible);
        EXPECT_EQ(sink.lines.size(), 1U);
    }

    TEST(TS35TouchUi, ConfirmationExpiresAcrossMillisWrap) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client, 100, 250);
        auto idle = connected(MachineState::Idle);

        tap(ui, 300, 280, 0xfffffff0U, idle);
        ASSERT_TRUE(ui.state().confirmation_visible);
        ui.poll(0x20U, idle);
        EXPECT_TRUE(ui.state().confirmation_visible);
        ui.poll(0x80U, idle);
        EXPECT_FALSE(ui.state().confirmation_visible);
        EXPECT_TRUE(sink.lines.empty());
    }

    TEST(TS35TouchUi, JogPressAndReleaseAlwaysEmitFiniteStartThenCancel) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        ui.ingest(touch(TouchEventKind::Press, 380, 160), 10, idle);
        ASSERT_TRUE(ui.jogActive());
        ASSERT_EQ(sink.lines.size(), 1U);
        EXPECT_EQ(sink.lines[0], "$J=G91G21F500.000X1.000");
        ui.ingest(touch(TouchEventKind::Release, 380, 160), 20, idle);
        EXPECT_FALSE(ui.jogActive());
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::JogCancel));
    }

    TEST(TS35TouchUi, DragOutAndCancelCannotEmitDuplicateJogCancel) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        ui.ingest(touch(TouchEventKind::Press, 90, 160), 10, idle);
        ui.ingest(touch(TouchEventKind::Move, 240, 160), 20, idle);
        ui.ingest(touch(TouchEventKind::Cancel, 240, 160), 30, idle);
        ui.ingest(touch(TouchEventKind::Release, 240, 160), 40, idle);
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::JogCancel));
    }

    TEST(TS35TouchUi, LostTouchHeartbeatCancelsJog) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client, 5000, 100);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        ui.ingest(touch(TouchEventKind::Press, 240, 100), 10, idle);
        ASSERT_TRUE(ui.jogActive());
        ui.poll(110, idle);
        EXPECT_TRUE(ui.jogActive());
        ui.poll(111, idle);
        EXPECT_FALSE(ui.jogActive());
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::JogCancel));
    }

    TEST(TS35TouchUi, RawContactHeartbeatKeepsAStationaryJogAlive) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client, 5000, 100);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        ui.ingest(touch(TouchEventKind::Press, 240, 100), 10, idle);
        ui.touchAlive(100);
        ui.poll(190, idle);
        EXPECT_TRUE(ui.jogActive());
        EXPECT_TRUE(sink.realtime.empty());
        ui.poll(201, idle);
        EXPECT_FALSE(ui.jogActive());
        EXPECT_EQ(sink.realtime.size(), 1U);
    }

    TEST(TS35TouchUi, HeldJogRepeatsOnlyFiniteReauthorizedSegments) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client, 5000, 500);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        ui.ingest(touch(TouchEventKind::Press, 380, 160), 10, idle);
        ASSERT_EQ(sink.lines.size(), 1U);
        ui.touchAlive(240);
        ui.poll(259, idle);
        EXPECT_EQ(sink.lines.size(), 1U);
        ui.poll(260, connected(MachineState::Jog));
        ASSERT_EQ(sink.lines.size(), 2U);
        EXPECT_EQ(sink.lines[0], "$J=G91G21F500.000X1.000");
        EXPECT_EQ(sink.lines[1], "$J=G91G21F500.000X1.000");

        ui.ingest(touch(TouchEventKind::Release, 380, 160), 270, connected(MachineState::Jog));
        EXPECT_FALSE(ui.jogActive());
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::JogCancel));
    }

    TEST(TS35TouchUi, StateChangeBeforeRepeatCancelsInsteadOfSendingAnotherSegment) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client, 5000, 500);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);
        ui.ingest(touch(TouchEventKind::Press, 380, 160), 10, idle);
        ui.touchAlive(250);

        ui.poll(260, connected(MachineState::Run));
        EXPECT_FALSE(ui.jogActive());
        EXPECT_EQ(sink.lines.size(), 1U);
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::JogCancel));
    }

    TEST(TS35TouchUi, FreshPressCannotAdoptAnAlreadyJoggingMachine) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto jogging = connected(MachineState::Jog);
        tap(ui, 60, 280, 0, jogging);

        ui.ingest(touch(TouchEventKind::Press, 380, 160), 10, jogging);
        EXPECT_FALSE(ui.jogActive());
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_TRUE(sink.realtime.empty());
        EXPECT_STREQ(ui.state().message, "Starting a jog requires Idle state");
    }

    TEST(TS35TouchUi, TransportFailureDuringRepeatCancelsAndRequiresFreshPress) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client, 5000, 500);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);
        ui.ingest(touch(TouchEventKind::Press, 380, 160), 10, idle);
        ASSERT_TRUE(ui.jogActive());
        ASSERT_EQ(sink.lines.size(), 1U);

        sink.accept_lines = false;
        ui.touchAlive(250);
        ui.poll(260, connected(MachineState::Jog));
        EXPECT_FALSE(ui.jogActive());
        EXPECT_EQ(sink.lines.size(), 2U);
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::JogCancel));

        sink.accept_lines = true;
        ui.poll(520, connected(MachineState::Jog));
        EXPECT_EQ(sink.lines.size(), 2U);
        EXPECT_EQ(sink.realtime.size(), 1U);
    }

    TEST(TS35TouchUi, JogProfileControlsEveryBoundedSegment) {
        RecordingSink sink;
        MachineClient client(sink, { 2.0f, 1000.0f });
        TouchUiController ui(client, 5000, 500);
        ui.setJogProfile(0.25f, 300.0f, 400);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        ui.ingest(touch(TouchEventKind::Press, 90, 160), 10, idle);
        ui.touchAlive(400);
        ui.poll(409, idle);
        EXPECT_EQ(sink.lines.size(), 1U);
        ui.poll(410, connected(MachineState::Jog));
        ASSERT_EQ(sink.lines.size(), 2U);
        EXPECT_EQ(sink.lines[0], "$J=G91G21F300.000X-0.250");
        EXPECT_EQ(sink.lines[1], "$J=G91G21F300.000X-0.250");
    }

    TEST(TS35TouchUi, JogSpeedControlsChangeFeedWithoutEmittingMotion) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        ui.setJogProfile(1.0f, 500.0f, 2000.0f, 250);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        uint64_t generation = ui.state().generation;
        tap(ui, 330, 280, 10, idle);
        EXPECT_FLOAT_EQ(ui.state().jog_feed_mm_min, 1000.0f);
        EXPECT_GT(ui.state().generation, generation);
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_TRUE(sink.realtime.empty());

        ui.ingest(touch(TouchEventKind::Press, 380, 160), 20, idle);
        ASSERT_EQ(sink.lines.size(), 1U);
        EXPECT_EQ(sink.lines[0], "$J=G91G21F1000.000X1.000");
        ui.ingest(touch(TouchEventKind::Release, 380, 160), 21, idle);

        tap(ui, 270, 280, 30, idle);
        EXPECT_FLOAT_EQ(ui.state().jog_feed_mm_min, 500.0f);
    }

    TEST(TS35TouchUi, JogSpeedCannotExceedConfiguredMaximum) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        ui.setJogProfile(1.0f, 500.0f, 750.0f, 250);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        tap(ui, 330, 280, 10, idle);
        EXPECT_FLOAT_EQ(ui.state().jog_feed_mm_min, 750.0f);
        uint64_t generation = ui.state().generation;
        tap(ui, 330, 280, 20, idle);
        EXPECT_FLOAT_EQ(ui.state().jog_feed_mm_min, 750.0f);
        EXPECT_EQ(ui.state().generation, generation);
    }

    TEST(TS35TouchUi, JogStepControlsChangeDistanceWithoutEmittingMotion) {
        RecordingSink sink;
        MachineClient client(sink, { 10.0f, 2000.0f });
        TouchUiController ui(client);
        ui.setJogProfile(1.0f, 500.0f, 2000.0f, 250);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        tap(ui, 200, 280, 10, idle);
        EXPECT_FLOAT_EQ(ui.state().jog_distance_mm, 5.0f);
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_TRUE(sink.realtime.empty());

        ui.ingest(touch(TouchEventKind::Press, 380, 160), 20, idle);
        ASSERT_EQ(sink.lines.size(), 1U);
        EXPECT_EQ(sink.lines[0], "$J=G91G21F500.000X5.000");
        ui.ingest(touch(TouchEventKind::Release, 380, 160), 21, idle);

        tap(ui, 140, 280, 30, idle);
        EXPECT_FLOAT_EQ(ui.state().jog_distance_mm, 1.0f);
    }

    TEST(TS35TouchUi, WifiStatusChangesOnlyInvalidateUiWhenVisibleValueChanges) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);

        uint64_t generation = ui.state().generation;
        ui.setWifiStatus(WifiState::Station, 70);
        EXPECT_EQ(ui.state().wifi_state, WifiState::Station);
        EXPECT_EQ(ui.state().wifi_signal_percent, 70);
        EXPECT_GT(ui.state().generation, generation);

        generation = ui.state().generation;
        ui.setWifiStatus(WifiState::Station, 70);
        EXPECT_EQ(ui.state().generation, generation);
        ui.setWifiStatus(WifiState::Station, 120);
        EXPECT_EQ(ui.state().wifi_signal_percent, 100);
        EXPECT_GT(ui.state().generation, generation);
    }

    TEST(TS35TouchUi, PlaneSelectorRemapsBothJogAxesWithoutMotionOnSelection) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);

        tap(ui, 200, 25, 10, idle);
        EXPECT_EQ(ui.state().jog_plane, JogPlane::XZ);
        EXPECT_TRUE(sink.lines.empty());
        ui.ingest(touch(TouchEventKind::Press, 240, 100), 20, idle);
        ASSERT_EQ(sink.lines.size(), 1U);
        EXPECT_EQ(sink.lines[0], "$J=G91G21F500.000Z1.000");
        ui.ingest(touch(TouchEventKind::Release, 240, 100), 21, idle);

        tap(ui, 200, 25, 30, idle);
        EXPECT_EQ(ui.state().jog_plane, JogPlane::YZ);
        ui.ingest(touch(TouchEventKind::Press, 380, 160), 40, idle);
        ASSERT_EQ(sink.lines.size(), 2U);
        EXPECT_EQ(sink.lines[1], "$J=G91G21F500.000Y1.000");
    }

    TEST(TS35TouchUi, PenServoControlsAreDiscreteAndCommissioningInterlocked) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);
        ui.setZControlMode(true, false);
        tap(ui, 60, 280, 0, idle);
        tap(ui, 200, 25, 10, idle);
        ASSERT_EQ(ui.state().jog_plane, JogPlane::XZ);

        tap(ui, 240, 100, 20, idle);
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_TRUE(sink.realtime.empty());
        EXPECT_STREQ(ui.state().message, "Pen controls are not commissioned");

        ui.setZControlMode(true, true);
        tap(ui, 240, 100, 30, idle);
        tap(ui, 240, 210, 40, idle);
        ASSERT_EQ(sink.realtime.size(), 2U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::Macro0));
        EXPECT_EQ(sink.realtime[1], static_cast<uint8_t>(Cmd::Macro1));
        EXPECT_TRUE(sink.lines.empty());
    }

    TEST(TS35TouchUi, DisconnectedAndRejectedTouchesEmitNothing) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        MachineSnapshot disconnected;

        tap(ui, 240, 280, 0, disconnected);
        tap(ui, 60, 280, 10, disconnected);
        ui.ingest(touch(TouchEventKind::Press, 380, 160), 20, disconnected);
        ui.disconnect(disconnected);
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_TRUE(sink.realtime.empty());
        EXPECT_FALSE(ui.jogActive());
    }

    TEST(TS35TouchUi, FileBrowsingUsesSupportedCommandsAndConfirmsStart) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);

        tap(ui, 140, 280, 0, idle);
        ASSERT_EQ(ui.state().page, UiPage::Files);
        ASSERT_EQ(sink.lines, (std::vector<std::string> { "$Files/ListGCode=/sd" }));

        FileListSnapshot files;
        files.valid = true;
        files.path = "/sd";
        files.count = 2;
        files.entries[0] = { "plots", -1 };
        files.entries[1] = { "drawing.nc", 1234 };

        tap(ui, 100, 84, 10, idle, &files);
        ASSERT_EQ(sink.lines.size(), 2U);
        EXPECT_EQ(sink.lines[1], "$Files/ListGCode=/sd/plots");

        tap(ui, 100, 118, 20, idle, &files);
        ASSERT_TRUE(ui.state().confirmation_visible);
        EXPECT_EQ(ui.state().pending_action, MachineAction::FileStart);
        tap(ui, 320, 260, 30, idle, &files);
        ASSERT_EQ(sink.lines.size(), 3U);
        EXPECT_EQ(sink.lines[2], "$SD/Run=/sd/drawing.nc");
    }

    TEST(TS35TouchUi, DisconnectWhileConfirmingFileStartEmitsNoStart) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);

        FileListSnapshot files;
        files.valid = true;
        files.path = "/sd";
        files.count = 1;
        files.entries[0] = { "drawing.nc", 1234 };
        tap(ui, 140, 280, 0, idle, &files);
        tap(ui, 100, 84, 10, idle, &files);
        ASSERT_TRUE(ui.state().confirmation_visible);
        ASSERT_TRUE(sink.lines.empty());

        MachineSnapshot disconnected;
        ui.disconnect(disconnected);
        EXPECT_FALSE(ui.state().confirmation_visible);
        tap(ui, 320, 260, 20, disconnected, &files);
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_TRUE(sink.realtime.empty());
    }

    TEST(TS35TouchUi, FileBrowserRejectsUnsafeNamesAndBoundsPages) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);
        FileListSnapshot files;
        files.valid = true;
        files.path = "/sd";
        files.count = 7;
        for (size_t index = 0; index < files.count; ++index) {
            files.entries[index] = { "file" + std::to_string(index) + ".nc", 1 };
        }
        files.entries[0].name = "../unsafe.nc";

        tap(ui, 140, 280, 0, idle, &files);
        tap(ui, 100, 84, 10, idle, &files);
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_FALSE(ui.state().confirmation_visible);

        tap(ui, 400, 290, 20, idle, &files);
        EXPECT_EQ(ui.state().file_offset, 5U);
        tap(ui, 400, 290, 30, idle, &files);
        EXPECT_EQ(ui.state().file_offset, 5U);
        tap(ui, 80, 290, 40, idle, &files);
        EXPECT_EQ(ui.state().file_offset, 0U);
    }

    TEST(TS35TouchUi, ResetControlUsesFileCancelForAnActiveSdJob) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto running = connected(MachineState::Run);
        running.sd_job_active = true;

        tap(ui, 430, 280, 0, running);
        ASSERT_TRUE(ui.state().confirmation_visible);
        EXPECT_EQ(ui.state().pending_action, MachineAction::FileCancel);
        tap(ui, 320, 260, 10, running);
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::Reset));
    }

    TEST(TS35TouchUi, DisconnectDuringJogClearsEveryTouchLatch) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);
        ui.ingest(touch(TouchEventKind::Press, 380, 160), 10, idle);
        ASSERT_TRUE(ui.jogActive());

        MachineSnapshot disconnected;
        ui.disconnect(disconnected);
        EXPECT_FALSE(ui.jogActive());
        ui.ingest(touch(TouchEventKind::Release, 380, 160), 20, disconnected);
        EXPECT_TRUE(sink.realtime.empty());
        ASSERT_EQ(sink.lines.size(), 1U);
    }

    TEST(TS35TouchUi, WatchdogDisconnectCancelsUsingLastConnectedSnapshotAndRequiresFreshPress) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto idle = connected(MachineState::Idle);
        tap(ui, 60, 280, 0, idle);
        ui.ingest(touch(TouchEventKind::Press, 380, 160), 10, idle);
        ASSERT_TRUE(ui.jogActive());
        ASSERT_EQ(sink.lines.size(), 1U);

        // TS35Display's watchdog disconnects the UI with its last accepted
        // connected snapshot before invalidating the machine model.
        ui.disconnect(idle);
        EXPECT_FALSE(ui.jogActive());
        ASSERT_EQ(sink.realtime.size(), 1U);
        EXPECT_EQ(sink.realtime[0], static_cast<uint8_t>(Cmd::JogCancel));

        MachineSnapshot disconnected;
        ui.ingest(touch(TouchEventKind::Release, 380, 160), 20, disconnected);
        ui.poll(30, disconnected);
        EXPECT_EQ(sink.lines.size(), 1U);
        EXPECT_EQ(sink.realtime.size(), 1U);

        // Reconnection alone cannot revive the old gesture. Only a fresh
        // physical press can start another finite segment.
        ui.poll(40, idle);
        EXPECT_EQ(sink.lines.size(), 1U);
        ui.ingest(touch(TouchEventKind::Press, 380, 160), 50, idle);
        ASSERT_EQ(sink.lines.size(), 2U);
        ui.ingest(touch(TouchEventKind::Release, 380, 160), 60, idle);
        EXPECT_EQ(sink.realtime.size(), 2U);
    }

    TEST(TS35TouchUi, DisconnectClearsPressedConfirmationAcrossSameStateReconnect) {
        RecordingSink sink;
        MachineClient client(sink);
        TouchUiController ui(client);
        auto running = connected(MachineState::Run);

        tap(ui, 430, 280, 0, running);
        ASSERT_TRUE(ui.state().confirmation_visible);
        EXPECT_EQ(ui.state().pending_action, MachineAction::ResetAbort);
        ui.ingest(touch(TouchEventKind::Press, 320, 260), 10, running);

        ui.disconnect(running);
        EXPECT_FALSE(ui.state().confirmation_visible);
        ui.ingest(touch(TouchEventKind::Release, 320, 260), 20, running);
        EXPECT_TRUE(sink.lines.empty());
        EXPECT_TRUE(sink.realtime.empty());
    }

}  // namespace
