// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TouchUi.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace TS35 {
    namespace {

        bool inside(ScreenPoint point, int left, int top, int right, int bottom) {
            return point.valid && point.x >= left && point.x < right && point.y >= top && point.y < bottom;
        }

        bool isJogControl(UiControl control) {
            return control == UiControl::JogXNegative || control == UiControl::JogXPositive ||
                   control == UiControl::JogYNegative || control == UiControl::JogYPositive;
        }

        void jogAxes(JogPlane plane, char& horizontal, char& vertical) {
            switch (plane) {
                case JogPlane::XY:
                    horizontal = 'X';
                    vertical = 'Y';
                    break;
                case JogPlane::XZ:
                    horizontal = 'X';
                    vertical = 'Z';
                    break;
                case JogPlane::YZ:
                    horizontal = 'Y';
                    vertical = 'Z';
                    break;
            }
        }

        bool makeJogRequest(UiControl control,
                            JogPlane plane,
                            float distance_mm,
                            float feed_mm_min,
                            JogRequest& request) {
            char horizontal = 'X';
            char vertical = 'Y';
            jogAxes(plane, horizontal, vertical);
            request = { 'X', 1, distance_mm, feed_mm_min };
            switch (control) {
                case UiControl::JogXNegative:
                    request.axis = horizontal;
                    request.direction = -1;
                    return true;
                case UiControl::JogXPositive:
                    request.axis = horizontal;
                    return true;
                case UiControl::JogYNegative:
                    request.axis = vertical;
                    request.direction = -1;
                    return true;
                case UiControl::JogYPositive:
                    request.axis = vertical;
                    return true;
                default:
                    return false;
            }
        }

        bool safeFileName(std::string_view name) {
            return !name.empty() && name != "." && name != ".." &&
                   name.find_first_of("/\\\r\n") == std::string_view::npos;
        }

        bool joinFilePath(std::string_view directory, std::string_view name, std::string& result) {
            if (!safeFileName(name) || directory.empty() || directory.size() > MaxFilePath ||
                directory.find("..") != std::string_view::npos || directory.find_first_of("\r\n") != std::string_view::npos) {
                return false;
            }
            size_t length = directory.size() + (directory.back() == '/' ? 0U : 1U) + name.size();
            if (length > MaxFilePath) {
                return false;
            }
            result.assign(directory);
            if (result.back() != '/') {
                result += '/';
            }
            result.append(name);
            return true;
        }

    }  // namespace

    TouchUiController::TouchUiController(MachineClient& client,
                                         uint32_t confirmation_timeout_ms,
                                         uint32_t jog_touch_timeout_ms) :
        _client(client),
        _jog_safety(jog_touch_timeout_ms),
        _confirmation_timeout_ms(confirmation_timeout_ms) {}

    void TouchUiController::setJogProfile(float distance_mm,
                                          float feed_mm_min,
                                          float max_feed_mm_min,
                                          uint32_t repeat_ms) {
        _jog_distance_mm = distance_mm;
        _jog_feed_mm_min = feed_mm_min;
        _max_jog_feed_mm_min = max_feed_mm_min;
        _jog_repeat_ms = repeat_ms;
        _state.jog_distance_mm = distance_mm;
        _state.jog_feed_mm_min = feed_mm_min;
    }

    void TouchUiController::setWifiStatus(WifiState state, uint8_t signal_percent) {
        signal_percent = std::min<uint8_t>(signal_percent, 100);
        if (_state.wifi_state == state && _state.wifi_signal_percent == signal_percent) {
            return;
        }
        _state.wifi_state = state;
        _state.wifi_signal_percent = signal_percent;
        ++_state.generation;
    }

    void TouchUiController::setZControlMode(bool is_pen_servo, bool controls_enabled) {
        if (_state.z_is_pen_servo == is_pen_servo && _state.pen_controls_enabled == controls_enabled) {
            return;
        }
        _state.z_is_pen_servo = is_pen_servo;
        _state.pen_controls_enabled = controls_enabled;
        ++_state.generation;
    }

    UiControl TouchUiController::hitTest(ScreenPoint point) const {
        if (!point.valid) {
            return UiControl::None;
        }
        if (_state.confirmation_visible) {
            if (inside(point, 20, 220, 220, 304)) {
                return UiControl::Dismiss;
            }
            if (inside(point, 260, 220, 460, 304)) {
                return UiControl::Confirm;
            }
            return UiControl::None;
        }
        if (_state.page == UiPage::Status) {
            if (inside(point, 0, 250, 96, 320)) {
                return UiControl::ShowJog;
            }
            if (inside(point, 96, 250, 192, 320)) {
                return UiControl::ShowFiles;
            }
            if (inside(point, 192, 250, 288, 320)) {
                return UiControl::FeedHold;
            }
            if (inside(point, 288, 250, 384, 320)) {
                return UiControl::Home;
            }
            if (inside(point, 384, 250, 480, 320)) {
                return UiControl::ResetAbort;
            }
            return UiControl::None;
        }

        if (inside(point, 0, 0, 100, 64)) {
            return UiControl::ShowStatus;
        }
        if (_state.page == UiPage::Files) {
            if (inside(point, 380, 0, 480, 64)) {
                return UiControl::FileRefresh;
            }
            if (inside(point, 8, 68, 472, 102)) {
                return UiControl::FileRow0;
            }
            if (inside(point, 8, 102, 472, 136)) {
                return UiControl::FileRow1;
            }
            if (inside(point, 8, 136, 472, 170)) {
                return UiControl::FileRow2;
            }
            if (inside(point, 8, 170, 472, 204)) {
                return UiControl::FileRow3;
            }
            if (inside(point, 8, 204, 472, 238)) {
                return UiControl::FileRow4;
            }
            if (inside(point, 4, 260, 156, 320)) {
                return UiControl::FilePrevious;
            }
            if (inside(point, 324, 260, 476, 320)) {
                return UiControl::FileNext;
            }
            return UiControl::None;
        }
        if (inside(point, 108, 0, 350, 56)) {
            return UiControl::JogPlaneNext;
        }
        if (inside(point, 40, 110, 150, 210)) {
            return UiControl::JogXNegative;
        }
        if (inside(point, 330, 110, 440, 210)) {
            return UiControl::JogXPositive;
        }
        if (inside(point, 180, 64, 300, 144)) {
            if (_state.z_is_pen_servo && _state.jog_plane != JogPlane::XY) {
                return UiControl::PenUp;
            }
            return UiControl::JogYPositive;
        }
        if (inside(point, 180, 176, 300, 256)) {
            if (_state.z_is_pen_servo && _state.jog_plane != JogPlane::XY) {
                return UiControl::PenDown;
            }
            return UiControl::JogYNegative;
        }
        if (inside(point, 108, 256, 174, 320)) {
            return UiControl::JogStepSmaller;
        }
        if (inside(point, 174, 256, 240, 320)) {
            return UiControl::JogStepLarger;
        }
        if (inside(point, 240, 256, 306, 320)) {
            return UiControl::JogSpeedSlower;
        }
        if (inside(point, 306, 256, 372, 320)) {
            return UiControl::JogSpeedFaster;
        }
        if (inside(point, 0, 256, 108, 320)) {
            return UiControl::Unlock;
        }
        if (inside(point, 372, 256, 480, 320)) {
            return UiControl::ResetAbort;
        }
        return UiControl::None;
    }

    void TouchUiController::setPage(UiPage page) {
        if (_state.page != page) {
            _state.page = page;
            ++_state.generation;
        }
    }

    void TouchUiController::setMessage(const char* message) {
        _state.message = message;
        ++_state.generation;
    }

    void TouchUiController::dismissConfirmation(const char* message) {
        if (_state.confirmation_visible) {
            _state.confirmation_visible = false;
            _pending_file_path.clear();
            setMessage(message);
        }
    }

    bool TouchUiController::confirmationContextMatches(const MachineSnapshot& machine) const {
        return machine.connected && machine.state == _confirmation_state && machine.sd_job_active == _confirmation_job_active;
    }

    void TouchUiController::requestAction(MachineAction action, uint32_t now_ms, const MachineSnapshot& machine) {
        DispatchResult result = _client.invoke(action, machine, false);
        if (result.status == DispatchStatus::ConfirmationRequired) {
            _state.confirmation_visible = true;
            _state.pending_action = action;
            _confirmation_started_ms = now_ms;
            _confirmation_state = machine.state;
            _confirmation_job_active = machine.sd_job_active;
            setMessage(result.reason);
            return;
        }
        setMessage(result.reason);
    }

    void TouchUiController::requestFileList(std::string_view path, const MachineSnapshot& machine) {
        DispatchResult result = _client.requestFileList(path, machine);
        if (result.status == DispatchStatus::Sent) {
            _state.file_offset = 0;
        }
        setMessage(result.reason);
    }

    void TouchUiController::selectFile(size_t visible_row,
                                       uint32_t now_ms,
                                       const MachineSnapshot& machine,
                                       const FileListSnapshot* files) {
        if (files == nullptr || !files->valid || files->loading) {
            setMessage("File list is not ready");
            return;
        }
        size_t index = _state.file_offset + visible_row;
        if (index >= files->count) {
            return;
        }
        std::string path;
        if (!joinFilePath(files->path, files->entries[index].name, path)) {
            setMessage("Unsafe file name");
            return;
        }
        if (files->entries[index].isDirectory()) {
            requestFileList(path, machine);
            return;
        }
        DispatchResult result = _client.startFile(path, machine, false);
        if (result.status == DispatchStatus::ConfirmationRequired) {
            _pending_file_path = path;
            _state.confirmation_visible = true;
            _state.pending_action = MachineAction::FileStart;
            _confirmation_started_ms = now_ms;
            _confirmation_state = machine.state;
            _confirmation_job_active = machine.sd_job_active;
        }
        setMessage(result.reason);
    }

    void TouchUiController::startJog(UiControl control, uint32_t now_ms, const MachineSnapshot& machine) {
        JogRequest request;
        if (!makeJogRequest(control, _state.jog_plane, _jog_distance_mm, _jog_feed_mm_min, request)) {
            return;
        }
        DispatchResult result = _client.startJog(request, machine);
        if (result.status == DispatchStatus::Sent) {
            _jog_safety.press(now_ms);
            _active_jog_control = control;
            _last_jog_segment_ms = now_ms;
        }
        setMessage(result.reason);
    }

    void TouchUiController::repeatJog(uint32_t now_ms, const MachineSnapshot& machine) {
        JogRequest request;
        if (!makeJogRequest(_active_jog_control, _state.jog_plane, _jog_distance_mm, _jog_feed_mm_min, request)) {
            stopJog(machine);
            return;
        }
        DispatchResult result = _client.continueJog(request, machine);
        if (result.status == DispatchStatus::Sent) {
            _last_jog_segment_ms = now_ms;
            return;
        }

        // A repeat that cannot be reauthorized or queued fails closed. Cancel
        // the already finite jog stream and require a fresh physical press.
        stopJog(machine);
        setMessage(result.reason);
    }

    void TouchUiController::stopJog(const MachineSnapshot& machine, bool disconnected) {
        bool should_cancel = disconnected ? _jog_safety.disconnect() : _jog_safety.release();
        _active_jog_control = UiControl::None;
        if (should_cancel && machine.connected) {
            DispatchResult result = _client.invoke(MachineAction::JogCancel, machine, true);
            setMessage(result.reason);
        }
    }

    void TouchUiController::adjustJogSpeed(bool faster) {
        // Deliberately finite, familiar feed presets. The configured maximum is
        // always an attainable final step even when it is not itself a preset.
        constexpr std::array<float, 10> Presets { 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f,
                                                   2000.0f, 5000.0f, 10000.0f, 20000.0f, 50000.0f };
        float next = _jog_feed_mm_min;
        if (faster) {
            for (float preset : Presets) {
                if (preset > _jog_feed_mm_min && preset <= _max_jog_feed_mm_min) {
                    next = preset;
                    break;
                }
            }
            if (next == _jog_feed_mm_min && _max_jog_feed_mm_min > _jog_feed_mm_min) {
                next = _max_jog_feed_mm_min;
            }
        } else {
            for (auto it = Presets.rbegin(); it != Presets.rend(); ++it) {
                if (*it < _jog_feed_mm_min && *it <= _max_jog_feed_mm_min) {
                    next = *it;
                    break;
                }
            }
        }
        if (std::fabs(next - _jog_feed_mm_min) >= 0.001f) {
            _jog_feed_mm_min = next;
            _state.jog_feed_mm_min = next;
            ++_state.generation;
        }
    }

    void TouchUiController::adjustJogStep(bool larger) {
        constexpr std::array<float, 6> Presets { 0.1f, 0.25f, 0.5f, 1.0f, 5.0f, 10.0f };
        float next = _jog_distance_mm;
        if (larger) {
            for (float preset : Presets) {
                if (preset > _jog_distance_mm && preset <= _client.jogLimits().max_distance_mm) {
                    next = preset;
                    break;
                }
            }
        } else {
            for (auto it = Presets.rbegin(); it != Presets.rend(); ++it) {
                if (*it < _jog_distance_mm && *it <= _client.jogLimits().max_distance_mm) {
                    next = *it;
                    break;
                }
            }
        }
        if (std::fabs(next - _jog_distance_mm) >= 0.001f) {
            _jog_distance_mm = next;
            _state.jog_distance_mm = next;
            ++_state.generation;
        }
    }

    void TouchUiController::activate(UiControl control,
                                     uint32_t now_ms,
                                     const MachineSnapshot& machine,
                                     const FileListSnapshot* files) {
        switch (control) {
            case UiControl::ShowStatus:
                setPage(UiPage::Status);
                break;
            case UiControl::ShowJog:
                setPage(UiPage::Jog);
                break;
            case UiControl::ShowFiles:
                setPage(UiPage::Files);
                if (files == nullptr || (!files->valid && !files->loading)) {
                    requestFileList("/sd", machine);
                }
                break;
            case UiControl::FeedHold:
                requestAction(MachineAction::FeedHold, now_ms, machine);
                break;
            case UiControl::ResetAbort:
                requestAction(machine.sd_job_active ? MachineAction::FileCancel : MachineAction::ResetAbort, now_ms, machine);
                break;
            case UiControl::Home:
                requestAction(MachineAction::Home, now_ms, machine);
                break;
            case UiControl::Unlock:
                requestAction(MachineAction::Unlock, now_ms, machine);
                break;
            case UiControl::JogStepSmaller:
                adjustJogStep(false);
                break;
            case UiControl::JogStepLarger:
                adjustJogStep(true);
                break;
            case UiControl::JogSpeedSlower:
                adjustJogSpeed(false);
                break;
            case UiControl::JogSpeedFaster:
                adjustJogSpeed(true);
                break;
            case UiControl::JogPlaneNext:
                _state.jog_plane = _state.jog_plane == JogPlane::XY
                                       ? JogPlane::XZ
                                       : (_state.jog_plane == JogPlane::XZ ? JogPlane::YZ : JogPlane::XY);
                ++_state.generation;
                break;
            case UiControl::PenUp:
            case UiControl::PenDown:
                if (!_state.pen_controls_enabled) {
                    setMessage("Pen controls are not commissioned");
                } else {
                    MachineAction action = control == UiControl::PenUp ? MachineAction::PenUp : MachineAction::PenDown;
                    DispatchResult result = _client.invoke(action, machine, true);
                    setMessage(result.reason);
                }
                break;
            case UiControl::FileRefresh:
                requestFileList(files != nullptr && files->valid ? files->path : std::string_view("/sd"), machine);
                break;
            case UiControl::FileRow0:
            case UiControl::FileRow1:
            case UiControl::FileRow2:
            case UiControl::FileRow3:
            case UiControl::FileRow4:
                selectFile(static_cast<size_t>(control) - static_cast<size_t>(UiControl::FileRow0), now_ms, machine, files);
                break;
            case UiControl::FilePrevious:
                if (_state.file_offset >= 5) {
                    _state.file_offset -= 5;
                    ++_state.generation;
                }
                break;
            case UiControl::FileNext:
                if (files != nullptr && _state.file_offset + 5 < files->count) {
                    _state.file_offset += 5;
                    ++_state.generation;
                }
                break;
            case UiControl::Dismiss:
                dismissConfirmation("Cancelled");
                break;
            case UiControl::Confirm: {
                if (!confirmationContextMatches(machine)) {
                    dismissConfirmation("Machine state changed");
                    break;
                }
                MachineAction action = _state.pending_action;
                _state.confirmation_visible = false;
                DispatchResult result = action == MachineAction::FileStart
                                            ? _client.startFile(_pending_file_path, machine, true)
                                            : _client.invoke(action, machine, true);
                _pending_file_path.clear();
                setMessage(result.reason);
                break;
            }
            case UiControl::JogXNegative:
            case UiControl::JogXPositive:
            case UiControl::JogYNegative:
            case UiControl::JogYPositive:
            case UiControl::None:
                break;
        }
    }

    void TouchUiController::ingest(TouchEvent event,
                                   uint32_t now_ms,
                                   const MachineSnapshot& machine,
                                   const FileListSnapshot* files) {
        switch (event.kind) {
            case TouchEventKind::Press:
                _pressed_control = hitTest(event.point);
                _tap_eligible = _pressed_control != UiControl::None;
                if (isJogControl(_pressed_control)) {
                    startJog(_pressed_control, now_ms, machine);
                }
                break;
            case TouchEventKind::Move: {
                UiControl current = hitTest(event.point);
                if (isJogControl(_pressed_control) && current == _pressed_control && _jog_safety.active()) {
                    _jog_safety.touchAlive(now_ms);
                } else if (isJogControl(_pressed_control)) {
                    stopJog(machine);
                    _tap_eligible = false;
                } else if (current != _pressed_control) {
                    _tap_eligible = false;
                }
                break;
            }
            case TouchEventKind::Release: {
                UiControl released = hitTest(event.point);
                if (isJogControl(_pressed_control)) {
                    stopJog(machine);
                } else if (_tap_eligible && released == _pressed_control) {
                    activate(released, now_ms, machine, files);
                }
                _pressed_control = UiControl::None;
                _tap_eligible = false;
                break;
            }
            case TouchEventKind::Cancel:
                if (isJogControl(_pressed_control) || _jog_safety.active()) {
                    stopJog(machine);
                }
                _pressed_control = UiControl::None;
                _tap_eligible = false;
                break;
            case TouchEventKind::None:
                break;
        }
    }

    void TouchUiController::poll(uint32_t now_ms, const MachineSnapshot& machine, const FileListSnapshot* files) {
        if (_state.confirmation_visible &&
            static_cast<uint32_t>(now_ms - _confirmation_started_ms) >= _confirmation_timeout_ms) {
            dismissConfirmation("Confirmation expired");
        } else if (_state.confirmation_visible && !confirmationContextMatches(machine)) {
            dismissConfirmation("Machine state changed");
        }
        if (!machine.connected) {
            stopJog(machine, true);
        } else if (_jog_safety.poll(now_ms)) {
            DispatchResult result = _client.invoke(MachineAction::JogCancel, machine, true);
            _active_jog_control = UiControl::None;
            setMessage(result.reason);
        } else if (_jog_safety.active() && static_cast<uint32_t>(now_ms - _last_jog_segment_ms) >= _jog_repeat_ms) {
            repeatJog(now_ms, machine);
        }
        if (files != nullptr && _state.file_offset >= files->count && _state.file_offset != 0) {
            _state.file_offset = files->count == 0 ? 0 : ((files->count - 1) / 5) * 5;
            ++_state.generation;
        }
    }

    void TouchUiController::disconnect(const MachineSnapshot& machine) {
        stopJog(machine, true);
        dismissConfirmation("Disconnected");
        _pressed_control = UiControl::None;
        _tap_eligible = false;
    }

}  // namespace TS35
