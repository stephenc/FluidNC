// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/PanelDiagnostic.h"

#include "gtest/gtest.h"

#include <array>
#include <set>

namespace {
    using namespace TS35;

    TEST(TS35PanelDiagnostic, RejectsInvalidCardRowsAndDestination) {
        std::array<uint16_t, StatusView::Width> row {};
        EXPECT_FALSE(renderDiagnosticScanline(DiagnosticCardCount, 0, row.data(), row.size()));
        EXPECT_FALSE(renderDiagnosticScanline(0, -1, row.data(), row.size()));
        EXPECT_FALSE(renderDiagnosticScanline(0, StatusView::Height, row.data(), row.size()));
        EXPECT_FALSE(renderDiagnosticScanline(0, 0, nullptr, row.size()));
        EXPECT_FALSE(renderDiagnosticScanline(0, 0, row.data(), row.size() - 1));
    }

    TEST(TS35PanelDiagnostic, EveryCardIsDeterministicDistinctAndGuarded) {
        std::set<uint64_t> hashes;
        for (unsigned card = 0; card < DiagnosticCardCount; ++card) {
            uint64_t hash = 1469598103934665603ULL;
            for (int y = 0; y < StatusView::Height; ++y) {
                std::array<uint16_t, StatusView::Width + 2> guarded {};
                guarded.front() = 0x1234;
                guarded.back()  = 0x5678;
                ASSERT_TRUE(renderDiagnosticScanline(card, y, guarded.data() + 1, StatusView::Width));
                EXPECT_EQ(guarded.front(), 0x1234);
                EXPECT_EQ(guarded.back(), 0x5678);
                for (size_t x = 1; x <= StatusView::Width; ++x) {
                    hash ^= guarded[x];
                    hash *= 1099511628211ULL;
                }
            }
            EXPECT_TRUE(hashes.insert(hash).second);
        }
    }

}  // namespace
