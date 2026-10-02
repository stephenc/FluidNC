// Copyright (c) 2026 Stephen Connolly
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FileListModel.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>

namespace TS35 {

    void FileListModel::resetParser() {
        _working = {};
        _entry   = {};
        _lex     = LexState::Idle;
        _token.clear();
        _key.clear();
        _object_depth      = 0;
        _array_depth       = 0;
        _files_array_depth = 0;
        _file_object_depth = 0;
        _unicode_digits    = 0;
        _expecting_key     = false;
        _token_overflow    = false;
        _entry_has_name    = false;
        _entry_has_size    = false;
        _saw_root          = false;
        _saw_files         = false;
        _saw_path          = false;
        _failed            = false;
    }

    void FileListModel::begin() {
        resetParser();
        _snapshot.loading = true;
        _snapshot.valid   = false;
        _snapshot.error.clear();
        ++_snapshot.generation;
    }

    void FileListModel::appendToken(char c, size_t limit) {
        if (_token.size() < limit) {
            _token += c;
        } else {
            _token_overflow = true;
        }
    }

    void FileListModel::beginFileEntry() {
        _entry             = {};
        _entry_has_name    = false;
        _entry_has_size    = false;
        _file_object_depth = _object_depth;
    }

    void FileListModel::endFileEntry() {
        if (_entry_has_name && _entry_has_size && !_token_overflow) {
            if (_working.count < _working.entries.size()) {
                _working.entries[_working.count++] = _entry;
            } else {
                _working.truncated = true;
            }
        } else {
            _working.truncated = true;
        }
        _file_object_depth = 0;
    }

    bool FileListModel::completeString() {
        if (_expecting_key) {
            _key           = _token;
            _expecting_key = false;
        } else if (_file_object_depth != 0 && _key == "name") {
            if (_token_overflow || _token.size() > MaxFileName) {
                _working.truncated = true;
            } else {
                _entry.name     = _token;
                _entry_has_name = true;
            }
            _key.clear();
        } else if (_file_object_depth == 0 && _key == "path") {
            if (_token_overflow || _token.size() > MaxFilePath) {
                _failed = true;
                return false;
            }
            _working.path = _token;
            _saw_path     = true;
            _key.clear();
        } else if (_file_object_depth == 0 && _key == "error") {
            _working.error = _token;
            _key.clear();
        } else {
            _key.clear();
        }
        _token.clear();
        _token_overflow = false;
        return true;
    }

    bool FileListModel::completeNumber() {
        if (_file_object_depth != 0 && _key == "size" && !_token_overflow) {
            char*     end   = nullptr;
            errno           = 0;
            long long value = std::strtoll(_token.c_str(), &end, 10);
            if (errno == ERANGE || end == _token.c_str() || *end != '\0') {
                _failed = true;
                return false;
            }
            _entry.size     = static_cast<int64_t>(value);
            _entry_has_size = true;
        }
        _key.clear();
        _token.clear();
        _token_overflow = false;
        _lex            = LexState::Idle;
        return true;
    }

    bool FileListModel::punctuation(char c) {
        switch (c) {
            case '{':
                ++_object_depth;
                if (!_saw_root) {
                    _saw_root = true;
                } else if (_files_array_depth != 0 && _file_object_depth == 0) {
                    beginFileEntry();
                }
                _expecting_key = true;
                return true;
            case '}':
                if (_file_object_depth == _object_depth) {
                    endFileEntry();
                }
                if (_object_depth <= 0) {
                    return false;
                }
                --_object_depth;
                return true;
            case '[':
                ++_array_depth;
                if (_key == "files") {
                    _files_array_depth = _array_depth;
                    _saw_files         = true;
                    _key.clear();
                }
                return true;
            case ']':
                if (_array_depth <= 0) {
                    return false;
                }
                if (_files_array_depth == _array_depth) {
                    _files_array_depth = 0;
                }
                --_array_depth;
                return true;
            case ',':
                if (_object_depth > 0 && _file_object_depth == 0) {
                    _expecting_key = true;
                } else if (_file_object_depth != 0) {
                    _expecting_key = true;
                }
                return true;
            case ':':
                return !_key.empty();
            default:
                return std::isspace(static_cast<unsigned char>(c)) != 0;
        }
    }

