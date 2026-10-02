// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "StatusPresenter.h"

#include <cstddef>
#include <cstdint>

namespace TS35 {

    constexpr unsigned DiagnosticCardCount = 6;

    // Deterministic electrical/visual proof cards. Like the production
    // renderer, this writes exactly one RGB565 row and needs no framebuffer.
    bool renderDiagnosticScanline(unsigned card, int y, uint16_t* pixels, size_t pixel_count);

}  // namespace TS35
