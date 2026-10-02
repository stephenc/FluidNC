// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TouchTracker.h"

#include <algorithm>
#include <cstdlib>

namespace TS35 {

    TouchTracker::TouchTracker(uint8_t press_samples, uint8_t release_samples, int16_t move_threshold, int16_t spike_threshold) :
        _press_samples(std::max<uint8_t>(1, press_samples)),
        _release_samples(std::max<uint8_t>(1, release_samples)),
        _move_threshold(std::max<int16_t>(1, move_threshold)),
        _spike_threshold(std::max<int16_t>(_move_threshold, spike_threshold)) {}

    TouchEvent TouchTracker::ingest(bool pressed, ScreenPoint point) {
        pressed = pressed && point.valid;
        if (!_active) {
            _release_count = 0;
            if (!pressed) {
                _press_count = 0;
                _candidate_valid = false;
                return {};
            }
            if (_press_count != 0 &&
                (std::abs(point.x - _last.x) > _spike_threshold || std::abs(point.y - _last.y) > _spike_threshold)) {
                _press_count = 0;
            }
            _last = point;
            if (++_press_count < _press_samples) {
                return {};
            }
            _press_count = 0;
            _active      = true;
            return { TouchEventKind::Press, _last };
        }

        _press_count = 0;
        if (!pressed) {
            if (++_release_count < _release_samples) {
                return {};
            }
            _release_count = 0;
            _active        = false;
            _candidate_valid = false;
            return { TouchEventKind::Release, _last };
        }

        _release_count = 0;
        if (std::abs(point.x - _last.x) > _spike_threshold || std::abs(point.y - _last.y) > _spike_threshold) {
            if (!_candidate_valid || std::abs(point.x - _candidate.x) > _spike_threshold ||
                std::abs(point.y - _candidate.y) > _spike_threshold) {
                _candidate       = point;
                _candidate_valid = true;
                return {};
            }
        }
        _candidate_valid = false;
        if (std::abs(point.x - _last.x) < _move_threshold && std::abs(point.y - _last.y) < _move_threshold) {
            return {};
        }
        _last = point;
        return { TouchEventKind::Move, _last };
    }

    TouchEvent TouchTracker::cancel() {
        _press_count   = 0;
        _release_count = 0;
        _candidate_valid = false;
        if (!_active) {
            return {};
        }
        _active = false;
        return { TouchEventKind::Cancel, _last };
    }

}  // namespace TS35