    bool FileListModel::feed(std::string_view chunk) {
        if (_failed || !_snapshot.loading) {
            return false;
        }

        for (char c : chunk) {
            bool again = true;
            while (again) {
                again = false;
                switch (_lex) {
                    case LexState::String:
                        if (c == '\\') {
                            _lex = LexState::Escape;
                        } else if (c == '"') {
                            _lex = LexState::Idle;
                            if (!completeString()) {
                                return false;
                            }
                        } else if (static_cast<unsigned char>(c) < 0x20U) {
                            _failed = true;
                            return false;
                        } else {
                            appendToken(c, MaxFilePath);
                        }
                        break;
                    case LexState::Escape:
                        if (c == 'u') {
                            _lex            = LexState::Unicode;
                            _unicode_digits = 0;
                            appendToken('?', MaxFilePath);
                        } else {
                            switch (c) {
                                case '"':
                                    appendToken('"', MaxFilePath);
                                    break;
                                case '\\':
                                    appendToken('\\', MaxFilePath);
                                    break;
                                case '/':
                                    appendToken('/', MaxFilePath);
                                    break;
                                case 'b':
                                    appendToken('\b', MaxFilePath);
                                    break;
                                case 'f':
                                    appendToken('\f', MaxFilePath);
                                    break;
                                case 'n':
                                    appendToken('\n', MaxFilePath);
                                    break;
                                case 'r':
                                    appendToken('\r', MaxFilePath);
                                    break;
                                case 't':
                                    appendToken('\t', MaxFilePath);
                                    break;
                                default:
                                    _failed = true;
                                    return false;
                            }
                            _lex = LexState::String;
                        }
                        break;
                    case LexState::Unicode:
                        if (!std::isxdigit(static_cast<unsigned char>(c))) {
                            _failed = true;
                            return false;
                        }
                        if (++_unicode_digits == 4) {
                            _lex = LexState::String;
                        }
                        break;
                    case LexState::Number:
                        if (std::isdigit(static_cast<unsigned char>(c))) {
                            appendToken(c, 24);
                        } else {
                            if (!completeNumber()) {
                                return false;
                            }
                            again = true;
                        }
                        break;
                    case LexState::Literal:
                        if (std::isalpha(static_cast<unsigned char>(c))) {
                            appendToken(c, 8);
                        } else {
                            _token.clear();
                            _key.clear();
                            _lex  = LexState::Idle;
                            again = true;
                        }
                        break;
                    case LexState::Idle:
                        if (c == '"') {
                            _token.clear();
                            _token_overflow = false;
                            _lex            = LexState::String;
                        } else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
                            _token.clear();
                            _token_overflow = false;
                            appendToken(c, 24);
                            _lex = LexState::Number;
                        } else if (std::isalpha(static_cast<unsigned char>(c))) {
                            _token.clear();
                            appendToken(c, 8);
                            _lex = LexState::Literal;
                        } else if (!punctuation(c)) {
                            _failed = true;
                            return false;
                        }
                        break;
                }
            }
        }
        return true;
    }

    bool FileListModel::finish() {
        if (_lex == LexState::Number && !completeNumber()) {
            abort("Malformed file-list response");
            return false;
        }
        bool valid = !_failed && _lex == LexState::Idle && _object_depth == 0 && _array_depth == 0 && _saw_root && _saw_files && _saw_path;
        if (!valid) {
            abort("Malformed file-list response");
            return false;
        }

        uint64_t generation  = _snapshot.generation + 1;
        _snapshot            = _working;
        _snapshot.loading    = false;
        _snapshot.valid      = _snapshot.error.empty();
        _snapshot.generation = generation;
        return _snapshot.valid;
    }

    void FileListModel::abort(const char* reason) {
        _snapshot.count = 0;
        _snapshot.path.clear();
        _snapshot.error     = reason ? reason : "File-list response aborted";
        _snapshot.loading   = false;
        _snapshot.valid     = false;
        _snapshot.truncated = false;
        ++_snapshot.generation;
        _failed = true;
    }

}  // namespace TS35
