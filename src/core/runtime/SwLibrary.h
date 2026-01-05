#pragma once
/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwDir.h"
#include "SwJsonArray.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwString.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <sys/stat.h>
#endif

class SwLibrary {
 public:
    struct LoadAttempt {
        SwString path;
        bool ok{false};
        SwString error;
        int nativeError{0};
    };

    struct SymbolLookup {
        SwString symbol;
        bool ok{false};
        std::uintptr_t address{0};
    };

    SwLibrary() = default;
    explicit SwLibrary(const SwString& path) { (void)load(path); }

    SwLibrary(const SwLibrary&) = delete;
    SwLibrary& operator=(const SwLibrary&) = delete;

    SwLibrary(SwLibrary&& other) noexcept { moveFrom_(other); }
    SwLibrary& operator=(SwLibrary&& other) noexcept {
        if (this == &other) return *this;
        unload();
        moveFrom_(other);
        return *this;
    }

    ~SwLibrary() { unload(); }

    bool isLoaded() const { return handle_ != nullptr; }
    void* nativeHandle() const { return handle_; }
    const SwString& requestedPath() const { return requestedPath_; }
    const SwString& path() const { return path_; }
    const SwString& lastError() const { return lastError_; }
    const std::vector<LoadAttempt>& loadAttempts() const { return loadAttempts_; }
    const std::vector<SymbolLookup>& symbolLookups() const { return symbolLookups_; }

    static SwString platformPrefix() {
#ifdef _WIN32
        return SwString();
#else
        return SwString("lib");
#endif
    }

    static SwString platformSuffix() {
#ifdef _WIN32
        return SwString(".dll");
#elif defined(__APPLE__)
        return SwString(".dylib");
#else
        return SwString(".so");
#endif
    }

    bool load(const SwString& path) {
        unload();
        lastError_ = SwString();
        requestedPath_ = path;
        loadAttempts_.clear();
        symbolLookups_.clear();

        if (path.isEmpty()) {
            lastError_ = SwString("SwLibrary: empty path");
            return false;
        }

        const std::vector<SwString> candidates = resolvePathCandidates_(path);
        if (candidates.empty()) {
            lastError_ = SwString("SwLibrary: no candidate paths");
            return false;
        }

        for (size_t i = 0; i < candidates.size(); ++i) {
            const SwString& resolved = candidates[i];
            const std::string resolvedStd = resolved.toStdString();

            LoadAttempt attempt;
            attempt.path = resolved;

#ifdef _WIN32
            ::SetLastError(0);
            HMODULE h = ::LoadLibraryA(resolvedStd.c_str());
            if (!h) {
                const DWORD code = ::GetLastError();
                attempt.ok = false;
                attempt.nativeError = static_cast<int>(code);
                attempt.error = SwString("LoadLibraryA failed: ") + formatWin32Error_(code);
                loadAttempts_.push_back(attempt);
                lastError_ = SwString("SwLibrary: LoadLibraryA(\"") + resolved + SwString("\") failed: ") +
                             formatWin32Error_(code);
                continue;
            }
            handle_ = reinterpret_cast<void*>(h);
#else
            (void)::dlerror(); // clear
            void* h = ::dlopen(resolvedStd.c_str(), RTLD_LAZY | RTLD_LOCAL);
            if (!h) {
                const char* e = ::dlerror();
                attempt.ok = false;
                attempt.nativeError = 0;
                attempt.error = SwString("dlopen failed: ") + SwString(e ? e : "unknown");
                loadAttempts_.push_back(attempt);
                lastError_ = SwString("SwLibrary: dlopen(\"") + resolved + SwString("\") failed: ") +
                             SwString(e ? e : "unknown");
                continue;
            }
            handle_ = h;
#endif

            attempt.ok = true;
            attempt.error = SwString();
            loadAttempts_.push_back(attempt);
            path_ = resolved;
            return true;
        }

        return false;
    }

    void unload() {
        if (!handle_) return;
#ifdef _WIN32
        HMODULE h = reinterpret_cast<HMODULE>(handle_);
        ::FreeLibrary(h);
#else
        ::dlclose(handle_);
#endif
        handle_ = nullptr;
        path_ = SwString();
    }

    void* resolve(const SwString& symbol) const {
        SymbolLookup lookup;
        lookup.symbol = symbol;

        if (!handle_ || symbol.isEmpty()) {
            symbolLookups_.push_back(lookup);
            return nullptr;
        }
        const std::string s = symbol.toStdString();
#ifdef _WIN32
        HMODULE h = reinterpret_cast<HMODULE>(handle_);
        void* addr = reinterpret_cast<void*>(::GetProcAddress(h, s.c_str()));
        lookup.ok = (addr != nullptr);
        lookup.address = reinterpret_cast<std::uintptr_t>(addr);
        symbolLookups_.push_back(lookup);
        return addr;
#else
        (void)::dlerror(); // clear
        void* addr = ::dlsym(handle_, s.c_str());
        const char* e = ::dlerror();
        lookup.ok = (e == nullptr && addr != nullptr);
        lookup.address = reinterpret_cast<std::uintptr_t>(addr);
        symbolLookups_.push_back(lookup);
        return addr;
#endif
    }

