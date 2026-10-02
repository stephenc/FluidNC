// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "StatusPresenter.h"

#include <cstdio>

namespace TS35 {
    namespace {

        StatusTone toneFor(MachineState state) {
            switch (state) {
                case MachineState::Disconnected:
                    return StatusTone::Disconnected;
                case MachineState::Idle:
                    return StatusTone::Ready;
                case MachineState::Jog:
                case MachineState::Run:
                case MachineState::Homing:
                    return StatusTone::Active;
                case MachineState::Hold:
                case MachineState::Door:
                    return StatusTone::Paused;
                case MachineState::Alarm:
                case MachineState::Check:
                case MachineState::Sleep:
                case MachineState::Starting:
                case MachineState::Unknown:
                    return StatusTone::Warning;
            }
            return StatusTone::Warning;
        }

        std::string formatPosition(float value, bool inches) {
            char text[24];
            std::snprintf(text, sizeof(text), inches ? "%.4f" : "%.3f", static_cast<double>(value));
            return text;
        }

        std::string formatFloatPair(const char* label, float first, const char* secondLabel, float second) {
            char text[64];
            std::snprintf(text, sizeof(text), "%s %.0f  %s %.0f", label, static_cast<double>(first), secondLabel, static_cast<double>(second));
            return text;
        }

    }  // namespace

    StatusView composeStatusView(const MachineSnapshot& machine) {
        StatusView view;
        view.state = machine.connected ? machine.state_text : "Disconnected";
        view.tone  = machine.connected ? toneFor(machine.state) : StatusTone::Disconnected;

        const std::array<float, MaxAxes>* positions = nullptr;
        if (machine.connected && machine.work_position_valid) {
            view.position_caption = machine.inch_mode ? "WORK POSITION (IN)" : "WORK POSITION (MM)";
            positions             = &machine.work_position;
        } else if (machine.connected && machine.machine_position_valid) {
            view.position_caption = machine.inch_mode ? "MACHINE POSITION (IN)" : "MACHINE POSITION (MM)";
            positions             = &machine.machine_position;
        } else {
            view.position_caption = "POSITION UNAVAILABLE";
        }

        if (positions != nullptr) {
            for (size_t axis = 0; axis < view.axis_values.size() && axis < machine.axis_count; ++axis) {
                view.axis_values[axis] = formatPosition((*positions)[axis], machine.inch_mode);
            }
        }

        view.limit_text          = "LIMITS";
        bool           any_limit = false;
        constexpr char axes[]    = "XYZABCUVW";
        for (size_t axis = 0; axis < machine.limits.size(); ++axis) {
            if (machine.limits[axis]) {
                view.limit_text += any_limit ? "," : " ";
                view.limit_text += axes[axis];
                any_limit = true;
            }
        }
        if (machine.probe) {
            view.limit_text += any_limit ? ",PROBE" : " PROBE";
            any_limit = true;
        }
        if (!any_limit) {
            view.limit_text += " CLEAR";
        }

        view.feed_text = formatFloatPair("FEED", machine.feed_rate, "SPINDLE", machine.spindle_speed);
        view.tool_text = "TOOL " + std::to_string(machine.selected_tool);
        if (!machine.accessories.empty()) {
            view.tool_text += "  ACTIVE " + machine.accessories;
        }

        if (machine.sd_job_active) {
            char percent[16];
            std::snprintf(percent, sizeof(percent), "%.1f%%", static_cast<double>(machine.sd_percent));
            view.job_text = std::string(percent) + "  " + machine.sd_filename;
        } else {
            view.job_text = "NO ACTIVE SD JOB";
        }

        if (machine.last_alarm != 0 && machine.state == MachineState::Alarm) {
            view.alarm_text = "ALARM " + std::to_string(machine.last_alarm);
        } else if (machine.last_error != 0) {
            view.alarm_text = "LAST ERROR " + std::to_string(machine.last_error);
        }

        return view;
    }

}  // namespace TS35
