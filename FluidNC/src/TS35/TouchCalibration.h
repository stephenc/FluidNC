// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>

namespace TS35 {

    struct RawTouchPoint {
        uint16_t x = 0;
        uint16_t y = 0;
    };

    struct ScreenPoint {
        int16_t x     = 0;
        int16_t y     = 0;
        bool    valid = false;
    };

    struct TouchCorners {
        RawTouchPoint top_left;
        RawTouchPoint top_right;
        RawTouchPoint bottom_left;
        RawTouchPoint bottom_right;
    };

    // raw_x_* and raw_y_* refer to the raw channels selected for the screen X
    // and Y axes after swap_axes is applied.
    struct TouchCalibration {
        uint16_t raw_x_min = 0;
        uint16_t raw_x_max = 0;
        uint16_t raw_y_min = 0;
        uint16_t raw_y_max = 0;
        bool     swap_axes  = false;
        bool     invert_x   = false;
        bool     invert_y   = false;

        bool valid(uint16_t minimum_span = 200) const;
    };

    // Derives axis selection, direction and end points from four named corner
    // touches. It fails closed when the gestures do not describe two distinct,
    // approximately orthogonal axes with sufficient ADC span.
    bool deriveTouchCalibration(const TouchCorners& corners, TouchCalibration& result, uint16_t minimum_span = 200);

    // Maps into inclusive screen bounds and clamps edge pressure scatter. An
    // invalid calibration never produces a usable point.
    ScreenPoint mapTouch(const RawTouchPoint& raw, const TouchCalibration& calibration, int16_t width, int16_t height);

}  // namespace TS35
