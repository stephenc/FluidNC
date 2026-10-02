// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/StatusRasterizer.h"

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <string_view>

namespace TS35 {
    namespace {

        uint64_t frameHash(const MachineSnapshot& snapshot) {
            StatusView                              view = composeStatusView(snapshot);
            std::array<uint16_t, StatusView::Width> row {};
            uint64_t                                hash = 1469598103934665603ULL;
            for (int y = 0; y < StatusView::Height; ++y) {
                EXPECT_TRUE(renderStatusScanline(view, y, row.data(), row.size()));
                for (uint16_t pixel : row) {
                    hash ^= pixel;
                    hash *= 1099511628211ULL;
                }
            }
            return hash;
        }

        uint64_t fixtureFrameHash(std::initializer_list<std::string_view> lines) {
            MachineModel model;
            for (std::string_view line : lines) {
                model.ingestLine(line);
            }
            return frameHash(model.snapshot());
        }

        uint64_t uiFrameHash(const StatusView& view, const TouchUiState& ui, const FileListSnapshot* files = nullptr) {
            std::array<uint16_t, StatusView::Width> row {};
            uint64_t hash = 1469598103934665603ULL;
            for (int y = 0; y < StatusView::Height; ++y) {
                EXPECT_TRUE(renderTouchUiScanline(view, ui, y, row.data(), row.size(), files));
                for (uint16_t pixel : row) {
                    hash ^= pixel;
                    hash *= 1099511628211ULL;
                }
            }
            return hash;
        }

        uint64_t uiFrameHash(const TouchUiState& ui, const FileListSnapshot* files = nullptr) {
            return uiFrameHash(StatusView {}, ui, files);
        }

        TEST(TS35StatusRasterizer, RejectsInvalidDestinationsAndRows) {
            StatusView                              view;
            std::array<uint16_t, StatusView::Width> row {};

            EXPECT_FALSE(renderStatusScanline(view, -1, row.data(), row.size()));
            EXPECT_FALSE(renderStatusScanline(view, StatusView::Height, row.data(), row.size()));
            EXPECT_FALSE(renderStatusScanline(view, 0, nullptr, row.size()));
            EXPECT_FALSE(renderStatusScanline(view, 0, row.data(), row.size() - 1));
        }

        TEST(TS35StatusRasterizer, WritesExactlyOneScanline) {
            StatusView                                  view;
            std::array<uint16_t, StatusView::Width + 2> guarded {};
            guarded.front() = 0x1234;
            guarded.back()  = 0x5678;

            EXPECT_TRUE(renderStatusScanline(view, 0, guarded.data() + 1, StatusView::Width));
            EXPECT_EQ(guarded.front(), 0x1234);
            EXPECT_EQ(guarded.back(), 0x5678);
            EXPECT_NE(guarded[1], 0);
        }

        TEST(TS35StatusRasterizer, SceneRegionsHaveDistinctColors) {
            StatusView                              view;
            std::array<uint16_t, StatusView::Width> banner {};
            std::array<uint16_t, StatusView::Width> background {};
            std::array<uint16_t, StatusView::Width> card {};

            ASSERT_TRUE(renderStatusScanline(view, 0, banner.data(), banner.size()));
            ASSERT_TRUE(renderStatusScanline(view, 60, background.data(), background.size()));
            ASSERT_TRUE(renderStatusScanline(view, 100, card.data(), card.size()));

            EXPECT_NE(banner[0], background[0]);
            EXPECT_EQ(background[0], card[0]);
            EXPECT_NE(card[16], card[0]);
        }

        TEST(TS35StatusRasterizer, RequiredScenesMatchGoldenPixelHashes) {
            EXPECT_EQ(fixtureFrameHash({}), 18188353351838375020ULL);
            EXPECT_EQ(fixtureFrameHash({ "<Idle|MPos:10.000,20.000,0.000|WCO:10.000,20.000,0.000|FS:0,0|Ov:100,100,100>" }),
                      8936458303098950960ULL);
            EXPECT_EQ(fixtureFrameHash({ "<Jog|WPos:12.500,8.250,0.000|FS:750,0|Pn:X|Ov:100,100,100>" }),
                      16504216700904050564ULL);
            EXPECT_EQ(fixtureFrameHash(
                          { "<Run|WPos:42.125,18.750,0.000|FS:1200,0|Ov:95,100,100|SD:37.5,calibration-plot.nc>" }),
                      16676473085285578480ULL);
            EXPECT_EQ(fixtureFrameHash({ "<Hold:0|WPos:42.125,18.750,0.000|FS:0,0|SD:37.5,calibration-plot.nc>" }),
                      10347888174635950548ULL);
            EXPECT_EQ(fixtureFrameHash({ "<Home|MPos:-5.000,-5.000,0.000|FS:500,0|Pn:XY>" }),
                      6875451784615298492ULL);
            EXPECT_EQ(fixtureFrameHash({ "ALARM:14 (Homing required)", "<Alarm|WPos:0,0,0|Pn:Y>" }),
                      6988348463882620360ULL);
            EXPECT_EQ(fixtureFrameHash({ "<Run|WPos:1,not-a-number,3>",
                                         "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                         "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                         "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                         "xxxxxxxxxxxxxxxx",
                                         "<Idle|WPos:1,2,3|FS:0,0>" }),
                      16512442592455780204ULL);
        }

        TEST(TS35StatusRasterizer, DisconnectAfterActivityMatchesCleanStartExactly) {
            MachineModel clean;
            MachineModel delayed;
            ASSERT_TRUE(delayed.ingestLine(
                "<Run|WPos:20,10,0|FS:500,12000|Ov:95,50,110|A:SFM|Pn:PX|SD:10,plot.nc>"));

            delayed.markDisconnected();

            EXPECT_EQ(frameHash(delayed.snapshot()), frameHash(clean.snapshot()));
        }

