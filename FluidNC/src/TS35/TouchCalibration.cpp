// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TouchCalibration.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace TS35 {
    namespace {

        int32_t average(int32_t first, int32_t second) {
            return (first + second) / 2;
        }

        int32_t axisX(const RawTouchPoint& point, bool swap_axes) {
            return swap_axes ? point.y : point.x;
        }

        int32_t axisY(const RawTouchPoint& point, bool swap_axes) {
            return swap_axes ? point.x : point.y;
        }

        uint16_t bounded(int32_t value) {
            return static_cast<uint16_t>(std::clamp<int32_t>(value, 0, 4095));
        }

        int16_t scale(int32_t value, uint16_t low, uint16_t high, int16_t extent, bool invert) {
            int32_t clamped = std::clamp<int32_t>(value, low, high);
            int32_t result  = ((clamped - low) * static_cast<int32_t>(extent - 1) + (high - low) / 2) / (high - low);
            if (invert) {
                result = extent - 1 - result;
            }
            return static_cast<int16_t>(result);
        }

    }  // namespace

    bool TouchCalibration::valid(uint16_t minimum_span) const {
        return raw_x_max > raw_x_min && raw_y_max > raw_y_min && raw_x_max - raw_x_min >= minimum_span &&
               raw_y_max - raw_y_min >= minimum_span;
    }

    bool deriveTouchCalibration(const TouchCorners& corners, TouchCalibration& result, uint16_t minimum_span) {
        int32_t horizontal_x = average(corners.top_right.x - corners.top_left.x,
                                       corners.bottom_right.x - corners.bottom_left.x);
        int32_t horizontal_y = average(corners.top_right.y - corners.top_left.y,
                                       corners.bottom_right.y - corners.bottom_left.y);
        int32_t vertical_x = average(corners.bottom_left.x - corners.top_left.x,
                                     corners.bottom_right.x - corners.top_right.x);
        int32_t vertical_y = average(corners.bottom_left.y - corners.top_left.y,
                                     corners.bottom_right.y - corners.top_right.y);

        bool swap_axes = std::abs(horizontal_y) > std::abs(horizontal_x);
        int32_t horizontal_primary = swap_axes ? horizontal_y : horizontal_x;
        int32_t horizontal_cross   = swap_axes ? horizontal_x : horizontal_y;
        int32_t vertical_primary   = swap_axes ? vertical_x : vertical_y;
        int32_t vertical_cross     = swap_axes ? vertical_y : vertical_x;

        // A gesture set that changes mostly along the wrong axis is ambiguous,
        // as is an axis whose endpoints are too close to calibrate safely.
        if (std::abs(horizontal_primary) < minimum_span || std::abs(vertical_primary) < minimum_span ||
            std::abs(horizontal_cross) * 2 > std::abs(horizontal_primary) ||
            std::abs(vertical_cross) * 2 > std::abs(vertical_primary)) {
            result = {};
            return false;
        }

        int32_t left   = average(axisX(corners.top_left, swap_axes), axisX(corners.bottom_left, swap_axes));
        int32_t right  = average(axisX(corners.top_right, swap_axes), axisX(corners.bottom_right, swap_axes));
        int32_t top    = average(axisY(corners.top_left, swap_axes), axisY(corners.top_right, swap_axes));
        int32_t bottom = average(axisY(corners.bottom_left, swap_axes), axisY(corners.bottom_right, swap_axes));

        TouchCalibration candidate;
        candidate.raw_x_min = bounded(std::min(left, right));
        candidate.raw_x_max = bounded(std::max(left, right));
        candidate.raw_y_min = bounded(std::min(top, bottom));
        candidate.raw_y_max = bounded(std::max(top, bottom));
        candidate.swap_axes = swap_axes;
        candidate.invert_x  = right < left;
        candidate.invert_y  = bottom < top;
        if (!candidate.valid(minimum_span)) {
            result = {};
            return false;
        }
        result = candidate;
        return true;
    }

    ScreenPoint mapTouch(const RawTouchPoint& raw, const TouchCalibration& calibration, int16_t width, int16_t height) {
        if (!calibration.valid() || width <= 0 || height <= 0) {
            return {};
        }
        return { scale(axisX(raw, calibration.swap_axes),
                       calibration.raw_x_min,
                       calibration.raw_x_max,
                       width,
                       calibration.invert_x),
                 scale(axisY(raw, calibration.swap_axes),
                       calibration.raw_y_min,
                       calibration.raw_y_max,
                       height,
                       calibration.invert_y),
                 true };
    }

}  // namespace TS35
