// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ActionPolicy.h"

namespace TS35 {
    namespace {

        constexpr ActionDecision allowed { ActionDisposition::Allowed, "" };
        constexpr ActionDecision disconnected { ActionDisposition::Rejected, "Machine is disconnected" };

        ActionDecision confirmation(const char* reason) {
            return { ActionDisposition::ConfirmationRequired, reason };
        }

        ActionDecision rejected(const char* reason) {
            return { ActionDisposition::Rejected, reason };
        }

    }  // namespace

    ActionDecision evaluateAction(MachineAction action, const MachineSnapshot& machine) {
        if (!machine.connected) {
            return disconnected;
        }

        switch (action) {
            case MachineAction::FeedHold:
                switch (machine.state) {
                    case MachineState::Run:
                    case MachineState::Jog:
                    case MachineState::Homing:
                    case MachineState::Hold:
                        return allowed;
                    default:
                        return rejected("Feed hold is only valid during motion");
                }

            case MachineAction::ResetAbort:
                return confirmation("Reset stops the current operation and restarts FluidNC");

            case MachineAction::JogStart:
                if (machine.sd_job_active) {
                    return rejected("Cannot jog while an SD job is active");
                }
                if (machine.state == MachineState::Idle) {
                    return allowed;
                }
                return rejected("Starting a jog requires Idle state");

            case MachineAction::JogCancel:
                // A cancel is intentionally allowed for every connected state. It
                // is a safe idempotent response to stale UI or a lost release.
                return allowed;

            case MachineAction::Home:
                if (machine.state == MachineState::Idle || machine.state == MachineState::Alarm) {
                    return confirmation("Homing can move every configured axis");
                }
                return rejected("Homing requires Idle or Alarm state");

            case MachineAction::Unlock:
                if (machine.state == MachineState::Alarm) {
                    return confirmation("Unlock clears the alarm without homing");
                }
                return rejected("Unlock is only valid in Alarm state");

            case MachineAction::FileStart:
                if (machine.state == MachineState::Idle && !machine.sd_job_active) {
                    return confirmation("Starting this file can move the machine");
                }
                return rejected("File start requires Idle state and no active SD job");

            case MachineAction::FileCancel:
                if (machine.sd_job_active) {
                    return confirmation("Cancel the active SD job");
                }
                return rejected("No SD job is active");

            case MachineAction::PenUp:
            case MachineAction::PenDown:
                if (machine.state == MachineState::Idle && !machine.sd_job_active) {
                    return allowed;
                }
                return rejected("Pen control requires Idle state and no active SD job");
        }

        return rejected("Unsupported action");
    }

    bool JogSafety::press(uint32_t now_ms) {
        if (_active || _release_timeout_ms == 0) {
            return false;
        }
        _active        = true;
        _last_touch_ms = now_ms;
        return true;
    }

    void JogSafety::touchAlive(uint32_t now_ms) {
        if (_active) {
            _last_touch_ms = now_ms;
        }
    }

    bool JogSafety::release() {
        if (!_active) {
            return false;
        }
        _active = false;
        return true;
    }

    bool JogSafety::poll(uint32_t now_ms) {
        if (!_active || static_cast<uint32_t>(now_ms - _last_touch_ms) <= _release_timeout_ms) {
            return false;
        }
        _active = false;
        return true;
    }

    bool JogSafety::disconnect() {
        bool was_active = _active;
        _active         = false;
        return was_active;
    }

}  // namespace TS35