        TEST(TS35StatusRasterizer, TouchPagesAndConfirmationHaveStableGoldenHashes) {
            TouchUiState status;
            EXPECT_EQ(uiFrameHash(status), 17391761114179697842ULL);

            TouchUiState jog;
            jog.page = UiPage::Jog;
            EXPECT_EQ(uiFrameHash(jog), 8265158651094126169ULL);

            TouchUiState confirmation;
            confirmation.confirmation_visible = true;
            confirmation.message = "Homing can move every configured axis";
            EXPECT_EQ(uiFrameHash(confirmation), 15602517804770566152ULL);

            TouchUiState files;
            files.page = UiPage::Files;
            EXPECT_EQ(uiFrameHash(files), 17818400288928854033ULL);

            FileListSnapshot listing;
            listing.valid = true;
            listing.path = "/sd";
            listing.count = 2;
            listing.entries[0] = { "plots", -1 };
            listing.entries[1] = { "drawing.nc", 1234 };
            EXPECT_EQ(uiFrameHash(files, &listing), 14034761344721945055ULL);
        }

        TEST(TS35StatusRasterizer, JogSceneTracksLivePositionSpeedAndWifiStatus) {
            TouchUiState jog;
            jog.page = UiPage::Jog;
            StatusView view;
            uint64_t baseline = uiFrameHash(view, jog);

            view.axis_values[0] = "12.500";
            view.axis_values[1] = "-8.250";
            EXPECT_NE(uiFrameHash(view, jog), baseline);

            uint64_t positioned = uiFrameHash(view, jog);
            jog.jog_feed_mm_min = 1000.0f;
            EXPECT_NE(uiFrameHash(view, jog), positioned);

            uint64_t faster = uiFrameHash(view, jog);
            jog.wifi_state = WifiState::Station;
            jog.wifi_signal_percent = 70;
            EXPECT_NE(uiFrameHash(view, jog), faster);
        }

        TEST(TS35StatusRasterizer, WifiGlyphDistinguishesStationStrengthApAndNoConnection) {
            TouchUiState wifi;
            StatusView view;

            uint64_t disconnected = uiFrameHash(view, wifi);
            wifi.wifi_state = WifiState::Connecting;
            EXPECT_EQ(uiFrameHash(view, wifi), disconnected);

            wifi.wifi_state = WifiState::Station;
            wifi.wifi_signal_percent = 20;
            uint64_t weak_station = uiFrameHash(view, wifi);
            EXPECT_NE(weak_station, disconnected);

            wifi.wifi_signal_percent = 80;
            uint64_t strong_station = uiFrameHash(view, wifi);
            EXPECT_NE(strong_station, weak_station);

            wifi.wifi_state = WifiState::AccessPoint;
            uint64_t access_point = uiFrameHash(view, wifi);
            EXPECT_NE(access_point, strong_station);
            EXPECT_NE(access_point, disconnected);
        }

        TEST(TS35StatusRasterizer, HoldReportVisuallyLatchesTheHoldButton) {
            TouchUiState status;
            StatusView running;
            running.state = "Run";
            StatusView held = running;
            held.state = "Hold:0";

            EXPECT_NE(uiFrameHash(running, status), uiFrameHash(held, status));

            std::array<uint16_t, StatusView::Width> running_row {};
            std::array<uint16_t, StatusView::Width> held_row {};
            ASSERT_TRUE(renderTouchUiScanline(running, status, 260, running_row.data(), running_row.size()));
            ASSERT_TRUE(renderTouchUiScanline(held, status, 260, held_row.data(), held_row.size()));
            EXPECT_NE(running_row[200], held_row[200]);
        }

        TEST(TS35StatusRasterizer, ConfirmationDialogWrapsLongReasonsInsideTheOverlay) {
            TouchUiState confirmation;
            confirmation.confirmation_visible = true;
            confirmation.message = "Reset stops the current operation and restarts FluidNC";
            StatusView view;
            std::array<uint16_t, StatusView::Width> first_line {};
            std::array<uint16_t, StatusView::Width> second_line {};
            std::array<uint16_t, StatusView::Width> third_line {};

            ASSERT_TRUE(renderTouchUiScanline(view, confirmation, 136, first_line.data(), first_line.size()));
            ASSERT_TRUE(renderTouchUiScanline(view, confirmation, 162, second_line.data(), second_line.size()));
            ASSERT_TRUE(renderTouchUiScanline(view, confirmation, 188, third_line.data(), third_line.size()));
            EXPECT_NE(first_line, second_line);
            EXPECT_NE(second_line, third_line);

            // The reset reason wraps onto two short lines and remains inside
            // the explicit 360-pixel content width, leaving a bezel-safe gutter.
            constexpr uint16_t overlay = 0x08c5;
            auto has_text = [=](const auto& row) {
                return std::any_of(row.begin() + 30, row.begin() + 390,
                                   [](uint16_t pixel) { return pixel != overlay; });
            };
            auto gutter_is_clear = [=](const auto& row) {
                return std::all_of(row.begin() + 390, row.begin() + 468,
                                   [](uint16_t pixel) { return pixel == overlay; });
            };
            EXPECT_TRUE(has_text(first_line));
            EXPECT_TRUE(has_text(second_line));
            EXPECT_FALSE(has_text(third_line));
            EXPECT_TRUE(gutter_is_clear(first_line));
            EXPECT_TRUE(gutter_is_clear(second_line));
            EXPECT_TRUE(gutter_is_clear(third_line));
        }

    }  // namespace
}  // namespace TS35
