// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "MachineModel.h"

#include <array>
#include <cstdint>
#include <string>

namespace TS35 {

    enum class StatusTone : uint8_t {
        Disconnected,
        Ready,
        Active,
        Paused,
        Warning,
    };

    struct StatusView {
        static constexpr int Width  = 480;
        static constexpr int Height = 320;

        std::string state;
        StatusTone  tone = StatusTone::Disconnected;

        std::string                position_caption;
        std::array<std::string, 3> axis_labels { "X", "Y", "Z" };
        std::array<std::string, 3> axis_values { "--", "--", "--" };

        std::string limit_text;
        std::string feed_text;
        std::string tool_text;
        std::string job_text;
        std::string alarm_text;

        bool operator==(const StatusView& other) const {
            return state == other.state && tone == other.tone && position_caption == other.position_caption &&
                   axis_labels == other.axis_labels && axis_values == other.axis_values && limit_text == other.limit_text &&
                   feed_text == other.feed_text && tool_text == other.tool_text && job_text == other.job_text &&
                   alarm_text == other.alarm_text;
        }
        bool operator!=(const StatusView& other) const { return !(*this == other); }
    };

    StatusView composeStatusView(const MachineSnapshot& machine);

}  // namespace TS35
