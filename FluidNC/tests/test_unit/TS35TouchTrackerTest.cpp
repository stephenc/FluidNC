// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/TouchTracker.h"

#include "gtest/gtest.h"

namespace {
    using namespace TS35;

    ScreenPoint at(int16_t x, int16_t y) {
        return { x, y, true };
    }

    TEST(TS35TouchTracker, DebouncesPressAndReleaseExactlyOnce) {
        TouchTracker tracker(2, 2, 3, 80);
        EXPECT_EQ(tracker.ingest(true, at(10, 20)).kind, TouchEventKind::None);
        auto press = tracker.ingest(true, at(11, 21));
        EXPECT_EQ(press.kind, TouchEventKind::Press);
        EXPECT_TRUE(tracker.active());
        EXPECT_EQ(tracker.ingest(false, {}).kind, TouchEventKind::None);
        EXPECT_TRUE(tracker.active());
        EXPECT_EQ(tracker.ingest(true, at(11, 21)).kind, TouchEventKind::None);
        EXPECT_EQ(tracker.ingest(false, {}).kind, TouchEventKind::None);
        auto release = tracker.ingest(false, {});
        EXPECT_EQ(release.kind, TouchEventKind::Release);
        EXPECT_EQ(release.point.x, 11);
        EXPECT_FALSE(tracker.active());
        EXPECT_EQ(tracker.ingest(false, {}).kind, TouchEventKind::None);
    }

    TEST(TS35TouchTracker, ReportsOnlyMeaningfulMoves) {
        TouchTracker tracker(1, 1, 4);
        ASSERT_EQ(tracker.ingest(true, at(100, 100)).kind, TouchEventKind::Press);
        EXPECT_EQ(tracker.ingest(true, at(103, 103)).kind, TouchEventKind::None);
        auto move = tracker.ingest(true, at(104, 100));
        EXPECT_EQ(move.kind, TouchEventKind::Move);
        EXPECT_EQ(move.point.x, 104);
    }

    TEST(TS35TouchTracker, InvalidPressedPointFailsClosedAsRelease) {
        TouchTracker tracker(1, 1, 1);
        ASSERT_EQ(tracker.ingest(true, at(50, 60)).kind, TouchEventKind::Press);
        ScreenPoint invalid;
        EXPECT_EQ(tracker.ingest(true, invalid).kind, TouchEventKind::Release);
        EXPECT_FALSE(tracker.active());
    }

    TEST(TS35TouchTracker, BridgesShortPressureDropoutsWithoutStartingANewGesture) {
        TouchTracker tracker(2, 10, 8, 80);
        EXPECT_EQ(tracker.ingest(true, at(100, 100)).kind, TouchEventKind::None);
        EXPECT_EQ(tracker.ingest(true, at(101, 100)).kind, TouchEventKind::Press);
        for (int sample = 0; sample < 9; ++sample) {
            EXPECT_EQ(tracker.ingest(false, {}).kind, TouchEventKind::None);
            EXPECT_TRUE(tracker.active());
        }
        EXPECT_EQ(tracker.ingest(true, at(105, 100)).kind, TouchEventKind::None);
        for (int sample = 0; sample < 9; ++sample) {
            EXPECT_EQ(tracker.ingest(false, {}).kind, TouchEventKind::None);
        }
        EXPECT_EQ(tracker.ingest(false, {}).kind, TouchEventKind::Release);
        EXPECT_FALSE(tracker.active());
    }

    TEST(TS35TouchTracker, RejectsAnIsolatedCoordinateSpike) {
        TouchTracker tracker(1, 1, 4, 80);
        ASSERT_EQ(tracker.ingest(true, at(470, 280)).kind, TouchEventKind::Press);
        EXPECT_EQ(tracker.ingest(true, at(471, 116)).kind, TouchEventKind::None);
        auto move = tracker.ingest(true, at(479, 310));
        EXPECT_EQ(move.kind, TouchEventKind::Move);
        EXPECT_EQ(move.point.x, 479);
        EXPECT_EQ(move.point.y, 310);
    }

    TEST(TS35TouchTracker, AcceptsALargeMoveOnlyAfterSpatialConfirmation) {
        TouchTracker tracker(1, 1, 4, 80);
        ASSERT_EQ(tracker.ingest(true, at(20, 20)).kind, TouchEventKind::Press);
        EXPECT_EQ(tracker.ingest(true, at(200, 200)).kind, TouchEventKind::None);
        auto move = tracker.ingest(true, at(205, 204));
        EXPECT_EQ(move.kind, TouchEventKind::Move);
        EXPECT_EQ(move.point.x, 205);
        EXPECT_EQ(move.point.y, 204);
    }

    TEST(TS35TouchTracker, CancelClearsActiveGestureAndIsIdempotent) {
        TouchTracker tracker(1, 1, 1);
        EXPECT_EQ(tracker.cancel().kind, TouchEventKind::None);
        ASSERT_EQ(tracker.ingest(true, at(7, 8)).kind, TouchEventKind::Press);
        auto cancelled = tracker.cancel();
        EXPECT_EQ(cancelled.kind, TouchEventKind::Cancel);
        EXPECT_EQ(cancelled.point.y, 8);
        EXPECT_FALSE(tracker.active());
        EXPECT_EQ(tracker.cancel().kind, TouchEventKind::None);
    }

}  // namespace
