// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
// Protocol-model structure informed by FluidDial, copyright Mitch Bradley, GPLv3.

#include "MachineModel.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace TS35 {
    namespace {

        bool parseFloat(std::string_view text, float& value) {
            if (text.empty() || text.size() > 32) {
                return false;
            }
            std::string copy(text);
            char*       end = nullptr;
            errno           = 0;
            float parsed    = std::strtof(copy.c_str(), &end);
            if (errno == ERANGE || end != copy.c_str() + copy.size() || !std::isfinite(parsed)) {
                return false;
            }
            value = parsed;
            return true;
        }

        bool parseUnsigned(std::string_view text, uint32_t& value) {
            if (text.empty()) {
                return false;
            }
            auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc {} && result.ptr == text.data() + text.size();
        }

        bool parseLeadingUnsigned(std::string_view text, uint32_t& value) {
            size_t length = 0;
            while (length < text.size() && text[length] >= '0' && text[length] <= '9') {
                ++length;
            }
            return length != 0 && parseUnsigned(text.substr(0, length), value);
        }

        bool parseFloatList(std::string_view text, std::array<float, MaxAxes>& values, size_t& count) {
            count        = 0;
            size_t start = 0;
            while (start <= text.size()) {
                if (count == MaxAxes) {
                    return false;
                }
                size_t end = text.find(',', start);
                if (end == std::string_view::npos) {
                    end = text.size();
                }
                if (!parseFloat(text.substr(start, end - start), values[count++])) {
                    return false;
                }
                if (end == text.size()) {
                    break;
                }
                start = end + 1;
            }
            return count != 0;
        }

        bool parsePair(std::string_view text, float& first, float& second) {
            size_t comma = text.find(',');
            return comma != std::string_view::npos && text.find(',', comma + 1) == std::string_view::npos &&
                   parseFloat(text.substr(0, comma), first) && parseFloat(text.substr(comma + 1), second);
        }

        bool parseOverrides(std::string_view text, uint8_t& feed, uint8_t& rapid, uint8_t& spindle) {
            uint32_t parsed[3] {};
            size_t   start = 0;
            for (size_t index = 0; index < 3; ++index) {
                size_t end = text.find(',', start);
                if (end == std::string_view::npos) {
                    end = text.size();
                }
                if (!parseUnsigned(text.substr(start, end - start), parsed[index]) || parsed[index] > 255) {
                    return false;
                }
                if (index < 2 && end == text.size()) {
                    return false;
                }
                if (index == 2 && end != text.size()) {
                    return false;
                }
                start = end + 1;
            }
            feed    = static_cast<uint8_t>(parsed[0]);
            rapid   = static_cast<uint8_t>(parsed[1]);
            spindle = static_cast<uint8_t>(parsed[2]);
            return true;
        }

        MachineState decodeState(std::string_view state) {
            if (state == "Idle") {
                return MachineState::Idle;
            }
            if (state == "Jog") {
                return MachineState::Jog;
            }
            if (state == "Run") {
                return MachineState::Run;
            }
            if (state == "Hold" || state.substr(0, 5) == "Hold:") {
                return MachineState::Hold;
            }
            if (state == "Home" || state == "Homing") {
                return MachineState::Homing;
            }
            if (state == "Alarm" || state == "ConfigAlarm" || state == "Critical") {
                return MachineState::Alarm;
            }
            if (state == "Check") {
                return MachineState::Check;
            }
            if (state == "Door" || state.substr(0, 5) == "Door:") {
                return MachineState::Door;
            }
            if (state == "Sleep") {
                return MachineState::Sleep;
            }
            if (state == "Starting") {
                return MachineState::Starting;
            }
            return MachineState::Unknown;
        }

        int axisIndex(char axis) {
            constexpr std::string_view names = "XYZABCUVW";
            auto                       found = names.find(axis);
            return found == std::string_view::npos ? -1 : static_cast<int>(found);
        }

        void derivePositions(MachineSnapshot& snapshot) {
            if (!snapshot.offset_valid) {
                return;
            }
            if (snapshot.machine_position_valid) {
                for (size_t axis = 0; axis < snapshot.axis_count; ++axis) {
                    snapshot.work_position[axis] = snapshot.machine_position[axis] - snapshot.work_coordinate_offset[axis];
                }
                snapshot.work_position_valid = true;
            } else if (snapshot.work_position_valid) {
                for (size_t axis = 0; axis < snapshot.axis_count; ++axis) {
                    snapshot.machine_position[axis] = snapshot.work_position[axis] + snapshot.work_coordinate_offset[axis];
                }
                snapshot.machine_position_valid = true;
            }
        }

    }  // namespace

    const char* stateName(MachineState state) {
        switch (state) {
            case MachineState::Disconnected:
                return "Disconnected";
            case MachineState::Idle:
                return "Idle";
            case MachineState::Jog:
                return "Jog";
            case MachineState::Run:
                return "Run";
            case MachineState::Hold:
                return "Hold";
            case MachineState::Homing:
                return "Homing";
            case MachineState::Alarm:
                return "Alarm";
            case MachineState::Check:
                return "Check";
            case MachineState::Door:
                return "Door";
            case MachineState::Sleep:
                return "Sleep";
            case MachineState::Starting:
                return "Starting";
            case MachineState::Unknown:
                return "Unknown";
        }
        return "Unknown";
    }

    bool MachineModel::ingestLine(std::string_view line) {
        if (line.empty() || line.size() > MaxReportSize) {
            return false;
        }
        if (line.front() == '<') {
            return ingestStatus(line);
        }
        if (line.substr(0, 6) == "ALARM:") {
            return ingestAlarm(line);
        }
        if (line.substr(0, 6) == "error:") {
            return ingestError(line);
        }
        if (line.substr(0, 4) == "[GC:" && line.back() == ']') {
            return ingestGcodeModes(line);
        }
        return false;
    }

    bool MachineModel::ingestStatus(std::string_view line) {
        if (line.size() < 3 || line.back() != '>') {
            return false;
        }

        line.remove_prefix(1);
        line.remove_suffix(1);
        size_t separator = line.find('|');
        auto   stateText = line.substr(0, separator);
        if (stateText.empty()) {
            return false;
        }

        MachineSnapshot next = _snapshot;
        next.connected       = true;
        next.state           = decodeState(stateText);
        next.state_text.assign(stateText);
        next.machine_position_valid = false;
        next.work_position_valid    = false;
        next.axis_count             = 0;
        next.limits.fill(false);
        next.probe         = false;
        next.sd_job_active = false;
        next.sd_percent    = 0;
        next.sd_filename.clear();
        next.accessories.clear();

        size_t cursor = separator == std::string_view::npos ? line.size() : separator + 1;
        while (cursor < line.size()) {
            size_t end = line.find('|', cursor);
            if (end == std::string_view::npos) {
                end = line.size();
            }
            auto field = line.substr(cursor, end - cursor);
            if (field.empty()) {
                return false;
            }
            size_t colon = field.find(':');
            if (colon == std::string_view::npos || colon == 0) {
                return false;
            }
            auto tag   = field.substr(0, colon);
            auto value = field.substr(colon + 1);

            if (tag == "MPos" || tag == "WPos") {
                std::array<float, MaxAxes> position {};
                size_t                     count = 0;
                if (!parseFloatList(value, position, count)) {
                    return false;
                }
                next.axis_count = count;
                if (tag == "MPos") {
                    next.machine_position       = position;
                    next.machine_position_valid = true;
                } else {
                    next.work_position       = position;
                    next.work_position_valid = true;
                }
            } else if (tag == "WCO") {
                size_t count = 0;
                if (!parseFloatList(value, next.work_coordinate_offset, count)) {
                    return false;
                }
                next.offset_valid = true;
                if (next.axis_count == 0) {
                    next.axis_count = count;
                } else if (count < next.axis_count) {
                    return false;
                }
            } else if (tag == "FS") {
                if (!parsePair(value, next.feed_rate, next.spindle_speed)) {
                    return false;
                }
            } else if (tag == "Ov") {
                if (!parseOverrides(value, next.feed_override, next.rapid_override, next.spindle_override)) {
                    return false;
                }
            } else if (tag == "Pn") {
                for (char pin : value) {
                    if (pin == 'P') {
                        next.probe = true;
                        continue;
                    }
                    int index = axisIndex(pin);
                    if (index >= 0) {
                        next.limits[static_cast<size_t>(index)] = true;
                    }
                }
            } else if (tag == "A") {
                next.accessories.assign(value);
            } else if (tag == "SD") {
                size_t comma = value.find(',');
                if (comma == std::string_view::npos || !parseFloat(value.substr(0, comma), next.sd_percent) || next.sd_percent < 0 ||
                    next.sd_percent > 100 || comma + 1 == value.size()) {
                    return false;
                }
                next.sd_job_active = true;
                next.sd_filename.assign(value.substr(comma + 1));
            }
            cursor = end + 1;
        }

        derivePositions(next);
        ++next.generation;
        _snapshot = std::move(next);
        return true;
    }

    bool MachineModel::ingestAlarm(std::string_view line) {
        uint32_t alarm = 0;
        if (!parseLeadingUnsigned(line.substr(6), alarm) || alarm > std::numeric_limits<int>::max()) {
            return false;
        }
        MachineSnapshot next = _snapshot;
        next.connected       = true;
        next.state           = MachineState::Alarm;
        next.state_text      = "Alarm";
        next.last_alarm      = static_cast<int>(alarm);
        ++next.generation;
        _snapshot = std::move(next);
        return true;
    }

    bool MachineModel::ingestError(std::string_view line) {
        uint32_t error = 0;
        if (!parseLeadingUnsigned(line.substr(6), error) || error > std::numeric_limits<int>::max()) {
            return false;
        }
        MachineSnapshot next = _snapshot;
        next.connected       = true;
        next.last_error      = static_cast<int>(error);
        ++next.generation;
        _snapshot = std::move(next);
        return true;
    }

    bool MachineModel::ingestGcodeModes(std::string_view line) {
        MachineSnapshot next = _snapshot;
        line.remove_prefix(4);
        line.remove_suffix(1);
        size_t cursor = 0;
        while (cursor < line.size()) {
            while (cursor < line.size() && line[cursor] == ' ') {
                ++cursor;
            }
            size_t end = line.find(' ', cursor);
            if (end == std::string_view::npos) {
                end = line.size();
            }
            auto word = line.substr(cursor, end - cursor);
            if (word == "G20") {
                next.inch_mode = true;
            } else if (word == "G21") {
                next.inch_mode = false;
            } else if (word.size() > 1 && word.front() == 'T') {
                uint32_t tool = 0;
                if (!parseUnsigned(word.substr(1), tool)) {
                    return false;
                }
                next.selected_tool = tool;
            }
            cursor = end + 1;
        }
        next.connected = true;
        ++next.generation;
        _snapshot = std::move(next);
        return true;
    }

    void MachineModel::markDisconnected() {
        const uint64_t next_generation = _snapshot.generation + 1U;
        _snapshot                      = MachineSnapshot {};
        _snapshot.generation           = next_generation;
    }

}  // namespace TS35
