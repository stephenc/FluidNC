// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MachineClient.h"

#include "RealtimeCmd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace TS35 {
    namespace {

        constexpr size_t MaxCommandSize = 254;

        constexpr DispatchResult sent { DispatchStatus::Sent, "" };

        DispatchResult invalid(const char* reason) {
            return { DispatchStatus::InvalidArgument, reason };
        }

        bool validAxis(char axis) {
            constexpr std::string_view axes = "XYZABCUVW";
            return axes.find(axis) != std::string_view::npos;
        }

    }  // namespace

    DispatchResult MachineClient::authorize(MachineAction action, const MachineSnapshot& machine, bool confirmed) const {
        ActionDecision decision = evaluateAction(action, machine);
        if (decision.disposition == ActionDisposition::Rejected) {
            return { DispatchStatus::Rejected, decision.reason };
        }
        if (decision.disposition == ActionDisposition::ConfirmationRequired && !confirmed) {
            return { DispatchStatus::ConfirmationRequired, decision.reason };
        }
        return sent;
    }

    DispatchResult MachineClient::sendRealtime(uint8_t command) {
        return _sink.sendRealtime(command) ? sent : DispatchResult { DispatchStatus::TransportFull, "Command queue is full" };
    }

    DispatchResult MachineClient::sendLine(std::string_view command) {
        return _sink.sendLine(command) ? sent : DispatchResult { DispatchStatus::TransportFull, "Command queue is full" };
    }

    DispatchResult MachineClient::invoke(MachineAction action, const MachineSnapshot& machine, bool confirmed) {
        if (action == MachineAction::JogStart) {
            return invalid("JogStart requires bounded jog arguments");
        }
        if (action == MachineAction::FileStart) {
            return invalid("FileStart requires a validated SD path");
        }

        DispatchResult authorization = authorize(action, machine, confirmed);
        if (authorization.status != DispatchStatus::Sent) {
            return authorization;
        }

        switch (action) {
            case MachineAction::FeedHold:
                return sendRealtime(static_cast<uint8_t>(Cmd::FeedHold));
            case MachineAction::ResetAbort:
            case MachineAction::FileCancel:
                return sendRealtime(static_cast<uint8_t>(Cmd::Reset));
            case MachineAction::JogCancel:
                return sendRealtime(static_cast<uint8_t>(Cmd::JogCancel));
            case MachineAction::Home:
                return sendLine("$H");
            case MachineAction::Unlock:
                return sendLine("$X");
            case MachineAction::PenUp:
                return sendRealtime(static_cast<uint8_t>(Cmd::Macro0));
            case MachineAction::PenDown:
                return sendRealtime(static_cast<uint8_t>(Cmd::Macro1));
            case MachineAction::JogStart:
            case MachineAction::FileStart:
                break;
        }
        return invalid("Unsupported action");
    }

    DispatchResult MachineClient::startJog(const JogRequest& request, const MachineSnapshot& machine) {
        return sendJog(request, machine, false);
    }

    DispatchResult MachineClient::continueJog(const JogRequest& request, const MachineSnapshot& machine) {
        return sendJog(request, machine, true);
    }

    DispatchResult MachineClient::sendJog(const JogRequest& request, const MachineSnapshot& machine, bool continuation) {
        if (continuation) {
            if (!machine.connected) {
                return { DispatchStatus::Rejected, "Machine is disconnected" };
            }
            if (machine.sd_job_active) {
                return { DispatchStatus::Rejected, "Cannot jog while an SD job is active" };
            }
            if (machine.state != MachineState::Idle && machine.state != MachineState::Jog) {
                return { DispatchStatus::Rejected, "Jog continuation requires Idle or Jog state" };
            }
        } else {
            DispatchResult authorization = authorize(MachineAction::JogStart, machine, true);
            if (authorization.status != DispatchStatus::Sent) {
                return authorization;
            }
        }
        if (!validAxis(request.axis)) {
            return invalid("Jog axis is invalid");
        }
        if (request.direction != -1 && request.direction != 1) {
            return invalid("Jog direction must be -1 or 1");
        }
        if (!std::isfinite(request.distance_mm) || request.distance_mm <= 0.0f || request.distance_mm > _jog_limits.max_distance_mm) {
            return invalid("Jog distance is outside the configured finite bound");
        }
        if (!std::isfinite(request.feed_mm_min) || request.feed_mm_min <= 0.0f || request.feed_mm_min > _jog_limits.max_feed_mm_min) {
            return invalid("Jog feed is outside the configured bound");
        }

        std::array<char, 96> command {};
        int                  length = std::snprintf(command.data(),
                                   command.size(),
                                   "$J=G91G21F%.3f%c%.3f",
                                   static_cast<double>(request.feed_mm_min),
                                   request.axis,
                                   static_cast<double>(request.direction * request.distance_mm));
        if (length <= 0 || static_cast<size_t>(length) >= command.size()) {
            return invalid("Jog command does not fit the bounded buffer");
        }
        return sendLine(std::string_view(command.data(), static_cast<size_t>(length)));
    }

    DispatchResult MachineClient::startFile(std::string_view path, const MachineSnapshot& machine, bool confirmed) {
        DispatchResult authorization = authorize(MachineAction::FileStart, machine, confirmed);
        if (authorization.status != DispatchStatus::Sent) {
            return authorization;
        }
        if (path.empty()) {
            return invalid("SD path is empty");
        }
        if (path.find_first_of("\r\n") != std::string_view::npos) {
            return invalid("SD path contains a line break");
        }
        constexpr std::string_view prefix = "$SD/Run=";
        if (prefix.size() + path.size() > MaxCommandSize) {
            return invalid("SD path is too long");
        }

        std::array<char, MaxCommandSize + 1> command {};
        std::copy(prefix.begin(), prefix.end(), command.begin());
        std::copy(path.begin(), path.end(), command.begin() + prefix.size());
        return sendLine(std::string_view(command.data(), prefix.size() + path.size()));
    }

    DispatchResult MachineClient::requestFileList(std::string_view path, const MachineSnapshot& machine) {
        if (!machine.connected) {
            return { DispatchStatus::Rejected, "Machine is disconnected" };
        }
        if (path != "/sd" && (path.size() < 4 || path.substr(0, 4) != "/sd/")) {
            return invalid("File-list path must be on /sd");
        }
        if (path.find_first_of("\r\n") != std::string_view::npos || path.find("..") != std::string_view::npos) {
            return invalid("File-list path is unsafe");
        }
        constexpr std::string_view prefix = "$Files/ListGCode=";
        if (prefix.size() + path.size() > MaxCommandSize) {
            return invalid("File-list path is too long");
        }

        std::array<char, MaxCommandSize + 1> command {};
        std::copy(prefix.begin(), prefix.end(), command.begin());
        std::copy(path.begin(), path.end(), command.begin() + prefix.size());
        return sendLine(std::string_view(command.data(), prefix.size() + path.size()));
    }

}  // namespace TS35
