// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35.h"

#include "Machine/MachineConfig.h"
#include "Serial.h"
#include "TS35/StatusRasterizer.h"

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <sdkconfig.h>

#include <algorithm>
#include <cstring>
#include <utility>

#if defined(CONFIG_IDF_TARGET_ESP32)

namespace {
    // ST7796 initialization values are derived from Bodmer's TFT_eSPI
    // ST7796_Init.h (FreeBSD license) as bundled in Makerbase MKS-DLC32
    // firmware. Commands are issued independently here; no TFT_eSPI or LVGL
    // dependency is introduced.
    constexpr uint8_t PositiveGamma[] = { 0xf0, 0x09, 0x0b, 0x06, 0x04, 0x15, 0x2f, 0x54, 0x42, 0x3c, 0x17, 0x14, 0x18, 0x1b };
    constexpr uint8_t NegativeGamma[] = { 0xf0, 0x09, 0x0b, 0x06, 0x04, 0x03, 0x2d, 0x43, 0x42, 0x3b, 0x16, 0x14, 0x17, 0x1b };
}

void TS35Display::validate() {
    Assert(_sck.defined(), "ts35 sck_pin must be configured");
    Assert(_miso.defined(), "ts35 miso_pin must be configured");
    Assert(_mosi.defined(), "ts35 mosi_pin must be configured");
    Assert(_cs.defined(), "ts35 cs_pin must be configured");
    Assert(_dc.defined(), "ts35 dc_pin must be configured");
    Assert(_reset.defined(), "ts35 reset_pin must be configured");
    Assert(_backlight.defined(), "ts35 backlight_pin must be configured");
    Assert(_touch_cs.defined(), "ts35 touch_cs_pin must be configured");
}

void TS35Display::afterParse() {
    if (_disconnect_timeout_ms <= _report_interval_ms) {
        log_error("ts35 disconnect_timeout_ms must be greater than report_interval_ms");
        _error = true;
    }
    TS35::TouchCalibration calibration { static_cast<uint16_t>(_touch_calibration_x_min),
                                         static_cast<uint16_t>(_touch_calibration_x_max),
                                         static_cast<uint16_t>(_touch_calibration_y_min),
                                         static_cast<uint16_t>(_touch_calibration_y_max),
                                         _touch_swap_axes,
                                         _touch_invert_x,
                                         _touch_invert_y };
    if (_touch_ui_enabled && !calibration.valid()) {
        log_error("ts35 touch_ui_enabled requires valid touch calibration");
        _error = true;
    }
    if (_touch_ui_enabled && _diagnostic_mode) {
        log_error("ts35 touch_ui_enabled and diagnostic_mode are mutually exclusive");
        _error = true;
    }
    if (_jog_segment_mm > _max_jog_distance_mm || _jog_feed_mm_min > _max_jog_feed_mm_min) {
        log_error("ts35 jog profile exceeds configured safety limits");
        _error = true;
    }
    if (_pen_controls_enabled && !_z_is_pen_servo) {
        log_error("ts35 pen_controls_enabled requires z_is_pen_servo");
        _error = true;
    }
}

void TS35Display::init() {
    if (_error) {
        return;
    }

    _diagnostic_started_ms = millis();
    _report_watchdog.reset(_diagnostic_started_ms);
    _client.setJogLimits({ _max_jog_distance_mm, _max_jog_feed_mm_min });
    _touch_ui.setJogProfile(
        _jog_segment_mm, _jog_feed_mm_min, _max_jog_feed_mm_min, static_cast<uint32_t>(_jog_repeat_ms));
    _touch_ui.setZControlMode(_z_is_pen_servo, _pen_controls_enabled);
    initPanel();
    if (_error) {
        return;
    }
    allChannels.registration(this);
    _report_when_idle = true;
    setReportInterval(static_cast<uint32_t>(_report_interval_ms));
    log_info("TS35 channel interval:" << _report_interval_ms << "ms timeout:" << _disconnect_timeout_ms << "ms");
}

size_t TS35Display::write(uint8_t data) {
    if (_protocol.ingest(data, _model)) {
        _report_watchdog.observe(millis());
    }
    return 1;
}

