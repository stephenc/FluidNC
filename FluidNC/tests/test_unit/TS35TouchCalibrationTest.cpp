// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/TouchCalibration.h"

#include "gtest/gtest.h"

namespace {
    using namespace TS35;

    TEST(TS35TouchCalibration, DerivesDirectAxesAndMapsCorners) {
        TouchCalibration calibration;
        ASSERT_TRUE(deriveTouchCalibration({ { 300, 400 }, { 3700, 420 }, { 320, 3600 }, { 3680, 3580 } }, calibration));
        EXPECT_FALSE(calibration.swap_axes);
        EXPECT_FALSE(calibration.invert_x);
        EXPECT_FALSE(calibration.invert_y);

        auto top_left = mapTouch({ 310, 410 }, calibration, 480, 320);
        EXPECT_TRUE(top_left.valid);
        EXPECT_EQ(top_left.x, 0);
        EXPECT_EQ(top_left.y, 0);
        auto bottom_right = mapTouch({ 3690, 3590 }, calibration, 480, 320);
        EXPECT_EQ(bottom_right.x, 479);
        EXPECT_EQ(bottom_right.y, 319);
    }

    TEST(TS35TouchCalibration, DerivesSwappedAndInvertedAxes) {
        TouchCalibration calibration;
        ASSERT_TRUE(deriveTouchCalibration({ { 3600, 3700 }, { 3580, 300 }, { 400, 3680 }, { 420, 320 } }, calibration));
        EXPECT_TRUE(calibration.swap_axes);
        EXPECT_TRUE(calibration.invert_x);
        EXPECT_TRUE(calibration.invert_y);

        auto center = mapTouch({ 2000, 2000 }, calibration, 480, 320);
        EXPECT_TRUE(center.valid);
        EXPECT_NEAR(center.x, 240, 2);
        EXPECT_NEAR(center.y, 160, 2);
    }

    TEST(TS35TouchCalibration, RejectsSmallOrAmbiguousGestureSets) {
        TouchCalibration calibration;
        EXPECT_FALSE(deriveTouchCalibration({ { 1000, 1000 }, { 1050, 1040 }, { 1030, 1050 }, { 1060, 1060 } }, calibration));
        EXPECT_FALSE(calibration.valid());

        EXPECT_FALSE(deriveTouchCalibration({ { 200, 200 }, { 3000, 3000 }, { 3000, 3000 }, { 3800, 3800 } }, calibration));
    }

    TEST(TS35TouchCalibration, InvalidCalibrationFailsClosedAndEdgesClamp) {
        EXPECT_FALSE(mapTouch({ 1000, 1000 }, {}, 480, 320).valid);

        TouchCalibration calibration { 300, 3700, 400, 3600, false, false, false };
        auto             below = mapTouch({ 0, 0 }, calibration, 480, 320);
        auto             above = mapTouch({ 4095, 4095 }, calibration, 480, 320);
        ASSERT_TRUE(below.valid);
        EXPECT_EQ(below.x, 0);
        EXPECT_EQ(below.y, 0);
        EXPECT_EQ(above.x, 479);
        EXPECT_EQ(above.y, 319);
    }

    TEST(TS35TouchCalibration, MapsPhysicalTs35CornerCapture) {
        TouchCorners physical { { 3693, 643 }, { 392, 672 }, { 3709, 3576 }, { 404, 3517 } };
        TouchCalibration calibration;
        ASSERT_TRUE(deriveTouchCalibration(physical, calibration));
        EXPECT_FALSE(calibration.swap_axes);
        EXPECT_TRUE(calibration.invert_x);
        EXPECT_FALSE(calibration.invert_y);
        EXPECT_EQ(calibration.raw_x_min, 398);
        EXPECT_EQ(calibration.raw_x_max, 3701);
        EXPECT_EQ(calibration.raw_y_min, 657);
        EXPECT_EQ(calibration.raw_y_max, 3546);

        auto top_left     = mapTouch(physical.top_left, calibration, 480, 320);
        auto bottom_right = mapTouch(physical.bottom_right, calibration, 480, 320);
        EXPECT_NEAR(top_left.x, 0, 4);
        EXPECT_NEAR(top_left.y, 0, 4);
        EXPECT_NEAR(bottom_right.x, 479, 4);
        EXPECT_NEAR(bottom_right.y, 319, 4);
    }

}  // namespace