    SwJsonObject introspectionJson() const {
        SwJsonObject o;

        o["loaded"] = SwJsonValue(isLoaded());
        o["requestedPath"] = SwJsonValue(requestedPath_.toStdString());
        o["path"] = SwJsonValue(path_.toStdString());
        o["lastError"] = SwJsonValue(lastError_.toStdString());
        o["platformPrefix"] = SwJsonValue(platformPrefix().toStdString());
        o["platformSuffix"] = SwJsonValue(platformSuffix().toStdString());

        const std::uintptr_t h = reinterpret_cast<std::uintptr_t>(handle_);
        o["nativeHandle"] = SwJsonValue(static_cast<long long>(reinterpret_cast<std::intptr_t>(handle_)));
        o["nativeHandleHex"] = SwJsonValue(ptrHex_(h).toStdString());

        // File info (best effort).
        if (!path_.isEmpty()) {
            uint64_t fileSize = 0;
            uint64_t mtime = 0;
            const bool ok = fileInfo_(path_, &fileSize, &mtime);
            SwJsonObject f;
            f["exists"] = SwJsonValue(ok);
            if (ok) {
                f["size"] = SwJsonValue(static_cast<long long>(fileSize));
                f["mtime"] = SwJsonValue(static_cast<long long>(mtime));
            }
            o["file"] = SwJsonValue(f);
        }

        SwJsonArray attempts;
        for (size_t i = 0; i < loadAttempts_.size(); ++i) {
            const LoadAttempt& a = loadAttempts_[i];
            SwJsonObject ao;
            ao["path"] = SwJsonValue(a.path.toStdString());
            ao["ok"] = SwJsonValue(a.ok);
            if (!a.error.isEmpty()) ao["error"] = SwJsonValue(a.error.toStdString());
            if (a.nativeError != 0) ao["nativeError"] = SwJsonValue(static_cast<long long>(a.nativeError));
            attempts.append(SwJsonValue(ao));
        }
        o["loadAttempts"] = SwJsonValue(attempts);

        SwJsonArray syms;
        for (size_t i = 0; i < symbolLookups_.size(); ++i) {
            const SymbolLookup& s = symbolLookups_[i];
            SwJsonObject so;
            so["symbol"] = SwJsonValue(s.symbol.toStdString());
            so["ok"] = SwJsonValue(s.ok);
            so["address"] = SwJsonValue(ptrHex_(s.address).toStdString());
            syms.append(SwJsonValue(so));
        }
        o["symbolLookups"] = SwJsonValue(syms);

        return o;
    }

 private:
    static SwString normalizeSlashes_(SwString p) {
        p = p.trimmed();
        p.replace("\\", "/");
        while (p.contains("//")) p.replace("//", "/");
        return p;
    }

    static bool isAbs_(const SwString& p) {
        if (p.isEmpty()) return false;
        if (p.startsWith("/") || p.startsWith("\\")) return true;
        return (p.size() >= 2 && p[1] == ':');
    }

    static bool hasAnySuffix_(const SwString& p) {
        const std::string s = p.toStdString();
        const size_t dot = s.find_last_of('.');
        const size_t slash = s.find_last_of("/\\");
        return (dot != std::string::npos) && (slash == std::string::npos || dot > slash);
    }

    static SwString addPrefixToLeaf_(const SwString& path, const SwString& prefix) {
        if (path.isEmpty() || prefix.isEmpty()) return path;

        const SwString p = normalizeSlashes_(path);
        const std::string s = p.toStdString();
        const size_t slash = s.find_last_of('/');
        if (slash == std::string::npos) {
            if (p.startsWith(prefix)) return p;
            return prefix + p;
        }

        const SwString dir(s.substr(0, slash));
        const SwString leaf(s.substr(slash + 1));
        if (leaf.isEmpty() || leaf.startsWith(prefix)) return p;
        return dir + "/" + prefix + leaf;
    }

    static void pushUnique_(std::vector<SwString>& out, const SwString& v) {
        for (size_t i = 0; i < out.size(); ++i) {
            if (out[i] == v) return;
        }
        out.push_back(v);
    }

    static SwString joinCwd_(const SwString& rel) {
        // Resolve relative paths from current working directory.
        SwString cwd = SwDir::currentPath();
        cwd = normalizeSlashes_(cwd);
        while (cwd.endsWith("/")) cwd = cwd.left(static_cast<int>(cwd.size()) - 1);
        if (cwd.isEmpty()) return rel;
        return cwd + "/" + rel;
    }