Error TS35Display::pollLine(char* line) {
    const uint32_t now = millis();
    if (_model.snapshot().connected && _report_watchdog.expired(now, static_cast<uint32_t>(_disconnect_timeout_ms))) {
        if (_touch_ui_enabled) {
            _touch_ui.ingest(_touch_tracker.cancel(), now, _model.snapshot(), &_files.snapshot());
            _touch_ui.disconnect(_model.snapshot());
        }
        _model.markDisconnected();
        if (_files.snapshot().loading) {
            _files.abort("Disconnected");
        }
        _receiving_file_list = false;
        _discarding_file_list = false;
    }

    // The base implementation drains commands queued by future touch scenes,
    // processes realtime bytes, and requests interval reports when appropriate.
    Error result = Channel::pollLine(line);
    pollTouchDiagnostic(now);
    if (_touch_ui_enabled) {
        if (static_cast<uint32_t>(now - _last_wifi_status_ms) >= 1000U) {
            _last_wifi_status_ms = now;
            wifi_mode_t mode = WiFi.getMode();
            if (WiFi.status() == WL_CONNECTED) {
                int32_t rssi = WiFi.RSSI();
                int32_t percent = rssi <= -100 ? 0 : (rssi >= -50 ? 100 : 2 * (rssi + 100));
                // Ten-percent buckets prevent RSSI jitter from continually
                // invalidating an otherwise static scanline frame.
                percent = ((percent + 5) / 10) * 10;
                _touch_ui.setWifiStatus(TS35::WifiState::Station, static_cast<uint8_t>(percent));
            } else if (mode == WIFI_AP || mode == WIFI_AP_STA) {
                _touch_ui.setWifiStatus(TS35::WifiState::AccessPoint, 0);
            } else if (mode == WIFI_STA) {
                _touch_ui.setWifiStatus(TS35::WifiState::Connecting, 0);
            } else {
                _touch_ui.setWifiStatus(TS35::WifiState::Off, 0);
            }
        }
        _touch_ui.poll(now, _model.snapshot(), &_files.snapshot());
    }
    renderOneRow();
    return result;
}

bool TS35Display::readTouchRaw(uint16_t& raw_x, uint16_t& raw_y, uint16_t& pressure) {
    // This is the bounded XPT2046 transaction used by the pinned Makerbase
    // TFT_eSPI reference, without its delay-based retry loop. Debouncing and
    // calibration are deliberately deferred until physical raw samples exist.
    _spi->beginTransaction(SPISettings(static_cast<uint32_t>(_touch_frequency_hz), MSBFIRST, SPI_MODE0));
    _cs.on();
    _touch_cs.off();

    int32_t raw_pressure = 0x0fff;
    _spi->transfer(0xb0);
    raw_pressure += static_cast<int32_t>(_spi->transfer16(0xc0) >> 3U);
    raw_pressure -= static_cast<int32_t>(_spi->transfer16(0x00) >> 3U);
    pressure = static_cast<uint16_t>(std::clamp(raw_pressure, 0, 0x0fff));

    _spi->transfer(0x90);
    _spi->transfer(0);
    _spi->transfer(0x90);
    _spi->transfer(0);
    _spi->transfer(0x90);
    _spi->transfer(0);
    _spi->transfer(0x90);
    uint16_t value = static_cast<uint16_t>(_spi->transfer(0)) << 5U;
    value |= static_cast<uint16_t>(_spi->transfer(0x90) >> 3U) & 0x1fU;
    raw_x = value;

    _spi->transfer(0);
    _spi->transfer(0xd0);
    _spi->transfer(0);
    _spi->transfer(0xd0);
    _spi->transfer(0);
    _spi->transfer(0xd0);
    value = static_cast<uint16_t>(_spi->transfer(0)) << 5U;
    value |= static_cast<uint16_t>(_spi->transfer(0) >> 3U) & 0x1fU;
    raw_y = value;

    _touch_cs.on();
    _spi->endTransaction();
    return pressure > static_cast<uint16_t>(_touch_pressure_threshold);
}

