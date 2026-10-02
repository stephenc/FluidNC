// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
// Protocol-model structure informed by FluidDial, copyright Mitch Bradley, GPLv3.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace TS35 {

    constexpr size_t MaxAxes       = 9;
    constexpr size_t MaxReportSize = 255;

    enum class MachineState : uint8_t {
        Disconnected,
        Idle,
        Jog,
        Run,
        Hold,
        Homing,
        Alarm,
        Check,
        Door,
        Sleep,
        Starting,
        Unknown,
    };

    struct MachineSnapshot {
        bool         connected = false;
        MachineState state     = MachineState::Disconnected;
        std::string  state_text { "Disconnected" };

        std::array<float, MaxAxes> machine_position {};
        std::array<float, MaxAxes> work_position {};
        std::array<float, MaxAxes> work_coordinate_offset {};
        size_t                     axis_count             = 0;
        bool                       machine_position_valid = false;
        bool                       work_position_valid    = false;
        bool                       offset_valid           = false;

        std::array<bool, MaxAxes> limits {};
        bool                      probe = false;

        float       feed_rate        = 0;
        float       spindle_speed    = 0;
        uint8_t     feed_override    = 100;
        uint8_t     rapid_override   = 100;
        uint8_t     spindle_override = 100;
        std::string accessories;

        bool        sd_job_active = false;
        float       sd_percent    = 0;
        std::string sd_filename;

        uint32_t selected_tool = 0;
        int      last_alarm    = 0;
        int      last_error    = 0;
        bool     inch_mode     = false;

        uint64_t generation = 0;
    };

    class MachineModel {
    public:
        const MachineSnapshot& snapshot() const { return _snapshot; }

        // Ingest one complete serialized FluidNC/Grbl report line. Unknown report
        // types are ignored. Malformed recognized reports never partially mutate
        // the current snapshot.
        bool ingestLine(std::string_view line);

        void markDisconnected();

    private:
        bool ingestStatus(std::string_view line);
        bool ingestAlarm(std::string_view line);
        bool ingestError(std::string_view line);
        bool ingestGcodeModes(std::string_view line);

        MachineSnapshot _snapshot;
    };

    const char* stateName(MachineState state);

}  // namespace TS35
