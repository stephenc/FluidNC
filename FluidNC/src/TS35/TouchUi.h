// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "FileListModel.h"
#include "MachineClient.h"
#include "TouchTracker.h"

#include <cstdint>

namespace TS35 {

    enum class UiPage : uint8_t { Status, Jog, Files };

    enum class WifiState : uint8_t { Off, Connecting, AccessPoint, Station };
    enum class JogPlane : uint8_t { XY, XZ, YZ };

    enum class UiControl : uint8_t {
        None,
        ShowStatus,
        ShowJog,
        ShowFiles,
        FeedHold,
        ResetAbort,
        Home,
        Unlock,
        JogXNegative,
        JogXPositive,
        JogYNegative,
        JogYPositive,
        JogPlaneNext,
        JogStepSmaller,
        JogStepLarger,
        JogSpeedSlower,
        JogSpeedFaster,
        PenUp,
        PenDown,
        FileRefresh,
        FileRow0,
        FileRow1,
        FileRow2,
        FileRow3,
        FileRow4,
        FilePrevious,
        FileNext,
        Confirm,
        Dismiss,
    };

    struct TouchUiState {
        UiPage        page                 = UiPage::Status;
        bool          confirmation_visible = false;
        MachineAction pending_action       = MachineAction::FeedHold;
        const char*   message              = "";
        size_t        file_offset           = 0;
        float         jog_distance_mm       = 1.0f;
        float         jog_feed_mm_min       = 500.0f;
        JogPlane      jog_plane             = JogPlane::XY;
        bool          z_is_pen_servo        = false;
        bool          pen_controls_enabled  = false;
        WifiState     wifi_state            = WifiState::Off;
        uint8_t       wifi_signal_percent   = 0;
        uint64_t      generation           = 0;
    };

    // Platform-neutral touch intent controller. It owns no machine state and
    // can affect FluidNC only through MachineClient's serialized boundary.
    class TouchUiController {
    public:
        explicit TouchUiController(MachineClient& client,
                                   uint32_t confirmation_timeout_ms = 5000,
                                   uint32_t jog_touch_timeout_ms = 250);

        void ingest(TouchEvent event,
                    uint32_t now_ms,
                    const MachineSnapshot& machine,
                    const FileListSnapshot* files = nullptr);
        // Raw contact samples keep a stationary jog alive without manufacturing
        // synthetic move events. The caller must invoke this only while contact
        // is physically present.
        void touchAlive(uint32_t now_ms) { _jog_safety.touchAlive(now_ms); }
        void setJogProfile(float distance_mm, float feed_mm_min, float max_feed_mm_min, uint32_t repeat_ms);
        void setJogProfile(float distance_mm, float feed_mm_min, uint32_t repeat_ms) {
            setJogProfile(distance_mm, feed_mm_min, _max_jog_feed_mm_min, repeat_ms);
        }
        void setWifiStatus(WifiState state, uint8_t signal_percent);
        void setZControlMode(bool is_pen_servo, bool controls_enabled);
        void poll(uint32_t now_ms, const MachineSnapshot& machine, const FileListSnapshot* files = nullptr);
        void disconnect(const MachineSnapshot& machine);

        const TouchUiState& state() const { return _state; }
        bool                jogActive() const { return _jog_safety.active(); }

        UiControl hitTest(ScreenPoint point) const;

    private:
        void setPage(UiPage page);
        void setMessage(const char* message);
        void activate(UiControl control,
                      uint32_t now_ms,
                      const MachineSnapshot& machine,
                      const FileListSnapshot* files);
        void startJog(UiControl control, uint32_t now_ms, const MachineSnapshot& machine);
        void repeatJog(uint32_t now_ms, const MachineSnapshot& machine);
        void stopJog(const MachineSnapshot& machine, bool disconnected = false);
        void adjustJogStep(bool larger);
        void adjustJogSpeed(bool faster);
        void requestAction(MachineAction action, uint32_t now_ms, const MachineSnapshot& machine);
        void requestFileList(std::string_view path, const MachineSnapshot& machine);
        void selectFile(size_t visible_row,
                        uint32_t now_ms,
                        const MachineSnapshot& machine,
                        const FileListSnapshot* files);
        void dismissConfirmation(const char* message);
        bool confirmationContextMatches(const MachineSnapshot& machine) const;

        MachineClient& _client;
        JogSafety      _jog_safety;
        TouchUiState   _state;
        UiControl      _pressed_control = UiControl::None;
        bool           _tap_eligible = false;
        UiControl      _active_jog_control = UiControl::None;
        uint32_t       _last_jog_segment_ms = 0;
        float          _jog_distance_mm = 1.0f;
        float          _jog_feed_mm_min = 500.0f;
        float          _max_jog_feed_mm_min = 5000.0f;
        uint32_t       _jog_repeat_ms = 250;
        uint32_t       _confirmation_timeout_ms;
        uint32_t       _confirmation_started_ms = 0;
        MachineState   _confirmation_state = MachineState::Disconnected;
        bool           _confirmation_job_active = false;
        std::string    _pending_file_path;
    };

}  // namespace TS35
