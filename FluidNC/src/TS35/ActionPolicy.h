// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "MachineModel.h"

#include <cstdint>

namespace TS35 {

    enum class MachineAction : uint8_t {
        FeedHold,
        ResetAbort,
        JogStart,
        JogCancel,
        Home,
        Unlock,
        FileStart,
        FileCancel,
        PenUp,
        PenDown,
    };

    enum class ActionDisposition : uint8_t {
        Allowed,
        ConfirmationRequired,
        Rejected,
    };

    struct ActionDecision {
        ActionDisposition disposition;
        const char*       reason;
    };

    ActionDecision evaluateAction(MachineAction action, const MachineSnapshot& machine);

    // Tracks touch liveness separately from command transport. A caller sends a
    // bounded jog when press() succeeds and must send realtime JogCancel whenever
    // release() or poll() returns true.
    class JogSafety {
    public:
        explicit JogSafety(uint32_t release_timeout_ms) : _release_timeout_ms(release_timeout_ms) {}

        bool press(uint32_t now_ms);
        void touchAlive(uint32_t now_ms);
        bool release();
        bool poll(uint32_t now_ms);
        bool disconnect();

        bool active() const { return _active; }

    private:
        uint32_t _release_timeout_ms;
        uint32_t _last_touch_ms = 0;
        bool     _active        = false;
    };

}  // namespace TS35
