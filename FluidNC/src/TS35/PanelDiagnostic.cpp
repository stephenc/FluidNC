// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PanelDiagnostic.h"

#include "StatusRasterizer.h"

#include <algorithm>
#include <array>

namespace TS35 {
    namespace {

        constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
            return static_cast<uint16_t>(((red & 0xf8U) << 8U) | ((green & 0xfcU) << 3U) | (blue >> 3U));
        }

        constexpr uint16_t Black   = rgb565(0, 0, 0);
        constexpr uint16_t White   = rgb565(255, 255, 255);
        constexpr uint16_t Red     = rgb565(255, 0, 0);
        constexpr uint16_t Green   = rgb565(0, 255, 0);
        constexpr uint16_t Blue    = rgb565(0, 0, 255);
        constexpr uint16_t Yellow  = rgb565(255, 255, 0);
        constexpr uint16_t Cyan    = rgb565(0, 255, 255);
        constexpr uint16_t Magenta = rgb565(255, 0, 255);

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

        // Large seven-segment card number in the center. Segment order is
        // top, upper-right, lower-right, bottom, lower-left, upper-left, middle.
        void cardNumber(uint16_t* pixels, int y, unsigned number, uint16_t color) {
            constexpr uint8_t segments[] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d };
            uint8_t bits = segments[number % 6];
            constexpr int left = 210;
            constexpr int top  = 104;
            if ((bits & 0x01U) != 0) rectangleRow(pixels, y, left, top, 60, 10, color);
            if ((bits & 0x02U) != 0) rectangleRow(pixels, y, left + 50, top, 10, 55, color);
            if ((bits & 0x04U) != 0) rectangleRow(pixels, y, left + 50, top + 55, 10, 55, color);
            if ((bits & 0x08U) != 0) rectangleRow(pixels, y, left, top + 100, 60, 10, color);
            if ((bits & 0x10U) != 0) rectangleRow(pixels, y, left, top + 55, 10, 55, color);
            if ((bits & 0x20U) != 0) rectangleRow(pixels, y, left, top, 10, 55, color);
            if ((bits & 0x40U) != 0) rectangleRow(pixels, y, left, top + 50, 60, 10, color);
        }

    }  // namespace

    bool renderDiagnosticScanline(unsigned card, int y, uint16_t* pixels, size_t pixel_count) {
        if (pixels == nullptr || pixel_count < StatusView::Width || y < 0 || y >= StatusView::Height ||
            card >= DiagnosticCardCount) {
            return false;
        }

        if (card == 4) {
            StatusView view;
            view.state            = "CARD 4 TEXT";
            view.tone             = StatusTone::Warning;
            view.position_caption = "ORIENTATION 480 X 320 RGB565";
            view.axis_labels      = { "X", "Y", "Z" };
            view.axis_values      = { "-123.456", "0.000", "987.654" };
            view.limit_text       = "LIMITS XYZ PROBE";
            view.feed_text        = "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789";
            view.tool_text        = "CLIPPING CHECK: LEFT........................RIGHT";
            view.job_text         = "BOTTOM ROW DESCENDERS , . /";
            view.alarm_text       = "TOP RIGHT";
            return renderStatusScanline(view, y, pixels, pixel_count);
        }

        std::fill(pixels, pixels + StatusView::Width, Black);
        switch (card) {
            case 0: {
                constexpr uint16_t colors[] = { Red, Green, Blue, White };
                for (int band = 0; band < 4; ++band) {
                    span(pixels, band * 120, 120, colors[band]);
                }
                cardNumber(pixels, y, card, Black);
                break;
            }
            case 1:
                rectangleRow(pixels, y, 0, 0, 48, 48, Red);
                rectangleRow(pixels, y, StatusView::Width - 48, 0, 48, 48, Green);
                rectangleRow(pixels, y, 0, StatusView::Height - 48, 48, 48, Blue);
                rectangleRow(pixels, y, StatusView::Width - 48, StatusView::Height - 48, 48, 48, Yellow);
                if (y == 0 || y == StatusView::Height - 1) span(pixels, 0, StatusView::Width, White);
                pixels[0]                       = White;
                pixels[StatusView::Width - 1]   = White;
                span(pixels, StatusView::Width / 2 - 1, 3, White);
                cardNumber(pixels, y, card, White);
                break;
            case 2:
                for (int x = 0; x < StatusView::Width; ++x) {
                    pixels[x] = rgb565(static_cast<uint8_t>((x * 255) / (StatusView::Width - 1)),
                                       static_cast<uint8_t>((y * 255) / (StatusView::Height - 1)),
                                       static_cast<uint8_t>(255 - ((x * 255) / (StatusView::Width - 1))));
                }
                cardNumber(pixels, y, card, White);
                break;
            case 3:
                if (y % 20 == 0) {
                    span(pixels, 0, StatusView::Width, Cyan);
                } else {
                    for (int x = 0; x < StatusView::Width; x += 20) {
                        pixels[x] = Cyan;
                    }
                }
                rectangleRow(pixels, y, -10, 145, 35, 30, Magenta);
                rectangleRow(pixels, y, StatusView::Width - 25, 145, 35, 30, Yellow);
                cardNumber(pixels, y, card, White);
                break;
            case 5:
                for (int x = 0; x < StatusView::Width; ++x) {
                    bool checker = ((x / 16) + (y / 16)) % 2 == 0;
                    pixels[x]    = checker ? rgb565(24, 42, 64) : rgb565(8, 14, 24);
                }
                rectangleRow(pixels, y, 20, 20, 80, 80, Red);
                rectangleRow(pixels, y, 380, 220, 80, 80, Green);
                cardNumber(pixels, y, card, White);
                break;
            default:
                return false;
        }
        return true;
    }

}  // namespace TS35
