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

#include "core/fs/SwDir.h"
#include "core/fs/SwStandardPaths.h"
#include "core/types/SwString.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

#if defined(__linux__)
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

struct SwCrashReport {
    SwString appName;
    SwString timestamp;
    SwString crashDir;
    SwString logPath;
    SwString dumpPath;
};

class SwCrashHandler {
public:
    static bool enabledFromEnv() {
        const char* env = std::getenv("SW_CRASH_DUMPS");
        if (!env) {
            return false;
        }
        SwString v(env);
        v = v.trimmed().toLower();
        return v == "1" || v == "true" || v == "yes" || v == "on";
    }

    static void install(const SwString& appName) {
        if (appName.trimmed().isEmpty()) {
            return;
        }

        appNameStorage_() = appName.trimmed();
        crashDirStorage_() = computeCrashDir_(appNameStorage_());

        if (!enabledFromEnv()) {
            return;
        }

        if (crashDirStorage_().isEmpty()) {
            return;
        }

        if (installedStorage_()) {
            return;
        }
        installedStorage_() = true;

        SwString dirForCreate = crashDirStorage_();
        dirForCreate.replace("\\", "/");
        (void)SwDir::mkpathAbsolute(dirForCreate, false);

#if defined(_WIN32)
        ::SetUnhandledExceptionFilter(&unhandledExceptionFilter_);
#elif defined(__linux__)
        installSignalHandlers_();
#endif
    }

    static SwString crashDirectory() {
        if (crashDirStorage_().isEmpty() && !appNameStorage_().isEmpty()) {
            crashDirStorage_() = computeCrashDir_(appNameStorage_());
        }
        return crashDirStorage_();
    }

    static bool takeLastCrashReport(SwCrashReport& outReport) {
        outReport = SwCrashReport{};

        const SwString appName = appNameStorage_();
        if (appName.isEmpty()) {
            return false;
        }
        const SwString crashDir = crashDirectory();
        if (crashDir.isEmpty()) {
            return false;
        }

        const SwString markerPath = markerFilePath_();
        std::string contents;
        if (!readAllTextFile_(markerPath, contents)) {
            return false;
        }

        outReport.appName = appName;
        outReport.crashDir = crashDir;

        const SwString text(contents);
        const SwList<SwString> lines = text.split("\n");
        for (const SwString& rawLine : lines) {
            const SwString line = rawLine.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            const size_t eq = line.toStdString().find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const SwString key = line.left(static_cast<int>(eq)).trimmed().toLower();
            const SwString value = line.mid(static_cast<int>(eq + 1)).trimmed();
            if (key == "time") {
                outReport.timestamp = value;
            } else if (key == "log") {
                outReport.logPath = value;
            } else if (key == "dump") {
                outReport.dumpPath = value;
            }
        }

        removeFile_(markerPath);
        return !outReport.logPath.isEmpty() || !outReport.dumpPath.isEmpty();
    }

private:
    static SwString& appNameStorage_() {
        static SwString s_appName;
        return s_appName;
    }

    static SwString& crashDirStorage_() {
        static SwString s_crashDir;
        return s_crashDir;
    }

    static bool& installedStorage_() {
        static bool s_installed = false;
        return s_installed;
    }

    static SwString computeCrashDir_(const SwString& appName) {
        SwString base = SwStandardPaths::writableLocation(SwStandardPaths::AppLocalDataLocation);
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::CacheLocation);
        }
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::TempLocation);
        }
        if (base.isEmpty()) {
            return SwString();
        }

        base.replace("\\", "/");
        if (!base.endsWith("/")) {
            base += "/";
        }

        SwString dir = base + "SwCrashDumps/" + appName;
#if defined(_WIN32)
        dir.replace("/", "\\");
