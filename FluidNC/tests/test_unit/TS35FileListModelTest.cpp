// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TS35/FileListModel.h"

#include "gtest/gtest.h"

#include <string>

namespace {
    using namespace TS35;

    TEST(TS35FileListModel, ParsesSupportedFluidNCResponseAcrossArbitraryChunks) {
        FileListModel model;
        model.begin();

        ASSERT_TRUE(model.feed("{\"fil"));
        ASSERT_TRUE(model.feed("es\":[{\"name\":\"plot one.nc\",\"size\":1234},"));
        ASSERT_TRUE(model.feed("{\"name\":\"subdir\",\"size\":-1}],\"path\":\"/sd\"}"));
        ASSERT_TRUE(model.finish());

        const auto& list = model.snapshot();
        EXPECT_TRUE(list.valid);
        EXPECT_FALSE(list.loading);
        ASSERT_EQ(list.count, 2U);
        EXPECT_EQ(list.path, "/sd");
        EXPECT_EQ(list.entries[0].name, "plot one.nc");
        EXPECT_EQ(list.entries[0].size, 1234);
        EXPECT_FALSE(list.entries[0].isDirectory());
        EXPECT_EQ(list.entries[1].name, "subdir");
        EXPECT_TRUE(list.entries[1].isDirectory());
    }

    TEST(TS35FileListModel, HandlesEscapesSplitAcrossChunks) {
        FileListModel model;
        model.begin();
        ASSERT_TRUE(model.feed(R"json({"files":[{"name":"quote\)json"));
        ASSERT_TRUE(model.feed(R"json("slash\\unicode\u00)json"));
        ASSERT_TRUE(model.feed(R"json(e9.nc","size":7}],"path":"/sd"})json"));
        ASSERT_TRUE(model.finish());

        ASSERT_EQ(model.snapshot().count, 1U);
        EXPECT_EQ(model.snapshot().entries[0].name, "quote\"slash\\unicode?.nc");
    }

    TEST(TS35FileListModel, EmptyDirectoryIsValid) {
        FileListModel model;
        model.begin();
        ASSERT_TRUE(model.feed("{\"files\":[],\"path\":\"/sd/empty\"}"));
        EXPECT_TRUE(model.finish());
        EXPECT_TRUE(model.snapshot().valid);
        EXPECT_EQ(model.snapshot().count, 0U);
    }

    TEST(TS35FileListModel, FluidNCErrorProducesInvalidSnapshot) {
        FileListModel model;
        model.begin();
        ASSERT_TRUE(model.feed("{\"files\":[],\"path\":\"/sd/missing\",\"error\":\"Bad path\"}"));
        EXPECT_FALSE(model.finish());
        EXPECT_FALSE(model.snapshot().valid);
        EXPECT_EQ(model.snapshot().error, "Bad path");
    }

    TEST(TS35FileListModel, MalformedOrInterruptedResponseFailsClosed) {
        FileListModel model;
        model.begin();
        ASSERT_TRUE(model.feed("{\"files\":[{\"name\":\"half.nc\""));
        EXPECT_FALSE(model.finish());
        EXPECT_FALSE(model.snapshot().valid);
        EXPECT_FALSE(model.snapshot().loading);
        EXPECT_EQ(model.snapshot().count, 0U);
        EXPECT_EQ(model.snapshot().error, "Malformed file-list response");
    }

    TEST(TS35FileListModel, EveryInterruptedPrefixFailsClosedAndNextResponseRecovers) {
        const std::string json = R"json({"files":[{"name":"plot.nc","size":1234}],"path":"/sd"})json";

        for (size_t length = 0; length < json.size(); ++length) {
            FileListModel model;
            model.begin();
            ASSERT_TRUE(model.feed(std::string_view(json).substr(0, length))) << "prefix length " << length;
            EXPECT_FALSE(model.finish()) << "prefix length " << length;
            EXPECT_FALSE(model.snapshot().valid) << "prefix length " << length;
            EXPECT_FALSE(model.snapshot().loading) << "prefix length " << length;
            EXPECT_EQ(model.snapshot().count, 0U) << "prefix length " << length;

            model.begin();
            ASSERT_TRUE(model.feed(json)) << "recovery after prefix length " << length;
            ASSERT_TRUE(model.finish()) << "recovery after prefix length " << length;
            ASSERT_EQ(model.snapshot().count, 1U);
            EXPECT_EQ(model.snapshot().entries[0].name, "plot.nc");
        }
    }

    TEST(TS35FileListModel, OutOfRangeFileSizeFailsClosed) {
        for (const char* size : { "9223372036854775808", "-9223372036854775809" }) {
            FileListModel model;
            model.begin();

            std::string json = "{\"files\":[{\"name\":\"bad.nc\",\"size\":" + std::string(size) + "}],\"path\":\"/sd\"}";
            EXPECT_FALSE(model.feed(json));
            EXPECT_FALSE(model.finish());
            EXPECT_FALSE(model.snapshot().valid);
            EXPECT_FALSE(model.snapshot().loading);
            EXPECT_EQ(model.snapshot().count, 0U);
            EXPECT_EQ(model.snapshot().error, "Malformed file-list response");
        }
    }

    TEST(TS35FileListModel, EntryCapacityIsBoundedAndReported) {
        std::string json = "{\"files\":[";
        for (size_t index = 0; index < MaxFileEntries + 3; ++index) {
            if (index != 0) {
                json += ',';
            }
            json += "{\"name\":\"file" + std::to_string(index) + ".nc\",\"size\":1}";
        }
        json += "],\"path\":\"/sd\"}";

        FileListModel model;
        model.begin();
        ASSERT_TRUE(model.feed(json));
        ASSERT_TRUE(model.finish());
        EXPECT_EQ(model.snapshot().count, MaxFileEntries);
        EXPECT_TRUE(model.snapshot().truncated);
    }

    TEST(TS35FileListModel, OversizeNameIsSkippedWithoutUnboundingParser) {
        std::string long_name(MaxFileName + 10, 'a');
        std::string json = "{\"files\":[{\"name\":\"" + long_name + "\",\"size\":1},{\"name\":\"ok.nc\",\"size\":2}],\"path\":\"/sd\"}";

        FileListModel model;
        model.begin();
        ASSERT_TRUE(model.feed(json));
        ASSERT_TRUE(model.finish());
        ASSERT_EQ(model.snapshot().count, 1U);
        EXPECT_EQ(model.snapshot().entries[0].name, "ok.nc");
        EXPECT_TRUE(model.snapshot().truncated);
    }

    TEST(TS35FileListModel, AbortClearsLoadingAndStaleEntries) {
        FileListModel model;
        model.begin();
        ASSERT_TRUE(model.feed("{\"files\":[{\"name\":\"old.nc\",\"size\":1}],\"path\":\"/sd\"}"));
        ASSERT_TRUE(model.finish());
        ASSERT_EQ(model.snapshot().count, 1U);

        model.begin();
        model.abort("Disconnected");
        EXPECT_FALSE(model.snapshot().loading);
        EXPECT_FALSE(model.snapshot().valid);
        EXPECT_EQ(model.snapshot().count, 0U);
        EXPECT_EQ(model.snapshot().error, "Disconnected");
    }

}  // namespace