void TS35Display::pollTouchDiagnostic(uint32_t now_ms) {
    if ((!_touch_diagnostic_mode && !_touch_ui_enabled) || static_cast<uint32_t>(now_ms - _last_touch_sample_ms) < 20U) {
        return;
    }
    _last_touch_sample_ms = now_ms;

    uint16_t raw_x    = 0;
    uint16_t raw_y    = 0;
    uint16_t pressure = 0;
    bool     pressed  = readTouchRaw(raw_x, raw_y, pressure);

    TS35::TouchCalibration calibration { static_cast<uint16_t>(_touch_calibration_x_min),
                                         static_cast<uint16_t>(_touch_calibration_x_max),
                                         static_cast<uint16_t>(_touch_calibration_y_min),
                                         static_cast<uint16_t>(_touch_calibration_y_max),
                                         _touch_swap_axes,
                                         _touch_invert_x,
                                         _touch_invert_y };
    TS35::ScreenPoint mapped = TS35::mapTouch({ raw_x, raw_y },
                                               calibration,
                                               TS35::StatusView::Width,
                                               TS35::StatusView::Height);
    TS35::TouchEvent event = _touch_tracker.ingest(pressed, mapped);
    if (_touch_ui_enabled) {
        if (pressed) {
            _touch_ui.touchAlive(now_ms);
        }
        _touch_ui.ingest(event, now_ms, _model.snapshot(), &_files.snapshot());
    }
    if (_touch_diagnostic_mode) {
        switch (event.kind) {
            case TS35::TouchEventKind::Press:
                log_info("TS35 touch event press ms:" << now_ms << " x:" << event.point.x << " y:" << event.point.y);
                break;
            case TS35::TouchEventKind::Move:
                log_info("TS35 touch event move ms:" << now_ms << " x:" << event.point.x << " y:" << event.point.y);
                break;
            case TS35::TouchEventKind::Release:
                log_info("TS35 touch event release ms:" << now_ms << " x:" << event.point.x << " y:" << event.point.y);
                break;
            case TS35::TouchEventKind::Cancel:
                log_info("TS35 touch event cancel ms:" << now_ms << " x:" << event.point.x << " y:" << event.point.y);
                break;
            case TS35::TouchEventKind::None:
                break;
        }
    }

    if (_touch_diagnostic_mode && pressed && (!_touch_was_pressed || static_cast<uint32_t>(now_ms - _last_touch_log_ms) >= 100U)) {
        if (mapped.valid) {
            log_info("TS35 touch raw ms:" << now_ms << " x:" << raw_x << " y:" << raw_y << " z:" << pressure << " screen x:" << mapped.x
                                                  << " y:" << mapped.y);
        } else {
            log_info("TS35 touch raw ms:" << now_ms << " x:" << raw_x << " y:" << raw_y << " z:" << pressure
                                           << " screen:uncalibrated");
        }
        _last_touch_log_ms = now_ms;
    } else if (_touch_diagnostic_mode && !pressed && _touch_was_pressed) {
        log_info("TS35 touch released ms:" << now_ms);
    }
    _touch_was_pressed = pressed;
}

void TS35Display::sendCommand(uint8_t command, const uint8_t* data, size_t length) {
    _spi->beginTransaction(SPISettings(static_cast<uint32_t>(_frequency_hz), MSBFIRST, SPI_MODE0));
    _cs.off();
    _dc.off();
    _spi->transfer(command);
    if (length != 0) {
        _dc.on();
        _spi->transferBytes(data, nullptr, length);
    }
    _cs.on();
    _spi->endTransaction();
}

void TS35Display::initPanel() {
    _sck.setAttr(Pin::Attr::Output | Pin::Attr::Exclusive);
    _miso.setAttr(Pin::Attr::Input | Pin::Attr::Exclusive);
    _mosi.setAttr(Pin::Attr::Output | Pin::Attr::Exclusive);
    _cs.setAttr(Pin::Attr::Output | Pin::Attr::Exclusive | Pin::Attr::InitialOn);
    _dc.setAttr(Pin::Attr::Output | Pin::Attr::Exclusive);
    _reset.setAttr(Pin::Attr::Output | Pin::Attr::Exclusive | Pin::Attr::InitialOn);
    _backlight.setAttr(Pin::Attr::Output | Pin::Attr::Exclusive);
    _touch_cs.setAttr(Pin::Attr::Output | Pin::Attr::Exclusive | Pin::Attr::InitialOn);

    _backlight.off();
    _touch_cs.on();
    _cs.on();

    _spi = new SPIClass(VSPI);
    _spi->begin(_sck.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native),
                _miso.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native),
                _mosi.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native),
                -1);

    _reset.off();
    delay(20);
    _reset.on();
    delay(120);

    sendCommand(0x11);  // Sleep out
    delay(20);

    const uint8_t enable1  = 0xc3;
    const uint8_t enable2  = 0x96;
    const uint8_t madctl   = 0x28;  // Landscape, BGR order (stock setRotation(1))
    const uint8_t format   = 0x55;  // RGB565
    const uint8_t inv      = 0x01;
    const uint8_t entry    = 0xc6;
    const uint8_t adjust[] = { 0x40, 0x8a, 0x00, 0x00, 0x29, 0x19, 0xa5, 0x33 };
    const uint8_t power2   = 0x06;
    const uint8_t power3   = 0xa7;
    const uint8_t vcom     = 0x18;
    const uint8_t disable1 = 0x3c;
    const uint8_t disable2 = 0x69;

    sendCommand(0xf0, &enable1, 1);
    sendCommand(0xf0, &enable2, 1);
    sendCommand(0x36, &madctl, 1);
    sendCommand(0x3a, &format, 1);
    sendCommand(0xb4, &inv, 1);
    sendCommand(0xb7, &entry, 1);
    sendCommand(0xe8, adjust, sizeof(adjust));
    sendCommand(0xc1, &power2, 1);
    sendCommand(0xc2, &power3, 1);
    sendCommand(0xc5, &vcom, 1);
    sendCommand(0xe0, PositiveGamma, sizeof(PositiveGamma));
    sendCommand(0xe1, NegativeGamma, sizeof(NegativeGamma));
    sendCommand(0xf0, &disable1, 1);
    sendCommand(0xf0, &disable2, 1);
    delay(120);
    sendCommand(0x29);  // Display on; backlight remains off until first frame.
}

