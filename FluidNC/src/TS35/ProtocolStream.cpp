// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ProtocolStream.h"

#include <string_view>

namespace TS35 {

    bool ProtocolStream::ingest(uint8_t byte, MachineModel& model) {
        bool newline = byte == '\r' || byte == '\n';
        if (byte == '\n' && _last_was_cr) {
            _last_was_cr = false;
            return false;
        }
        _last_was_cr = byte == '\r';

        if (_discarding) {
            if (newline) {
                _discarding = false;
                _length     = 0;
                ++_dropped_lines;
            }
            return false;
        }

        if (newline) {
            if (_length == 0) {
                return false;
            }
            bool accepted = model.ingestLine(std::string_view(_line.data(), _length));
            _length       = 0;
            if (!accepted) {
                ++_malformed_lines;
            }
            return accepted;
        }

        if (_length == MaxReportSize) {
            _discarding = true;
            _length     = 0;
            return false;
        }

        _line[_length++] = static_cast<char>(byte);
        return false;
    }

    void ProtocolStream::reset() {
        _length      = 0;
        _discarding  = false;
        _last_was_cr = false;
    }

}  // namespace TS35
