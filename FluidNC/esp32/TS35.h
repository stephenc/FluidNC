// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Channel.h"
#include "Module.h"
#include "TS35/FileListModel.h"
#include "TS35/MachineClient.h"
#include "TS35/MachineModel.h"
#include "TS35/PanelDiagnostic.h"
#include "TS35/ProtocolStream.h"
#include "TS35/ReportWatchdog.h"
#include "TS35/StatusPresenter.h"
#include "TS35/TouchCalibration.h"
#include "TS35/TouchTracker.h"
#include "TS35/TouchUi.h"

#include <array>

class SPIClass;
// FluidNC's report stream is the sole machine-state boundary for the display.
// The panel backend will consume model(); touch actions will be queued through
// Channel::push(), so neither side reaches into planner, SD, or WebUI internals.
class TS35Display : public Channel, public ConfigurableModule, public TS35::CommandSink {
public:
    explicit TS35Display(const char* name) : Channel(name), ConfigurableModule(name), _client(*this), _touch_ui(_client) {}

    TS35Display(const TS35Display&)            = delete;
    TS35Display(TS35Display&&)                 = delete;
    TS35Display& operator=(const TS35Display&) = delete;
    TS35Display& operator=(TS35Display&&)      = delete;

    void init() override;

    size_t write(uint8_t data) override;
    int    read() override { return -1; }
    int    peek() override { return -1; }

    Error pollLine(char* line) override;
    void  flushRx() override;
    void  beginJSON(const char* json_tag) override;
    void  endJSON(const char* json_tag) override;
    void  out_acked(const std::string& data, const char* tag) override;

    // Touch actions enqueue ordinary G-code through Channel::push().  Inherit
    // Channel::lineComplete() so newline-terminated commands are returned to
    // the protocol task.  The old display-only override always returned false
    // and therefore left every queued jog/home/file-list line unexecuted.
    size_t timedReadBytes(char*, size_t, TickType_t) override { return 0; }

    void validate() override;
    void afterParse() override;

    void group(Configuration::HandlerBase& handler) override {
        // @config report_interval_ms
        // @default 250
        // @tuning typical
        // How often FluidNC emits status reports to the display channel.
        handler.item("report_interval_ms", _report_interval_ms, 100, 5000);

        // @config disconnect_timeout_ms
        // @default 2000
        // @tuning typical
        // Mark displayed data unavailable when no valid status report arrives
        // within this interval. Must be longer than report_interval_ms.
        handler.item("disconnect_timeout_ms", _disconnect_timeout_ms, 500, 30000);

        // The pins are explicit configuration because constructing a Pin claims
        // it immediately. Pre-creating connector defaults here would make the
        // parser claim the same GPIO a second time when YAML overrides them.
        handler.item("sck_pin", _sck);
        handler.item("miso_pin", _miso);
        handler.item("mosi_pin", _mosi);
        handler.item("cs_pin", _cs);
        handler.item("dc_pin", _dc);
        handler.item("reset_pin", _reset);
        handler.item("backlight_pin", _backlight);
        handler.item("touch_cs_pin", _touch_cs);

        // The XPT2046-compatible controller shares the TFT SPI bus but must be
        // clocked much more slowly. Raw diagnostic mode is observational only:
        // it emits no FluidNC command and exists to derive this panel's actual
        // calibration from named owner gestures.
        handler.item("touch_frequency_hz", _touch_frequency_hz, 100000, 2500000);
        handler.item("touch_pressure_threshold", _touch_pressure_threshold, 20, 4095);
        handler.item("touch_diagnostic_mode", _touch_diagnostic_mode);
        handler.item("touch_ui_enabled", _touch_ui_enabled);
        handler.item("touch_calibration_x_min", _touch_calibration_x_min, 0, 4095);
        handler.item("touch_calibration_x_max", _touch_calibration_x_max, 0, 4095);
        handler.item("touch_calibration_y_min", _touch_calibration_y_min, 0, 4095);
        handler.item("touch_calibration_y_max", _touch_calibration_y_max, 0, 4095);
        handler.item("touch_swap_axes", _touch_swap_axes);
        handler.item("touch_invert_x", _touch_invert_x);
        handler.item("touch_invert_y", _touch_invert_y);

        // Stock firmware drives this panel at 40 MHz. Lower values are useful
        // for signal-integrity diagnosis without changing source.
        handler.item("frequency_hz", _frequency_hz, 1000000, 40000000);

        // Every press-and-hold jog is emitted as finite segments no larger
        // than this distance, in addition to realtime cancellation on release.
        handler.item("max_jog_distance_mm", _max_jog_distance_mm, 0.01f, 100.0f);
        handler.item("max_jog_feed_mm_min", _max_jog_feed_mm_min, 1.0f, 50000.0f);
        handler.item("jog_segment_mm", _jog_segment_mm, 0.01f, 100.0f);
        handler.item("jog_feed_mm_min", _jog_feed_mm_min, 1.0f, 50000.0f);
        handler.item("jog_repeat_ms", _jog_repeat_ms, 100, 2000);

        // When enabled, a selected XZ/YZ plane renders discrete PEN UP/PEN
        // DOWN controls instead of held Z jogging. Macro0 must be configured
        // as pen-up and Macro1 as pen-down in the machine YAML. The separate
        // enable is a commissioning interlock and defaults off.
        handler.item("z_is_pen_servo", _z_is_pen_servo);
        handler.item("pen_controls_enabled", _pen_controls_enabled);

        // Bench-only unattended panel proof. Cycles six deterministic cards;
        // touch remains disabled and no machine command is emitted.
        handler.item("diagnostic_mode", _diagnostic_mode);
        handler.item("diagnostic_card_ms", _diagnostic_card_ms, 1000, 30000);
    }

