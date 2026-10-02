// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "StatusRasterizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>

namespace TS35 {
    namespace {

        constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
            return static_cast<uint16_t>(((red & 0xf8U) << 8U) | ((green & 0xfcU) << 3U) | (blue >> 3U));
        }

        constexpr uint16_t Background = rgb565(10, 18, 32);
        constexpr uint16_t Panel      = rgb565(22, 34, 54);
        constexpr uint16_t Muted      = rgb565(145, 164, 188);
        constexpr uint16_t White      = rgb565(240, 246, 255);
        constexpr uint16_t Cyan       = rgb565(52, 211, 235);
        constexpr uint16_t WifiCross  = rgb565(244, 63, 94);

        uint16_t toneColor(StatusTone tone) {
            switch (tone) {
                case StatusTone::Disconnected:
                    return rgb565(71, 85, 105);
                case StatusTone::Ready:
                    return rgb565(5, 150, 105);
                case StatusTone::Active:
                    return rgb565(3, 105, 161);
                case StatusTone::Paused:
                    return rgb565(202, 138, 4);
                case StatusTone::Warning:
                    return rgb565(190, 24, 55);
            }
            return rgb565(71, 85, 105);
        }

        // Five-column glyphs from the glcdfont table distributed with Adafruit
        // GFX (see /THIRD_PARTY_NOTICES.md). Text is uppercased to keep this subset small.
        std::array<uint8_t, 5> glyph(char input) {
            char c = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
            switch (c) {
                case ' ':
                    return { 0, 0, 0, 0, 0 };
                case '!':
                    return { 0, 0, 0x5f, 0, 0 };
                case '%':
                    return { 0x23, 0x13, 0x08, 0x64, 0x62 };
                case '(':
                    return { 0, 0x1c, 0x22, 0x41, 0 };
                case ')':
                    return { 0, 0x41, 0x22, 0x1c, 0 };
                case '+':
                    return { 0x08, 0x08, 0x3e, 0x08, 0x08 };
                case ',':
                    return { 0, 0x50, 0x30, 0, 0 };
                case '-':
                    return { 0x08, 0x08, 0x08, 0x08, 0x08 };
                case '.':
                    return { 0, 0x60, 0x60, 0, 0 };
                case '/':
                    return { 0x20, 0x10, 0x08, 0x04, 0x02 };
                case '0':
                    return { 0x3e, 0x51, 0x49, 0x45, 0x3e };
                case '1':
                    return { 0, 0x42, 0x7f, 0x40, 0 };
                case '2':
                    return { 0x42, 0x61, 0x51, 0x49, 0x46 };
                case '3':
                    return { 0x21, 0x41, 0x45, 0x4b, 0x31 };
                case '4':
                    return { 0x18, 0x14, 0x12, 0x7f, 0x10 };
                case '5':
                    return { 0x27, 0x45, 0x45, 0x45, 0x39 };
                case '6':
                    return { 0x3c, 0x4a, 0x49, 0x49, 0x30 };
                case '7':
                    return { 0x01, 0x71, 0x09, 0x05, 0x03 };
                case '8':
                    return { 0x36, 0x49, 0x49, 0x49, 0x36 };
                case '9':
                    return { 0x06, 0x49, 0x49, 0x29, 0x1e };
                case ':':
                    return { 0, 0x36, 0x36, 0, 0 };
                case 'A':
                    return { 0x7e, 0x11, 0x11, 0x11, 0x7e };
                case 'B':
                    return { 0x7f, 0x49, 0x49, 0x49, 0x36 };
                case 'C':
                    return { 0x3e, 0x41, 0x41, 0x41, 0x22 };
                case 'D':
                    return { 0x7f, 0x41, 0x41, 0x22, 0x1c };
                case 'E':
                    return { 0x7f, 0x49, 0x49, 0x49, 0x41 };
                case 'F':
                    return { 0x7f, 0x09, 0x09, 0x09, 0x01 };
                case 'G':
                    return { 0x3e, 0x41, 0x49, 0x49, 0x7a };
                case 'H':
                    return { 0x7f, 0x08, 0x08, 0x08, 0x7f };
                case 'I':
                    return { 0, 0x41, 0x7f, 0x41, 0 };
                case 'J':
                    return { 0x20, 0x40, 0x41, 0x3f, 0x01 };
                case 'K':
                    return { 0x7f, 0x08, 0x14, 0x22, 0x41 };
                case 'L':
                    return { 0x7f, 0x40, 0x40, 0x40, 0x40 };
                case 'M':
                    return { 0x7f, 0x02, 0x0c, 0x02, 0x7f };
                case 'N':
                    return { 0x7f, 0x04, 0x08, 0x10, 0x7f };
                case 'O':
                    return { 0x3e, 0x41, 0x41, 0x41, 0x3e };
                case 'P':
                    return { 0x7f, 0x09, 0x09, 0x09, 0x06 };
                case 'Q':
                    return { 0x3e, 0x41, 0x51, 0x21, 0x5e };
                case 'R':
                    return { 0x7f, 0x09, 0x19, 0x29, 0x46 };
                case 'S':
                    return { 0x46, 0x49, 0x49, 0x49, 0x31 };
                case 'T':
                    return { 0x01, 0x01, 0x7f, 0x01, 0x01 };
                case 'U':
                    return { 0x3f, 0x40, 0x40, 0x40, 0x3f };
                case 'V':
                    return { 0x1f, 0x20, 0x40, 0x20, 0x1f };
                case 'W':
                    return { 0x3f, 0x40, 0x38, 0x40, 0x3f };
                case 'X':
                    return { 0x63, 0x14, 0x08, 0x14, 0x63 };
                case 'Y':
                    return { 0x07, 0x08, 0x70, 0x08, 0x07 };
                case 'Z':
                    return { 0x61, 0x51, 0x49, 0x45, 0x43 };
                default:
                    return { 0x02, 0x01, 0x51, 0x09, 0x06 };
            }
        }

