// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace TS35 {

    constexpr size_t MaxFileEntries = 32;
    constexpr size_t MaxFileName    = 95;
    constexpr size_t MaxFilePath    = 127;

    struct FileEntry {
        std::string name;
        int64_t     size = 0;

        bool isDirectory() const { return size < 0; }
    };

    struct FileListSnapshot {
        std::array<FileEntry, MaxFileEntries> entries;
        size_t                                count = 0;
        std::string                           path;
        std::string                           error;
        bool                                  loading    = false;
        bool                                  valid      = false;
        bool                                  truncated  = false;
        uint64_t                              generation = 0;
    };

    // Narrow, allocation-bounded-by-fields streaming parser for FluidNC's
    // $Files/ListGCode JSON. It consumes arbitrary chunk boundaries without
    // buffering the complete document.
    class FileListModel {
    public:
        const FileListSnapshot& snapshot() const { return _snapshot; }

        void begin();
        bool feed(std::string_view chunk);
        bool finish();
        void abort(const char* reason);

    private:
        enum class LexState : uint8_t { Idle, String, Escape, Unicode, Number, Literal };

        void resetParser();
        bool punctuation(char c);
        bool completeString();
        bool completeNumber();
        void appendToken(char c, size_t limit);
        void beginFileEntry();
        void endFileEntry();

        FileListSnapshot _snapshot;
        FileListSnapshot _working;
        FileEntry        _entry;

        LexState    _lex = LexState::Idle;
        std::string _token;
        std::string _key;
        int         _object_depth      = 0;
        int         _array_depth       = 0;
        int         _files_array_depth = 0;
        int         _file_object_depth = 0;
        int         _unicode_digits    = 0;
        bool        _expecting_key     = false;
        bool        _token_overflow    = false;
        bool        _entry_has_name    = false;
        bool        _entry_has_size    = false;
        bool        _saw_root          = false;
        bool        _saw_files         = false;
        bool        _saw_path          = false;
        bool        _failed            = false;
    };

}  // namespace TS35
