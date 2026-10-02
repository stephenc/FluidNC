// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/ActionPolicy.h"

#include "gtest/gtest.h"

namespace {
    using namespace TS35;

    MachineSnapshot connected(MachineState state) {
        MachineSnapshot machine;
        machine.connected = true;
        machine.state     = state;
        return machine;
    }

    TEST(TS35ActionPolicy, RejectsEveryActionWhileDisconnected) {
        MachineSnapshot machine;
        for (auto action : { MachineAction::FeedHold,
                             MachineAction::ResetAbort,
                             MachineAction::JogStart,
                             MachineAction::JogCancel,
                             MachineAction::Home,
                             MachineAction::Unlock,
                             MachineAction::FileStart,
                             MachineAction::FileCancel,
                             MachineAction::PenUp,
                             MachineAction::PenDown }) {
            EXPECT_EQ(evaluateAction(action, machine).disposition, ActionDisposition::Rejected);
        }
    }

    TEST(TS35ActionPolicy, FeedHoldUsesMotionStatesOnly) {
        for (auto state : { MachineState::Run, MachineState::Jog, MachineState::Homing, MachineState::Hold }) {
            EXPECT_EQ(evaluateAction(MachineAction::FeedHold, connected(state)).disposition, ActionDisposition::Allowed);
        }
        for (auto state : { MachineState::Idle, MachineState::Alarm, MachineState::Door, MachineState::Sleep }) {
            EXPECT_EQ(evaluateAction(MachineAction::FeedHold, connected(state)).disposition, ActionDisposition::Rejected);
        }
    }

    TEST(TS35ActionPolicy, SafetyCriticalActionsRequireConfirmation) {
        EXPECT_EQ(evaluateAction(MachineAction::ResetAbort, connected(MachineState::Run)).disposition,
                  ActionDisposition::ConfirmationRequired);
        EXPECT_EQ(evaluateAction(MachineAction::Home, connected(MachineState::Idle)).disposition, ActionDisposition::ConfirmationRequired);
        EXPECT_EQ(evaluateAction(MachineAction::Home, connected(MachineState::Alarm)).disposition, ActionDisposition::ConfirmationRequired);
        EXPECT_EQ(evaluateAction(MachineAction::Unlock, connected(MachineState::Alarm)).disposition, ActionDisposition::ConfirmationRequired);
        EXPECT_EQ(evaluateAction(MachineAction::FileStart, connected(MachineState::Idle)).disposition,
                  ActionDisposition::ConfirmationRequired);
    }

    TEST(TS35ActionPolicy, JogRequiresIdleAndNoActiveJob) {
        auto machine = connected(MachineState::Idle);
        EXPECT_EQ(evaluateAction(MachineAction::JogStart, machine).disposition, ActionDisposition::Allowed);

        machine.sd_job_active = true;
        EXPECT_EQ(evaluateAction(MachineAction::JogStart, machine).disposition, ActionDisposition::Rejected);

        machine.sd_job_active = false;
        machine.state         = MachineState::Jog;
        EXPECT_EQ(evaluateAction(MachineAction::JogStart, machine).disposition, ActionDisposition::Rejected);

        machine.state         = MachineState::Hold;
        EXPECT_EQ(evaluateAction(MachineAction::JogStart, machine).disposition, ActionDisposition::Rejected);
    }

    TEST(TS35ActionPolicy, JogCancelIsIdempotentlyAllowedWhenConnected) {
        for (auto state : { MachineState::Idle, MachineState::Jog, MachineState::Alarm, MachineState::Unknown }) {
            EXPECT_EQ(evaluateAction(MachineAction::JogCancel, connected(state)).disposition, ActionDisposition::Allowed);
        }
    }

    TEST(TS35ActionPolicy, FileActionsRequireConsistentJobState) {
        auto machine = connected(MachineState::Idle);
        EXPECT_EQ(evaluateAction(MachineAction::FileCancel, machine).disposition, ActionDisposition::Rejected);

        machine.sd_job_active = true;
        machine.state         = MachineState::Run;
        EXPECT_EQ(evaluateAction(MachineAction::FileCancel, machine).disposition, ActionDisposition::ConfirmationRequired);
        EXPECT_EQ(evaluateAction(MachineAction::FileStart, machine).disposition, ActionDisposition::Rejected);
    }

    TEST(TS35ActionPolicy, PenControlsRequireIdleWithoutAnActiveJob) {
        auto machine = connected(MachineState::Idle);
        EXPECT_EQ(evaluateAction(MachineAction::PenUp, machine).disposition, ActionDisposition::Allowed);
        EXPECT_EQ(evaluateAction(MachineAction::PenDown, machine).disposition, ActionDisposition::Allowed);

        machine.sd_job_active = true;
        EXPECT_EQ(evaluateAction(MachineAction::PenUp, machine).disposition, ActionDisposition::Rejected);
        machine.sd_job_active = false;
        machine.state = MachineState::Run;
        EXPECT_EQ(evaluateAction(MachineAction::PenDown, machine).disposition, ActionDisposition::Rejected);
    }

    TEST(TS35JogSafety, ReleaseProducesExactlyOneCancel) {
        JogSafety jog(300);
        ASSERT_TRUE(jog.press(1000));
        EXPECT_TRUE(jog.active());
        EXPECT_TRUE(jog.release());
        EXPECT_FALSE(jog.active());
        EXPECT_FALSE(jog.release());
    }

    TEST(TS35JogSafety, LostReleaseTimesOutAfterTouchHeartbeatStops) {
        JogSafety jog(300);
        ASSERT_TRUE(jog.press(1000));
        jog.touchAlive(1200);
        EXPECT_FALSE(jog.poll(1500));
        EXPECT_TRUE(jog.poll(1501));
        EXPECT_FALSE(jog.active());
        EXPECT_FALSE(jog.poll(2000));
    }

    TEST(TS35JogSafety, TimeoutArithmeticSurvivesMillisWrap) {
        JogSafety jog(20);
        ASSERT_TRUE(jog.press(0xfffffff0U));
        EXPECT_FALSE(jog.poll(4));
        EXPECT_TRUE(jog.poll(5));
    }

    TEST(TS35JogSafety, DisconnectClearsLatchedPress) {
        JogSafety jog(300);
        ASSERT_TRUE(jog.press(1000));
        EXPECT_TRUE(jog.disconnect());
        EXPECT_FALSE(jog.active());
        EXPECT_FALSE(jog.disconnect());
    }

}  // namespace
