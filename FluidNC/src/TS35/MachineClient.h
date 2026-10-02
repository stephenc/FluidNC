// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
// Command-boundary structure informed by FluidDial, copyright Mitch Bradley, GPLv3.

#pragma once

#include "ActionPolicy.h"

#include <cstdint>
#include <string_view>

namespace TS35 {

    class CommandSink {
    public:
        virtual ~CommandSink() = default;

        virtual bool sendRealtime(uint8_t command)      = 0;
        virtual bool sendLine(std::string_view command) = 0;
    };

    enum class DispatchStatus : uint8_t {
        Sent,
        ConfirmationRequired,
        Rejected,
        InvalidArgument,
        TransportFull,
    };

    struct DispatchResult {
        DispatchStatus status;
        const char*    reason;
    };

    struct JogLimits {
        float max_distance_mm = 10.0f;
        float max_feed_mm_min = 5000.0f;
    };

    struct JogRequest {
        char   axis;
        int8_t direction;
        float  distance_mm;
        float  feed_mm_min;
    };

    class MachineClient {
    public:
        explicit MachineClient(CommandSink& sink, JogLimits limits = {}) : _sink(sink), _jog_limits(limits) {}

        // Actions without arguments. JogStart and FileStart must use the
        // specialized methods below so arbitrary text never crosses the boundary.
        DispatchResult invoke(MachineAction action, const MachineSnapshot& machine, bool confirmed = false);

        DispatchResult startJog(const JogRequest& request, const MachineSnapshot& machine);
        // Only a caller that already owns a live, release-watched gesture may
        // continue a finite jog while FluidNC reports Jog.
        DispatchResult continueJog(const JogRequest& request, const MachineSnapshot& machine);
        DispatchResult startFile(std::string_view path, const MachineSnapshot& machine, bool confirmed = false);
        DispatchResult requestFileList(std::string_view path, const MachineSnapshot& machine);

        void setJogLimits(JogLimits limits) { _jog_limits = limits; }
        const JogLimits& jogLimits() const { return _jog_limits; }

    private:
        DispatchResult authorize(MachineAction action, const MachineSnapshot& machine, bool confirmed) const;
        DispatchResult sendJog(const JogRequest& request, const MachineSnapshot& machine, bool continuation);
        DispatchResult sendRealtime(uint8_t command);
        DispatchResult sendLine(std::string_view command);

        CommandSink& _sink;
        JogLimits    _jog_limits;
    };

}  // namespace TS35
