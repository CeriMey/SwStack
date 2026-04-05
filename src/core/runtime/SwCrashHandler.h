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

/**
 * @file SwCrashHandler.h
 * @ingroup core_runtime
 * @brief Declares helpers that install a best-effort crash capture path for supported platforms.
 *
 * @details
 * `SwCrashHandler` centralizes the minimal infrastructure required to preserve diagnostics after an
 * unexpected process termination. The implementation is intentionally conservative:
 * - it is opt-in through the `SW_CRASH_DUMPS` environment variable,
 * - it computes a writable crash directory under standard application data locations,
 * - it records a marker file describing the most recent crash artifacts,
 * - it uses platform-specific capture primitives to generate the actual report.
 *
 * Windows uses an unhandled-exception filter together with DbgHelp to write a textual stack trace
 * and a minidump. Linux installs signal handlers for common fatal signals and writes a best-effort
 * backtrace to a log file before re-raising the signal with the default handler.
 */

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

/**
 * @struct SwCrashReport
 * @brief Describes the artifacts produced by the most recently persisted crash.
 */
struct SwCrashReport {
    SwString appName;    ///< Application identifier passed to `SwCrashHandler::install()`.
    SwString timestamp;  ///< Timestamp token embedded in generated artifact names.
    SwString crashDir;   ///< Directory that stores marker, log, and dump files.
    SwString logPath;    ///< Path to the human-readable crash log, if one was produced.
    SwString dumpPath;   ///< Path to the platform dump file, if the platform supports it.
};

/**
 * @class SwCrashHandler
 * @brief Static utility that installs crash-report generation for the current process.
 *
 * @details
 * The class does not need to be instantiated. Callers typically invoke `install()` during
 * application startup, after they know the effective application name. Once installed, the handler
 * remembers the target crash directory and exposes `takeLastCrashReport()` so the next process run
 * can surface the previous failure to the user or telemetry pipeline.
 *
 * The API is designed around two principles:
 * - crash capture must never activate implicitly,
 * - normal runtime code should be able to query the last persisted report without having to know
 *   how each platform stores the raw artifacts.
 */
class SwCrashHandler {
public:
    /**
     * @brief Forces crash dumping on for the current process environment.
     *
     * @details
     * Some applications want crash capture enabled regardless of how the parent shell configured
     * `SW_CRASH_DUMPS`. This helper updates the current process environment so a subsequent
     * `install()` call always proceeds with handler registration.
     */
    static void forceEnable() {
#if defined(_WIN32)
        (void)_putenv_s("SW_CRASH_DUMPS", "1");
#else
        (void)setenv("SW_CRASH_DUMPS", "1", 1);
#endif
    }

    /**
     * @brief Returns whether crash dumping is enabled through the environment.
     * @return `true` when `SW_CRASH_DUMPS` contains an enabled value such as `1` or `true`.
     *
     * @details
     * This method is intentionally side-effect free. It only inspects the environment and does not
     * install handlers, allocate directories, or persist files.
     */
    static bool enabledFromEnv() {
        SwString v = envValue_("SW_CRASH_DUMPS");
        if (v.isEmpty()) {
            return false;
        }
        v = v.trimmed().toLower();
        return v == "1" || v == "true" || v == "yes" || v == "on";
    }

    /**
     * @brief Installs the process-wide crash handler for the provided application name.
     * @param appName Logical application name used to compute artifact paths and file names.
     *
     * @details
     * The method stores the normalized application name, resolves the crash directory, and installs
     * the platform-specific hooks only when crash dumping is enabled. Repeated calls are idempotent:
     * once the hooks are installed, later calls keep the stored directory information but do not
     * register duplicate handlers.
     *
     * @note Passing an empty name is treated as a no-op because no stable output location can be
     * derived from it.
     */
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

    /**
     * @brief Returns the directory where crash artifacts are stored for the current application.
     * @return Absolute or platform-native directory path, or an empty string when it cannot be
     *         resolved yet.
     *
     * @details
     * The directory is derived lazily from the installed application name. Standard writable
     * application data locations are preferred, with cache and temporary locations used as fallback.
     */
    static SwString crashDirectory() {
        if (crashDirStorage_().isEmpty() && !appNameStorage_().isEmpty()) {
            crashDirStorage_() = computeCrashDir_(appNameStorage_());
        }
        return crashDirStorage_();
    }

    /**
     * @brief Loads and clears the marker describing the last recorded crash.
     * @param outReport Output structure filled with the parsed metadata on success.
     * @return `true` when a previous crash marker was found and at least one artifact path was
     *         recovered.
     *
     * @details
     * The handler writes a lightweight marker file next to the crash artifacts so the next process
     * invocation can discover them without scanning the crash directory. This method parses that
     * marker, populates `outReport`, then removes the marker so the same report is not emitted
     * repeatedly on subsequent launches.
     */
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
    /**
     * @brief Reads an environment variable into a `SwString`.
     * @param name Environment variable name.
     * @return Variable value, or an empty string when the variable is absent.
     */
    static SwString envValue_(const char* name) {
        if (!name || !*name) {
            return SwString();
        }
#if defined(_WIN32)
        char* value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, name) != 0 || !value) {
            return SwString();
        }
        SwString result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name);
        return value ? SwString(value) : SwString();
