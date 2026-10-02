// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TouchCalibration.h"

#include <cstdint>

namespace TS35 {

    enum class TouchEventKind : uint8_t {
        None,
        Press,
        Move,
        Release,
        Cancel,
    };

    struct TouchEvent {
        TouchEventKind kind = TouchEventKind::None;
        ScreenPoint    point;
    };

    // Converts noisy sampled contact into one press, bounded moves and one
    // release. Invalid coordinates fail closed as a released sample.
    class TouchTracker {
    public:
        explicit TouchTracker(uint8_t press_samples = 2,
                              uint8_t release_samples = 6,
                              int16_t move_threshold = 3,
                              int16_t spike_threshold = 80);

        TouchEvent ingest(bool pressed, ScreenPoint point);
        TouchEvent cancel();
        bool       active() const { return _active; }

    private:
        uint8_t _press_samples;
        uint8_t _release_samples;
        int16_t _move_threshold;
        int16_t _spike_threshold;
        uint8_t _press_count   = 0;
        uint8_t _release_count = 0;
        bool    _active        = false;
        bool    _candidate_valid = false;
        ScreenPoint _last;
        ScreenPoint _candidate;
    };

}  // namespace TS35
