// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "MachineModel.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace TS35 {

    class ProtocolStream {
    public:
        // Returns true only when this byte completes a line accepted by the model.
        bool ingest(uint8_t byte, MachineModel& model);
        void reset();

        size_t   bufferedBytes() const { return _length; }
        bool     discarding() const { return _discarding; }
        uint32_t droppedLines() const { return _dropped_lines; }
        uint32_t malformedLines() const { return _malformed_lines; }

    private:
        std::array<char, MaxReportSize + 1> _line {};
        size_t                              _length          = 0;
        bool                                _discarding      = false;
        bool                                _last_was_cr     = false;
        uint32_t                            _dropped_lines   = 0;
        uint32_t                            _malformed_lines = 0;
    };

}  // namespace TS35