        void span(uint16_t* pixels, int x, int width, uint16_t color) {
            int first = std::max(0, x);
            int last  = std::min(StatusView::Width, x + width);
            if (first < last) {
                std::fill(pixels + first, pixels + last, color);
            }
        }

        void rectangleRow(uint16_t* pixels, int y, int x, int top, int width, int height, uint16_t color) {
            if (y >= top && y < top + height) {
                span(pixels, x, width, color);
            }
        }

        void textRow(uint16_t* pixels, int y, int x, int top, std::string_view text, int scale, uint16_t color) {
            int glyph_row = (y - top) / scale;
            if (y < top || glyph_row < 0 || glyph_row >= 7) {
                return;
            }
            for (char c : text) {
                auto columns = glyph(c);
                for (int column = 0; column < 5; ++column) {
                    if (columns[column] & (1U << glyph_row)) {
                        span(pixels, x + column * scale, scale, color);
                    }
                }
                x += 6 * scale;
                if (x >= StatusView::Width - 5 * scale) {
                    break;
                }
            }
        }

        void wrappedTextRows(uint16_t* pixels,
                             int y,
                             int x,
                             int top,
                             std::string_view text,
                             int scale,
                             uint16_t color,
                             int max_width,
                             size_t max_lines,
                             int line_height) {
            const size_t max_chars = max_width > 0 && scale > 0
                                         ? static_cast<size_t>(max_width / (6 * scale))
                                         : 0;
            if (max_chars == 0) {
                return;
            }
            size_t offset = 0;
            for (size_t line = 0; line < max_lines && offset < text.size(); ++line) {
                while (offset < text.size() && text[offset] == ' ') {
                    ++offset;
                }
                if (offset >= text.size()) {
                    break;
                }

                size_t remaining = text.size() - offset;
                size_t count = std::min(max_chars, remaining);
                bool more = count < remaining;
                if (more) {
                    size_t break_at = text.substr(offset, count + 1).find_last_of(' ');
                    if (break_at != std::string_view::npos && break_at != 0) {
                        count = break_at;
                    }
                }

                bool last_truncated_line = line + 1 == max_lines && offset + count < text.size();
                if (last_truncated_line && max_chars >= 3) {
                    size_t visible = std::min(count, max_chars - 3);
                    textRow(pixels, y, x, top + static_cast<int>(line) * line_height,
                            text.substr(offset, visible), scale, color);
                    textRow(pixels, y, x + static_cast<int>(visible) * 6 * scale,
                            top + static_cast<int>(line) * line_height, "...", scale, color);
                    break;
                }

                textRow(pixels, y, x, top + static_cast<int>(line) * line_height,
                        text.substr(offset, count), scale, color);
                offset += count;
            }
        }

