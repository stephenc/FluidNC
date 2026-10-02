// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "StatusPresenter.h"
#include "TouchUi.h"

#include <cstddef>
#include <cstdint>

namespace TS35 {

    // Render one RGB565 scanline of the fixed 480x320 status scene. This is the
    // common host/embedded renderer and deliberately requires no framebuffer.
    // Returns false for an invalid row or undersized destination.
    bool renderStatusScanline(const StatusView& view, int y, uint16_t* pixels, size_t pixel_count);

    // Renders the interactive status/jog shell and confirmation overlay while
    // retaining the scanline-only memory bound.
    bool renderTouchUiScanline(const StatusView& view,
                               const TouchUiState& ui,
                               int y,
                               uint16_t* pixels,
                               size_t pixel_count,
                               const FileListSnapshot* files = nullptr);

}  // namespace TS35