#endif
    }

    /**
     * @brief Returns the process-wide storage slot containing the installed application name.
     */
    static SwString& appNameStorage_() {
        static SwString s_appName;
        return s_appName;
    }

    /**
     * @brief Returns the process-wide storage slot containing the resolved crash directory.
     */
    static SwString& crashDirStorage_() {
        static SwString s_crashDir;
        return s_crashDir;
    }

    /**
     * @brief Returns the process-wide flag indicating whether handlers were already installed.
     */
    static bool& installedStorage_() {
        static bool s_installed = false;
        return s_installed;
    }

    /**
     * @brief Computes the crash artifact directory for a given application name.
     * @param appName Logical application name previously passed to `install()`.
     * @return Platform-native crash directory path, or an empty string when none can be resolved.
     */
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

    /**
     * @brief Returns a timestamp token suitable for crash artifact file names.
     * @return Timestamp formatted as `YYYYMMDD_hhmmss`.
     */
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

    /**
     * @brief Returns the path of the marker file used to advertise the most recent crash.
     */
    static SwString markerFilePath_() {
        return joinPath_(crashDirectory(), "last_crash.txt");
    }

    /**
     * @brief Joins a directory and file name using the current platform path style.
     * @param dir Base directory.
     * @param fileName Leaf file name.
     * @return Joined path.
     */
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

    /**
     * @brief Reads a whole file into memory.
     * @param path File path to load.
     * @param out Output string receiving the file bytes.
     * @return `true` on success, `false` when the file cannot be opened.
     */
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

    /**
     * @brief Removes a file without propagating deletion errors.
     * @param path File path to delete.
     */
    static void removeFile_(const SwString& path) {
#if defined(_WIN32)
        const std::wstring wpath = path.toStdWString();
        (void)::DeleteFileW(wpath.c_str());
#else
        (void)std::remove(path.toStdString().c_str());
#endif
    }

    /**
     * @brief Persists the marker consumed by `takeLastCrashReport()`.
     * @param timestamp Crash timestamp token.
     * @param logPath Path to the textual crash log.
     * @param dumpPath Path to the platform dump file, when one exists.
     */
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
    /**
     * @brief Writes a symbolic stack trace into the crash log on Windows.
     * @param f Output stream already opened on the log file.
     * @param ep Structured exception information provided by Windows.
     */
    static void writeStackTrace_(std::FILE* f, EXCEPTION_POINTERS* ep) {
        if (!f || !ep || !ep->ContextRecord) {
            return;
        }

        HANDLE process = ::GetCurrentProcess();
        HANDLE thread = ::GetCurrentThread();

        ::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
        const BOOL symInitializedHere = ::SymInitialize(process, nullptr, TRUE);
        const DWORD symInitError = symInitializedHere ? ERROR_SUCCESS : ::GetLastError();
        if (!symInitializedHere && symInitError != ERROR_INVALID_PARAMETER) {
            std::fprintf(f, "SymInitialize failed: %lu\n", static_cast<unsigned long>(symInitError));
            return;
        }
        (void)::SymRefreshModuleList(process);

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

        if (symInitializedHere) {
            ::SymCleanup(process);
        }
    }

    /**
     * @brief Writes a Windows minidump beside the textual crash log.
     * @param dumpPath Destination dump file path.
     * @param ep Structured exception information provided by Windows.
     */
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

    /**
     * @brief Process-wide unhandled-exception callback installed on Windows.
     * @param ep Structured exception information describing the crash.
     * @return `EXCEPTION_EXECUTE_HANDLER` after best-effort artifact generation.
     */
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
    /**
     * @brief Performs a best-effort blocking write on a signal-safe file descriptor.
     * @param fd Open file descriptor.
     * @param data Byte buffer to write.
     * @param size Number of bytes to write.
     */
    static void writeBestEffort_(int fd, const char* data, size_t size) {
        size_t offset = 0;
        while (offset < size) {
            const ssize_t written = ::write(fd, data + offset, size - offset);
            if (written <= 0) {
                return;
            }
            offset += static_cast<size_t>(written);
        }
    }

    /**
     * @brief Installs handlers for the fatal Unix signals monitored by the crash handler.
     */
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

    /**
     * @brief Signal handler that writes a backtrace log, stores the marker, and re-raises.
     * @param sig Fatal signal number.
     */
    static void signalHandler_(int sig) {
        const SwString stamp = timestampNow_();
        const SwString base = crashDirectory();
        const SwString logPath = joinPath_(base, appNameStorage_() + "_crash_" + stamp + ".log");

        int fd = ::open(logPath.toStdString().c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) {
            const std::string header = appNameStorage_().toStdString() + " crash (signal " + std::to_string(sig) + ")\n";
            writeBestEffort_(fd, header.c_str(), header.size());

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