        void wifiStatusRow(uint16_t* pixels, int y, const TouchUiState& ui, int x, int top) {
            // A compact conventional Wi-Fi mark: three semicircular rings over
            // a dot. Station mode lights rings according to RSSI, AP mode is
            // full strength with a small badge, and every non-connected state
            // gets an unmistakable red X.
            constexpr std::array<int, 3> Radius { 6, 11, 17 };
            constexpr int                CenterX = 18;
            constexpr int                CenterY = 25;
            bool                         station = ui.wifi_state == WifiState::Station;
            bool                         access_point = ui.wifi_state == WifiState::AccessPoint;
            bool                         disconnected = !station && !access_point;
            unsigned active_rings = access_point ? 3U
                                    : (station && ui.wifi_signal_percent > 0
                                           ? std::min(3U, (static_cast<unsigned>(ui.wifi_signal_percent) + 32U) / 33U)
                                           : 0U);

            int local_y = y - top;
            if (local_y >= 0 && local_y <= CenterY) {
                int dy = local_y - CenterY;
                for (int local_x = 0; local_x <= CenterX * 2; ++local_x) {
                    int dx = local_x - CenterX;
                    uint16_t color = 0;
                    for (size_t ring = 0; ring < Radius.size(); ++ring) {
                        int inner = Radius[ring] - 1;
                        int outer = Radius[ring] + 1;
                        int distance_squared = dx * dx + dy * dy;
                        if (distance_squared >= inner * inner && distance_squared <= outer * outer) {
                            color = ring < active_rings ? White : Muted;
                        }
                    }
                    if (dx * dx + dy * dy <= 5) {
                        color = station || access_point ? White : Muted;
                    }
                    if (color != 0) {
                        pixels[x + local_x] = color;
                    }
                }
            }

            if (disconnected && local_y >= 4 && local_y <= 24) {
                int offset = (local_y - 4) * 26 / 20;
                span(pixels, x + 5 + offset, 3, WifiCross);
                span(pixels, x + 31 - offset, 3, WifiCross);
            }
            if (access_point) {
                rectangleRow(pixels, y, x + 22, top, 15, 10, Panel);
                textRow(pixels, y, x + 24, top + 2, "AP", 1, Cyan);
            }
        }