    static std::vector<SwString> resolvePathCandidates_(const SwString& input) {
        std::vector<SwString> out;
        SwString p = normalizeSlashes_(input);
        if (p.isEmpty()) return out;

        // Build name variants: as-is, and with platform prefix injected into the leaf (best-effort).
        std::vector<SwString> names;
        pushUnique_(names, p);
        const SwString prefix = platformPrefix();
        if (!prefix.isEmpty()) {
            const SwString withPrefix = addPrefixToLeaf_(p, prefix);
            pushUnique_(names, withPrefix);
        }

        // Ensure platform suffix where missing.
        std::vector<SwString> suffixed;
        for (size_t i = 0; i < names.size(); ++i) {
            SwString x = names[i];
            if (!hasAnySuffix_(x)) x += platformSuffix();
            pushUnique_(suffixed, x);
        }

        // Make absolute if needed.
        for (size_t i = 0; i < suffixed.size(); ++i) {
            const SwString& x = suffixed[i];
            if (isAbs_(x)) {
                pushUnique_(out, x);
            } else {
                pushUnique_(out, joinCwd_(x));
            }
        }

        return out;
    }

    static SwString ptrHex_(std::uintptr_t v) {
        std::ostringstream os;
        os << "0x" << std::hex << std::setw(sizeof(void*) * 2) << std::setfill('0') << v;
        return SwString(os.str());
    }

    static bool fileInfo_(const SwString& path, uint64_t* sizeOut, uint64_t* mtimeOut) {
        if (sizeOut) *sizeOut = 0;
        if (mtimeOut) *mtimeOut = 0;
        if (path.isEmpty()) return false;

#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!::GetFileAttributesExA(path.toStdString().c_str(), GetFileExInfoStandard, &info)) {
            return false;
        }
        if (sizeOut) {
            ULARGE_INTEGER ui;
            ui.LowPart = info.nFileSizeLow;
            ui.HighPart = info.nFileSizeHigh;
            *sizeOut = static_cast<uint64_t>(ui.QuadPart);
        }
        if (mtimeOut) {
            *mtimeOut = fileTimeToUnixSeconds_(info.ftLastWriteTime);
        }
        return true;
#else
        struct stat st;
        if (::stat(path.toStdString().c_str(), &st) != 0) {
            return false;
        }
        if (sizeOut) *sizeOut = static_cast<uint64_t>(st.st_size);
        if (mtimeOut) *mtimeOut = static_cast<uint64_t>(st.st_mtime);
        return true;
#endif
    }

#ifdef _WIN32
    static uint64_t fileTimeToUnixSeconds_(const FILETIME& ft) {
        ULARGE_INTEGER ui;
        ui.LowPart = ft.dwLowDateTime;
        ui.HighPart = ft.dwHighDateTime;
        // FILETIME is 100ns since 1601-01-01.
        const uint64_t ticks = static_cast<uint64_t>(ui.QuadPart);
        const uint64_t kTicksPerSec = 10000000ULL;
        const uint64_t kEpochDiffSec = 11644473600ULL; // 1601->1970
        const uint64_t secs = ticks / kTicksPerSec;
        if (secs < kEpochDiffSec) return 0;
        return secs - kEpochDiffSec;
    }

    static SwString formatWin32Error_(DWORD code) {
        if (code == 0) return SwString("0");

        LPSTR msg = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD len =
            ::FormatMessageA(flags, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             reinterpret_cast<LPSTR>(&msg), 0, nullptr);
        if (len == 0 || !msg) {
            return SwString::number(static_cast<int>(code));
        }

        std::string s(msg, msg + len);
        ::LocalFree(msg);

        // Trim trailing CR/LF/spaces.
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
            s.pop_back();
        }

        return SwString::number(static_cast<int>(code)) + SwString(" ") + SwString(s);
    }
#endif

    void moveFrom_(SwLibrary& other) {
        handle_ = other.handle_;
        requestedPath_ = other.requestedPath_;
        path_ = other.path_;
        lastError_ = other.lastError_;
        loadAttempts_ = other.loadAttempts_;
        symbolLookups_ = other.symbolLookups_;
        other.handle_ = nullptr;
        other.requestedPath_ = SwString();
        other.path_ = SwString();
        other.lastError_ = SwString();
        other.loadAttempts_.clear();
        other.symbolLookups_.clear();
    }

    void* handle_{nullptr};
    SwString requestedPath_{};
    SwString path_{};
    SwString lastError_{};
    std::vector<LoadAttempt> loadAttempts_{};
    mutable std::vector<SymbolLookup> symbolLookups_{};
};
