// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>

namespace TS35 {

    // Tracks the age of the last accepted status report. Unsigned subtraction
    // deliberately makes the elapsed-time test safe across millis() wrap.
    class ReportWatchdog {
    public:
        void reset(uint32_t now_ms) {
            _last_report_ms = now_ms;
            _seen_report    = false;
        }

        void observe(uint32_t now_ms) {
            _last_report_ms = now_ms;
            _seen_report    = true;
        }

        bool expired(uint32_t now_ms, uint32_t timeout_ms) const {
            return _seen_report && static_cast<uint32_t>(now_ms - _last_report_ms) > timeout_ms;
        }

        bool     seenReport() const { return _seen_report; }
        uint32_t lastReportMs() const { return _last_report_ms; }

    private:
        uint32_t _last_report_ms = 0;
        bool     _seen_report    = false;
    };

}  // namespace TS35