void TS35Display::renderOneRow() {
    const auto& snapshot = _model.snapshot();
    if (_diagnostic_mode) {
        if (_next_row >= TS35::StatusView::Height) {
            uint32_t now = millis();
            if (static_cast<uint32_t>(now - _diagnostic_started_ms) < static_cast<uint32_t>(_diagnostic_card_ms)) {
                return;
            }
            _diagnostic_card       = (_diagnostic_card + 1U) % TS35::DiagnosticCardCount;
            _diagnostic_started_ms = now;
            _next_row              = 0;
            // Exercise the configured backlight polarity on every card and
            // keep the row-by-row replacement hidden until it is coherent.
            _backlight.off();
        }
    } else {
        // Finish the frame already in progress before adopting a newer snapshot.
        // Otherwise rapid reports could continually restart at row zero and never
        // expose a complete, coherent frame.
        const uint64_t ui_generation   = _touch_ui.state().generation;
        const uint64_t file_generation = _files.snapshot().generation;
        if (_rendered_generation == UINT64_MAX) {
            _view                     = TS35::composeStatusView(snapshot);
            _ui_view                  = _touch_ui.state();
            _files_view               = _files.snapshot();
            _rendered_generation      = snapshot.generation;
            _rendered_ui_generation   = ui_generation;
            _rendered_file_generation = file_generation;
            _next_row                 = 0;
        } else if (_next_row >= TS35::StatusView::Height) {
            bool redraw = false;
            if (_rendered_generation != snapshot.generation) {
                TS35::StatusView next_view = TS35::composeStatusView(snapshot);
                _rendered_generation       = snapshot.generation;
                if (next_view != _view) {
                    _view   = std::move(next_view);
                    redraw = true;
                }
            }
            if (_touch_ui_enabled && _rendered_ui_generation != ui_generation) {
                _ui_view                = _touch_ui.state();
                _rendered_ui_generation = ui_generation;
                redraw                  = true;
            }
            if (_touch_ui_enabled && _rendered_file_generation != file_generation) {
                _files_view                = _files.snapshot();
                _rendered_file_generation  = file_generation;
                redraw                     = true;
            }
            if (redraw) {
                _next_row = 0;
            } else {
                return;
            }
        }
    }

    const uint32_t row_started_us = micros();
    if (_next_row == 0) {
        _frame_started_us = row_started_us;
        _frame_max_row_us = 0;
    }

    bool rendered = _diagnostic_mode
                        ? TS35::renderDiagnosticScanline(_diagnostic_card, _next_row, _scanline.data(), _scanline.size())
                    : _touch_ui_enabled
                        ? TS35::renderTouchUiScanline(
                              _view, _ui_view, _next_row, _scanline.data(), _scanline.size(), &_files_view)
                        : TS35::renderStatusScanline(_view, _next_row, _scanline.data(), _scanline.size());
    if (_next_row >= TS35::StatusView::Height || !rendered) {
        return;
    }

    uint8_t column[] = { 0x00, 0x00, 0x01, 0xdf };  // 0..479
    uint8_t row[]    = { static_cast<uint8_t>(_next_row >> 8),
                         static_cast<uint8_t>(_next_row),
                         static_cast<uint8_t>(_next_row >> 8),
                         static_cast<uint8_t>(_next_row) };
    sendCommand(0x2a, column, sizeof(column));
    sendCommand(0x2b, row, sizeof(row));

    // SPI transmits bytes in memory order; RGB565 values are native little
    // endian on ESP32, so swap each word before the wire transfer.
    for (auto& pixel : _scanline) {
        pixel = static_cast<uint16_t>((pixel << 8U) | (pixel >> 8U));
    }

    _spi->beginTransaction(SPISettings(static_cast<uint32_t>(_frequency_hz), MSBFIRST, SPI_MODE0));
    _cs.off();
    _dc.off();
    _spi->transfer(0x2c);  // Memory write
    _dc.on();
    _spi->transferBytes(reinterpret_cast<uint8_t*>(_scanline.data()), nullptr, sizeof(_scanline));
    _cs.on();
    _spi->endTransaction();

    const uint32_t row_elapsed_us = static_cast<uint32_t>(micros() - row_started_us);
    _frame_max_row_us             = std::max(_frame_max_row_us, row_elapsed_us);
    ++_next_row;
    if (_next_row == TS35::StatusView::Height) {
        _backlight.on();
        const uint32_t frame_elapsed_us = static_cast<uint32_t>(micros() - _frame_started_us);
        if (_diagnostic_mode) {
            log_info("TS35 diagnostic card:" << _diagnostic_card << " frame_us:" << frame_elapsed_us
                                              << " row_max_us:" << _frame_max_row_us << " heap_free:" << ESP.getFreeHeap()
                                              << " heap_min:" << ESP.getMinFreeHeap()
                                              << " stack_hwm:" << uxTaskGetStackHighWaterMark(nullptr));
        } else if (_touch_ui_enabled && (++_ui_frame_count == 1 || _ui_frame_count % 16 == 0)) {
            log_info("TS35 ui frame:" << _ui_frame_count << " frame_us:" << frame_elapsed_us
                                       << " row_max_us:" << _frame_max_row_us << " heap_free:" << ESP.getFreeHeap()
                                       << " heap_min:" << ESP.getMinFreeHeap()
                                       << " stack_hwm:" << uxTaskGetStackHighWaterMark(nullptr));
        }
    }
}