        void jogPlaneAxes(JogPlane plane, size_t& horizontal, size_t& vertical) {
            switch (plane) {
                case JogPlane::XY:
                    horizontal = 0;
                    vertical = 1;
                    break;
                case JogPlane::XZ:
                    horizontal = 0;
                    vertical = 2;
                    break;
                case JogPlane::YZ:
                    horizontal = 1;
                    vertical = 2;
                    break;
            }
        }

    }  // namespace

    bool renderStatusScanline(const StatusView& view, int y, uint16_t* pixels, size_t pixel_count) {
        if (pixels == nullptr || pixel_count < StatusView::Width || y < 0 || y >= StatusView::Height) {
            return false;
        }

        std::fill(pixels, pixels + StatusView::Width, Background);
        rectangleRow(pixels, y, 0, 0, StatusView::Width, 54, toneColor(view.tone));
        textRow(pixels, y, 18, 13, view.state, 4, White);
        if (!view.alarm_text.empty()) {
            textRow(pixels, y, 285, 18, view.alarm_text, 2, White);
        }

        textRow(pixels, y, 18, 68, view.position_caption, 2, Muted);
        constexpr int card_x[] = { 16, 173, 330 };
        for (size_t axis = 0; axis < 3; ++axis) {
            rectangleRow(pixels, y, card_x[axis], 92, 140, 86, Panel);
            textRow(pixels, y, card_x[axis] + 12, 105, view.axis_labels[axis], 3, Cyan);
            textRow(pixels, y, card_x[axis] + 12, 143, view.axis_values[axis], 2, White);
        }

        rectangleRow(pixels, y, 16, 190, 448, 114, Panel);
        textRow(pixels, y, 28, 202, view.limit_text, 2, White);
        textRow(pixels, y, 28, 228, view.feed_text, 2, Muted);
        textRow(pixels, y, 28, 254, view.tool_text, 2, Muted);
        textRow(pixels, y, 28, 280, view.job_text, 2, Cyan);
        return true;
    }

    bool renderTouchUiScanline(const StatusView& view,
                               const TouchUiState& ui,
                               int y,
                               uint16_t* pixels,
                               size_t pixel_count,
                               const FileListSnapshot* files) {
        if (!renderStatusScanline(view, y, pixels, pixel_count)) {
            return false;
        }

        constexpr uint16_t Button       = rgb565(30, 64, 96);
        constexpr uint16_t SafeButton   = rgb565(5, 120, 105);
        constexpr uint16_t Caution      = rgb565(180, 120, 4);
        constexpr uint16_t HeldButton   = rgb565(234, 88, 12);
        constexpr uint16_t Danger       = rgb565(170, 30, 55);
        constexpr uint16_t Overlay      = rgb565(14, 24, 42);

        if (ui.page == UiPage::Status) {
            bool held = view.state == "Hold" || view.state.rfind("Hold:", 0) == 0;
            if (view.alarm_text.empty()) {
                wifiStatusRow(pixels, y, ui, 430, 10);
            }
            rectangleRow(pixels, y, 0, 246, StatusView::Width, 74, Background);
            rectangleRow(pixels, y, 4, 250, 88, 66, Button);
            rectangleRow(pixels, y, 100, 250, 88, 66, Button);
            rectangleRow(pixels, y, 196, 250, 88, 66, held ? HeldButton : Caution);
            rectangleRow(pixels, y, 292, 250, 88, 66, SafeButton);
            rectangleRow(pixels, y, 388, 250, 88, 66, Danger);
            textRow(pixels, y, 26, 272, "JOG", 2, White);
            textRow(pixels, y, 108, 272, "FILES", 2, White);
            textRow(pixels, y, 210, 272, held ? "HELD" : "HOLD", 2, White);
            textRow(pixels, y, 306, 272, "HOME", 2, White);
            textRow(pixels, y, 394, 272, "RESET", 2, White);
        } else if (ui.page == UiPage::Jog) {
            std::fill(pixels, pixels + StatusView::Width, Background);
            rectangleRow(pixels, y, 0, 0, StatusView::Width, 56, toneColor(view.tone));
            rectangleRow(pixels, y, 4, 4, 92, 48, Button);
            rectangleRow(pixels, y, 108, 4, 242, 48, Button);
            textRow(pixels, y, 17, 18, "BACK", 3, White);
            const char* plane_label = ui.jog_plane == JogPlane::XY ? "PLANE XY"
                                      : (ui.jog_plane == JogPlane::XZ ? "PLANE XZ" : "PLANE YZ");
            textRow(pixels, y, 154, 18, plane_label, 3, White);
            wifiStatusRow(pixels, y, ui, 430, 10);

            size_t horizontal = 0;
            size_t vertical = 1;
            jogPlaneAxes(ui.jog_plane, horizontal, vertical);
            textRow(pixels, y, 12, 66, view.axis_labels[horizontal], 2, Cyan);
            textRow(pixels, y, 12, 88, view.axis_values[horizontal], 2, White);
            textRow(pixels, y, 350, 66, view.axis_labels[vertical], 2, Cyan);
            textRow(pixels, y, 350, 88, view.axis_values[vertical], 2, White);

            rectangleRow(pixels, y, 40, 110, 110, 100, Button);
            rectangleRow(pixels, y, 330, 110, 110, 100, Button);
            rectangleRow(pixels, y, 180, 64, 120, 80, Button);
            rectangleRow(pixels, y, 180, 176, 120, 80, Button);
            char horizontal_negative[] = { view.axis_labels[horizontal][0], '-', '\0' };
            char horizontal_positive[] = { view.axis_labels[horizontal][0], '+', '\0' };
            char vertical_negative[] = { view.axis_labels[vertical][0], '-', '\0' };
            char vertical_positive[] = { view.axis_labels[vertical][0], '+', '\0' };
            textRow(pixels, y, 59, 145, horizontal_negative, 4, White);
            textRow(pixels, y, 349, 145, horizontal_positive, 4, White);
            if (ui.z_is_pen_servo && ui.jog_plane != JogPlane::XY) {
                textRow(pixels, y, 192, 91, "PEN UP", 2, ui.pen_controls_enabled ? White : Muted);
                textRow(pixels, y, 180, 203, "PEN DOWN", 2, ui.pen_controls_enabled ? White : Muted);
            } else {
                textRow(pixels, y, 205, 89, vertical_positive, 4, White);
                textRow(pixels, y, 205, 201, vertical_negative, 4, White);
            }

            char profile[28] {};
            std::snprintf(profile, sizeof(profile), "S %.2FMM  F %.0F", ui.jog_distance_mm, ui.jog_feed_mm_min);
            textRow(pixels, y, 150, 151, profile, 2, Cyan);

            rectangleRow(pixels, y, 4, 260, 100, 56, Caution);
            rectangleRow(pixels, y, 108, 260, 62, 56, Button);
            rectangleRow(pixels, y, 174, 260, 62, 56, Button);
            rectangleRow(pixels, y, 240, 260, 62, 56, Button);
            rectangleRow(pixels, y, 306, 260, 62, 56, Button);
            rectangleRow(pixels, y, 372, 260, 104, 56, Danger);
            textRow(pixels, y, 18, 281, "UNLOCK", 2, White);
            textRow(pixels, y, 127, 277, "S-", 3, White);
            textRow(pixels, y, 193, 277, "S+", 3, White);
            textRow(pixels, y, 259, 277, "F-", 3, White);
            textRow(pixels, y, 325, 277, "F+", 3, White);
            textRow(pixels, y, 394, 281, "RESET", 2, White);
        } else {
            std::fill(pixels, pixels + StatusView::Width, Background);
            rectangleRow(pixels, y, 0, 0, StatusView::Width, 56, toneColor(view.tone));
            rectangleRow(pixels, y, 4, 4, 92, 48, Button);
            rectangleRow(pixels, y, 384, 4, 92, 48, Button);
            textRow(pixels, y, 17, 18, "BACK", 3, White);
            textRow(pixels, y, 190, 14, "FILES", 3, White);
            textRow(pixels, y, 391, 21, "RELOAD", 2, White);

            if (files == nullptr || files->loading) {
                textRow(pixels, y, 24, 88, "LOADING FILES", 3, Muted);
            } else if (!files->valid) {
                textRow(pixels, y, 24, 78, "FILES UNAVAILABLE", 3, Danger);
                textRow(pixels, y, 24, 116, files->error, 2, Muted);
            } else {
                textRow(pixels, y, 12, 58, files->path, 1, Muted);
                for (size_t row_index = 0; row_index < 5; ++row_index) {
                    int top = 68 + static_cast<int>(row_index) * 34;
                    rectangleRow(pixels, y, 8, top, 464, 30, row_index % 2 == 0 ? Panel : Button);
                    size_t entry_index = ui.file_offset + row_index;
                    if (entry_index < files->count) {
                        const auto& entry = files->entries[entry_index];
                        if (entry.isDirectory()) {
                            textRow(pixels, y, 16, top + 8, "DIR", 2, Cyan);
                            textRow(pixels, y, 68, top + 8, entry.name, 2, White);
                        } else {
                            textRow(pixels, y, 16, top + 8, entry.name, 2, White);
                        }
                    }
                }
            }
            rectangleRow(pixels, y, 4, 260, 152, 56, Button);
            rectangleRow(pixels, y, 324, 260, 152, 56, Button);
            textRow(pixels, y, 31, 277, "PREVIOUS", 2, White);
            textRow(pixels, y, 367, 277, "NEXT", 3, White);
        }

        if (ui.confirmation_visible) {
            rectangleRow(pixels, y, 12, 62, 456, 258, Overlay);
            rectangleRow(pixels, y, 12, 62, 456, 54, Caution);
            textRow(pixels, y, 140, 75, "CONFIRM", 4, White);
            // Keep a generous right gutter instead of relying on the panel's
            // final pixels: some mounted TS35 bezels obscure that edge.
            wrappedTextRows(pixels, y, 30, 132, ui.message, 2, White, 360, 3, 26);
            rectangleRow(pixels, y, 20, 220, 200, 84, Button);
            rectangleRow(pixels, y, 260, 220, 200, 84, Danger);
            textRow(pixels, y, 55, 248, "CANCEL", 3, White);
            textRow(pixels, y, 282, 248, "CONFIRM", 3, White);
        }
        return true;
    }

}  // namespace TS35