#endif
        return dir;
    }

    static SwString timestampNow_() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char stamp[64] = {};
        std::snprintf(stamp,
                      sizeof(stamp),
                      "%04d%02d%02d_%02d%02d%02d",
                      tm.tm_year + 1900,
                      tm.tm_mon + 1,
                      tm.tm_mday,
                      tm.tm_hour,
                      tm.tm_min,
                      tm.tm_sec);
        return SwString(stamp);
    }

    static SwString markerFilePath_() {
        return joinPath_(crashDirectory(), "last_crash.txt");
    }

    static SwString joinPath_(const SwString& dir, const SwString& fileName) {
        if (dir.isEmpty()) {
            return fileName;
        }
        if (fileName.isEmpty()) {
            return dir;
        }

        SwString out = dir;
        if (!out.endsWith("/") && !out.endsWith("\\")) {
            out += "/";
        }
        out += fileName;
#if defined(_WIN32)
        out.replace("/", "\\");
#endif
        return out;
    }

    static bool readAllTextFile_(const SwString& path, std::string& out) {
        out.clear();

#if defined(_WIN32)
        std::FILE* f = nullptr;
        const std::wstring wpath = path.toStdWString();
        if (_wfopen_s(&f, wpath.c_str(), L"rb") != 0 || !f) {
            return false;
        }
#else
        std::FILE* f = std::fopen(path.toStdString().c_str(), "rb");
        if (!f) {
            return false;
        }
#endif

        char buf[4096];
        while (true) {
            const size_t n = std::fread(buf, 1, sizeof(buf), f);
            if (n > 0) {
                out.append(buf, n);
            }
            if (n < sizeof(buf)) {
                break;
            }
        }
        std::fclose(f);
        return true;
    }

    static void removeFile_(const SwString& path) {
#if defined(_WIN32)
        const std::wstring wpath = path.toStdWString();
        (void)::DeleteFileW(wpath.c_str());
#else
        (void)std::remove(path.toStdString().c_str());
#endif
    }

    static void writeMarkerFile_(const SwString& timestamp,
                                 const SwString& logPath,
                                 const SwString& dumpPath) {
        const SwString path = markerFilePath_();

#if defined(_WIN32)
        std::FILE* f = nullptr;
        const std::wstring wpath = path.toStdWString();
        if (_wfopen_s(&f, wpath.c_str(), L"wb") != 0 || !f) {
            return;
        }
#else
        std::FILE* f = std::fopen(path.toStdString().c_str(), "wb");
        if (!f) {
            return;
        }
#endif

        std::fprintf(f, "app=%s\n", appNameStorage_().toStdString().c_str());
        std::fprintf(f, "time=%s\n", timestamp.toStdString().c_str());
        if (!logPath.isEmpty()) {
            std::fprintf(f, "log=%s\n", logPath.toStdString().c_str());
        }
        if (!dumpPath.isEmpty()) {
            std::fprintf(f, "dump=%s\n", dumpPath.toStdString().c_str());
        }
        std::fclose(f);
    }

#if defined(_WIN32)
    static void writeStackTrace_(std::FILE* f, EXCEPTION_POINTERS* ep) {
        if (!f || !ep || !ep->ContextRecord) {
            return;
        }

        HANDLE process = ::GetCurrentProcess();
        HANDLE thread = ::GetCurrentThread();

        ::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
        if (!::SymInitialize(process, nullptr, TRUE)) {
            std::fprintf(f, "SymInitialize failed: %lu\n", ::GetLastError());
            return;
        }

        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 frame{};
        DWORD machine = 0;

#if defined(_M_X64)
        machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = ctx.Rip;
        // x64 builds (especially Release) often omit frame pointers (RBP),
        // so use RSP as the frame base to allow StackWalk64 to proceed.
        frame.AddrFrame.Offset = ctx.Rsp;
        frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_IX86)
        machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = ctx.Eip;
        frame.AddrFrame.Offset = ctx.Ebp;
        frame.AddrStack.Offset = ctx.Esp;
#else
        std::fprintf(f, "Unsupported architecture for stack trace.\n");
        ::SymCleanup(process);
        return;