    const TS35::MachineSnapshot&  model() const { return _model.snapshot(); }
    const TS35::FileListSnapshot& files() const { return _files.snapshot(); }
    TS35::MachineClient&          client() { return _client; }

private:
    TS35::MachineModel   _model;
    TS35::ProtocolStream _protocol;
    TS35::ReportWatchdog _report_watchdog;
    TS35::MachineClient  _client;
    TS35::FileListModel  _files;
    TS35::TouchUiController _touch_ui;
    // Physical TS35 evidence showed pressure dropouts up to about 180 ms
    // during continuous edge drags and one isolated >150 px coordinate spike.
    // Ten 50 Hz release samples bridge that noise while the independent
    // 250 ms jog heartbeat still fails closed on genuinely lost contact.
    TS35::TouchTracker   _touch_tracker { 2, 10, 8, 80 };
    bool                 _receiving_file_list = false;
    bool                 _discarding_file_list = false;

    Pin _sck;
    Pin _miso;
    Pin _mosi;
    Pin _cs;
    Pin _dc;
    Pin _reset;
    Pin _backlight;
    Pin _touch_cs;

    SPIClass* _spi = nullptr;

    TS35::StatusView                              _view;
    TS35::TouchUiState                            _ui_view;
    TS35::FileListSnapshot                        _files_view;
    std::array<uint16_t, TS35::StatusView::Width> _scanline {};
    uint64_t                                      _rendered_generation = UINT64_MAX;
    uint64_t                                      _rendered_ui_generation = UINT64_MAX;
    uint64_t                                      _rendered_file_generation = UINT64_MAX;
    int                                           _next_row            = 0;

    int32_t  _report_interval_ms    = 250;
    int32_t  _disconnect_timeout_ms = 2000;
    int32_t  _frequency_hz          = 40000000;
    int32_t  _touch_frequency_hz       = 2000000;
    int32_t  _touch_pressure_threshold = 350;
    int32_t  _touch_calibration_x_min  = 0;
    int32_t  _touch_calibration_x_max  = 0;
    int32_t  _touch_calibration_y_min  = 0;
    int32_t  _touch_calibration_y_max  = 0;
    bool     _touch_swap_axes           = false;
    bool     _touch_invert_x            = false;
    bool     _touch_invert_y            = false;
    float    _max_jog_distance_mm   = 10.0f;
    float    _max_jog_feed_mm_min   = 5000.0f;
    float    _jog_segment_mm        = 1.0f;
    float    _jog_feed_mm_min       = 500.0f;
    int32_t  _jog_repeat_ms         = 250;
    bool     _diagnostic_mode       = false;
    bool     _touch_diagnostic_mode = false;
    bool     _touch_ui_enabled      = false;
    bool     _z_is_pen_servo        = false;
    bool     _pen_controls_enabled  = false;
    int32_t  _diagnostic_card_ms    = 3000;
    uint32_t _diagnostic_started_ms = 0;
    uint32_t _frame_started_us      = 0;
    uint32_t _frame_max_row_us      = 0;
    unsigned _diagnostic_card       = 0;
    uint32_t _ui_frame_count        = 0;
    bool     _error                 = false;
    bool     _touch_was_pressed     = false;
    uint32_t _last_touch_sample_ms  = 0;
    uint32_t _last_touch_log_ms     = 0;
    uint32_t _last_wifi_status_ms   = UINT32_MAX - 999U;

    void initPanel();
    void sendCommand(uint8_t command, const uint8_t* data = nullptr, size_t length = 0);
    void renderOneRow();
    void pollTouchDiagnostic(uint32_t now_ms);
    bool readTouchRaw(uint16_t& raw_x, uint16_t& raw_y, uint16_t& pressure);

    bool sendRealtime(uint8_t command) override;
    bool sendLine(std::string_view command) override;
};