void TS35Display::flushRx() {
    Channel::flushRx();
    _protocol.reset();
    _report_watchdog.reset(millis());
    if (_touch_ui_enabled) {
        _touch_ui.ingest(_touch_tracker.cancel(), millis(), _model.snapshot(), &_files.snapshot());
        _touch_ui.disconnect(_model.snapshot());
    }
    _model.markDisconnected();
    if (_files.snapshot().loading) {
        _files.abort("Reset");
    }
    _receiving_file_list = false;
    _discarding_file_list = false;
}

void TS35Display::beginJSON(const char* json_tag) {
    if (json_tag != nullptr && std::strcmp(json_tag, "FilesList") == 0) {
        _files.begin();
        _receiving_file_list = true;
        _discarding_file_list = false;
    }
}

void TS35Display::out_acked(const std::string& data, const char* tag) {
    if ((_receiving_file_list || _discarding_file_list) && tag != nullptr && std::strcmp(tag, "JSON:") == 0) {
        if (_receiving_file_list && !_files.feed(data)) {
            _files.abort("Malformed file-list response");
            _receiving_file_list = false;
            _discarding_file_list = true;
        }
        return;
    }
    Channel::out_acked(data, tag);
}

void TS35Display::endJSON(const char* json_tag) {
    if ((_receiving_file_list || _discarding_file_list) && json_tag != nullptr && std::strcmp(json_tag, "FilesList") == 0) {
        if (_receiving_file_list) {
            _files.finish();
        }
        _receiving_file_list = false;
        _discarding_file_list = false;
    }
}

bool TS35Display::sendRealtime(uint8_t command) {
    if (rx_buffer_available() < 1) {
        return false;
    }
    push(command);
    return true;
}

bool TS35Display::sendLine(std::string_view command) {
    if (command.size() + 1U > static_cast<size_t>(rx_buffer_available())) {
        return false;
    }
    push(command);
    push(static_cast<uint8_t>('\n'));
    return true;
}

namespace {
    ConfigurableModuleFactory::InstanceBuilder<TS35Display> ts35_module __attribute__((init_priority(104))) ("ts35");
}

#endif  // CONFIG_IDF_TARGET_ESP32