#endif

        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        std::fprintf(f, "\nStack trace:\n");

        for (int i = 0; i < 64; ++i) {
            if (!::StackWalk64(machine,
                               process,
                               thread,
                               &frame,
                               &ctx,
                               nullptr,
                               ::SymFunctionTableAccess64,
                               ::SymGetModuleBase64,
                               nullptr)) {
                break;
            }
            if (frame.AddrPC.Offset == 0) {
                break;
            }

            const DWORD64 addr = frame.AddrPC.Offset;

            char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;

            DWORD64 displacement = 0;
            const BOOL haveSymbol = ::SymFromAddr(process, addr, &displacement, symbol);

            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisp = 0;
            const BOOL haveLine = ::SymGetLineFromAddr64(process, addr, &lineDisp, &line);

            if (haveSymbol && haveLine) {
                std::fprintf(f,
                             "  #%02d  %s + 0x%llx  (%s:%lu)\n",
                             i,
                             symbol->Name,
                             static_cast<unsigned long long>(displacement),
                             line.FileName ? line.FileName : "?",
                             line.LineNumber);
            } else if (haveSymbol) {
                std::fprintf(f,
                             "  #%02d  %s + 0x%llx  (0x%llx)\n",
                             i,
                             symbol->Name,
                             static_cast<unsigned long long>(displacement),
                             static_cast<unsigned long long>(addr));
            } else {
                std::fprintf(f, "  #%02d  0x%llx\n", i, static_cast<unsigned long long>(addr));
            }
        }

        ::SymCleanup(process);
    }

    static void writeMiniDump_(const std::wstring& dumpPath, EXCEPTION_POINTERS* ep) {
        if (!ep) {
            return;
        }

        HANDLE hFile = ::CreateFileW(dumpPath.c_str(),
                                     GENERIC_WRITE,
                                     0,
                                     nullptr,
                                     CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL,
                                     nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            return;
        }

        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = ::GetCurrentThreadId();
        info.ExceptionPointers = ep;
        info.ClientPointers = FALSE;

        (void)::MiniDumpWriteDump(::GetCurrentProcess(),
                                  ::GetCurrentProcessId(),
                                  hFile,
                                  static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs),
                                  &info,
                                  nullptr,
                                  nullptr);
        ::CloseHandle(hFile);
    }

    static LONG WINAPI unhandledExceptionFilter_(EXCEPTION_POINTERS* ep) {
        const SwString stamp = timestampNow_();
        const SwString base = crashDirectory();

        const SwString logPath = joinPath_(base, appNameStorage_() + "_crash_" + stamp + ".log");
        const SwString dumpPath = joinPath_(base, appNameStorage_() + "_crash_" + stamp + ".dmp");

        std::FILE* f = nullptr;
        const std::wstring wlog = logPath.toStdWString();
        _wfopen_s(&f, wlog.c_str(), L"wb");
        if (f) {
            std::fprintf(f, "%s crash\n", appNameStorage_().toStdString().c_str());
            if (ep && ep->ExceptionRecord) {
                std::fprintf(f,
                             "Exception code: 0x%08lx\nException address: 0x%p\n",
                             ep->ExceptionRecord->ExceptionCode,
                             ep->ExceptionRecord->ExceptionAddress);
            }
            writeStackTrace_(f, ep);
            std::fclose(f);
        }

        writeMiniDump_(dumpPath.toStdWString(), ep);
        writeMarkerFile_(stamp, logPath, dumpPath);
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif

#if defined(__linux__)
    static void installSignalHandlers_() {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = &signalHandler_;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;

        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGFPE, &sa, nullptr);
        sigaction(SIGILL, &sa, nullptr);
        sigaction(SIGBUS, &sa, nullptr);
    }

    static void signalHandler_(int sig) {
        const SwString stamp = timestampNow_();
        const SwString base = crashDirectory();
        const SwString logPath = joinPath_(base, appNameStorage_() + "_crash_" + stamp + ".log");

        int fd = ::open(logPath.toStdString().c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) {
            const std::string header = appNameStorage_().toStdString() + " crash (signal " + std::to_string(sig) + ")\n";
            (void)::write(fd, header.c_str(), header.size());

            void* frames[64];
            int n = ::backtrace(frames, 64);
            ::backtrace_symbols_fd(frames, n, fd);
            ::close(fd);
        }

        writeMarkerFile_(stamp, logPath, SwString());

        // Re-raise with default handler.
        ::signal(sig, SIG_DFL);
        ::raise(sig);
    }
#endif
};
