// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/ReportWatchdog.h"

#include <gtest/gtest.h>

namespace TS35 {
    TEST(TS35ReportWatchdog, CannotExpireBeforeFirstAcceptedReport) {
        ReportWatchdog watchdog;
        watchdog.reset(100);

        EXPECT_FALSE(watchdog.seenReport());
        EXPECT_FALSE(watchdog.expired(100000, 2000));
    }

    TEST(TS35ReportWatchdog, ExpiresOnlyAfterTimeoutBoundary) {
        ReportWatchdog watchdog;
        watchdog.observe(1000);

        EXPECT_FALSE(watchdog.expired(3000, 2000));
        EXPECT_TRUE(watchdog.expired(3001, 2000));
    }

    TEST(TS35ReportWatchdog, NewReportRestartsDeadline) {
        ReportWatchdog watchdog;
        watchdog.observe(1000);
        watchdog.observe(2500);

        EXPECT_FALSE(watchdog.expired(4000, 2000));
        EXPECT_TRUE(watchdog.expired(4501, 2000));
    }

    TEST(TS35ReportWatchdog, ElapsedTimeSurvivesMillisWrap) {
        ReportWatchdog watchdog;
        watchdog.observe(UINT32_MAX - 1000U);

        EXPECT_FALSE(watchdog.expired(999U, 2000));
        EXPECT_TRUE(watchdog.expired(1000U, 2000));
    }
}  // namespace TS35
