#pragma once

/**
 * @file src/core/io/SwProcess.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwProcess in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the process interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are ProcessFlags and SwProcess.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
 *
 */

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

#include "SwIODevice.h"
#include "SwDebug.h"
#include "SwByteArray.h"
#include "SwList.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

static constexpr const char* kSwLogCategory_SwProcess = "sw.core.io.swprocess";

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <shellapi.h>
enum class ProcessFlags : DWORD {
    NoFlag = 0,
    CreateNoWindow = CREATE_NO_WINDOW,
    CreateNewConsole = CREATE_NEW_CONSOLE,
    Detached = DETACHED_PROCESS,
    Suspended = CREATE_SUSPENDED,
    RunAsAdmin = 0x80000000  // Use ShellExecuteEx with "runas" verb
};

inline ProcessFlags operator|(ProcessFlags a, ProcessFlags b) {
    return static_cast<ProcessFlags>(static_cast<DWORD>(a) | static_cast<DWORD>(b));
}

inline ProcessFlags& operator|=(ProcessFlags& a, ProcessFlags b) {
    a = a | b;
    return a;
}
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
enum class ProcessFlags : int {
    NoFlag = 0,
    CreateNoWindow = 1 << 0,
    CreateNewConsole = 1 << 1,
    Detached = 1 << 2,
    Suspended = 1 << 3
};

inline ProcessFlags operator|(ProcessFlags a, ProcessFlags b) noexcept {
    return static_cast<ProcessFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline ProcessFlags& operator|=(ProcessFlags& a, ProcessFlags b) noexcept {
    a = a | b;
    return a;
}
#endif

/**
 * @class SwProcess
 * @brief The SwProcess class provides an interface for managing system processes with redirected IO.
 *
 * The implementation is fully asynchronous without timer-based polling:
 * - blocking readers are dedicated to stdout/stderr
 * - a dedicated waiter observes process termination
 * - all public signals are posted back to the object's affinity thread
 */
class SwProcess : public SwIODevice {
    SW_OBJECT(SwProcess, SwIODevice)
public:
    SwProcess(SwObject* parent = nullptr)
        : SwIODevice(parent) {
#if !defined(_WIN32)
        stdoutPipe_[0] = stdoutPipe_[1] = -1;
        stderrPipe_[0] = stderrPipe_[1] = -1;
        stdinPipe_[0] = stdinPipe_[1] = -1;
#endif
    }

    virtual ~SwProcess() {
        if (isOpen()) {
            close();
        }
        callbackGuard_.reset();
    }

    bool start(const SwString& program, const SwStringList& arguments = {},
               ProcessFlags flags = ProcessFlags::NoFlag,
               const SwString& workingDirectory = "") {
        if (isOpen()) {
            swCWarning(kSwLogCategory_SwProcess) << "Process already running!";
            return false;
        }

        resetRuntimeState_();
        activeFlags_ = flags;

        if (shouldCreatePipes_(flags)) {
            if (!createPipes_()) {
                swCError(kSwLogCategory_SwProcess) << "Failed to create pipes!";
                activeFlags_ = ProcessFlags::NoFlag;
                cleanupHandles_();
                return false;
            }
        }

        if (!startProcess_(program, arguments, flags, workingDirectory)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to start process!";
            activeFlags_ = ProcessFlags::NoFlag;
            cleanupHandles_();
            return false;
        }

        m_program = program;
        m_arguments = arguments;
        m_workingDirectory = workingDirectory;

        processRunning_.store(true, std::memory_order_release);
        processExited_.store(false, std::memory_order_release);
        exitNotificationEnabled_.store(true, std::memory_order_release);

        const uint64_t generation =
            asyncGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
        startAsyncRuntime_(generation);

        emit deviceOpened();
        return true;
    }

    bool start(ProcessFlags flags = ProcessFlags::NoFlag) {
        if (m_program.isEmpty()) {
            swCError(kSwLogCategory_SwProcess) << "Program is not set!";
            return false;
        }
        return start(m_program, m_arguments, flags, m_workingDirectory);
    }

    void closeStdIn() {
        closeStdInHandle_();
    }

    void setProgram(const SwString& program) { m_program = program; }
    SwString program() const { return m_program; }

    void setArguments(const SwStringList& arguments) { m_arguments = arguments; }
    SwStringList arguments() const { return m_arguments; }

    void setWorkingDirectory(const SwString& dir) { m_workingDirectory = dir; }
    SwString workingDirectory() const { return m_workingDirectory; }

    void close() override {
        shutdown_(StopMode::Close);
    }

    void release() {
        shutdown_(StopMode::Close, true);
    }

    bool isOpen() const override {
        return processRunning_.load(std::memory_order_acquire);
    }

    SwString read(int64_t maxSize = 0) override {
        return takeBuffer_(stdoutBuffer_, stdoutMutex_, maxSize);
    }

    SwString readStdErr(int64_t maxSize = 0) {
        return takeBuffer_(stderrBuffer_, stderrMutex_, maxSize);
    }

    bool write(const SwString& data) override {
        return writeStdIn_(data);
    }

public slots:
    void kill() {
        shutdown_(StopMode::Kill);
    }

    void terminate() {
        shutdown_(StopMode::Terminate);
    }

signals:
    DECLARE_SIGNAL_VOID(deviceOpened)
    DECLARE_SIGNAL_VOID(deviceClosed)
    DECLARE_SIGNAL_VOID(processFinished)
    DECLARE_SIGNAL(processTerminated, int)
    DECLARE_SIGNAL_VOID(readyReadStdOut)
    DECLARE_SIGNAL_VOID(readyReadStdErr)

private:
    enum class StreamKind {
        StdOut,
        StdErr
    };

    enum class StopMode {
        Close,
        Terminate,
        Kill
    };

    std::atomic<bool> processRunning_{false};
    std::atomic<bool> processExited_{false};
    std::atomic<bool> exitNotificationEnabled_{false};
    std::atomic<bool> stdoutSignalPending_{false};
    std::atomic<bool> stderrSignalPending_{false};
    std::atomic<uint64_t> asyncGeneration_{0};
    std::shared_ptr<int> callbackGuard_{std::make_shared<int>(0)};

    SwString m_program;
    SwStringList m_arguments;
    SwString m_workingDirectory;

    std::thread stdoutThread_;
    std::thread stderrThread_;
    std::thread waitThread_;

    std::mutex stdoutMutex_;
    std::mutex stderrMutex_;
    std::mutex stdinMutex_;

    SwByteArray stdoutBuffer_;
    SwByteArray stderrBuffer_;
    ProcessFlags activeFlags_{ProcessFlags::NoFlag};

#if defined(_WIN32)
    HANDLE hProcess_{NULL};
    HANDLE hThread_{NULL};
    HANDLE hJob_{NULL};
    HANDLE hStdOutRead_{NULL};
    HANDLE hStdOutWrite_{NULL};
    HANDLE hStdErrRead_{NULL};
    HANDLE hStdErrWrite_{NULL};
    HANDLE hStdInWrite_{NULL};
    HANDLE hStdInRead_{NULL};
#else
    pid_t childPid_{-1};
    int stdoutPipe_[2];
    int stderrPipe_[2];
    int stdinPipe_[2];
#endif

    void resetRuntimeState_() {
        stdoutSignalPending_.store(false, std::memory_order_release);
        stderrSignalPending_.store(false, std::memory_order_release);
        exitNotificationEnabled_.store(false, std::memory_order_release);
        processExited_.store(false, std::memory_order_release);
        clearBuffers_();
    }

    void clearBuffers_() {
        {
            std::lock_guard<std::mutex> lock(stdoutMutex_);
            stdoutBuffer_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(stderrMutex_);
            stderrBuffer_.clear();
        }
    }

    ThreadHandle* affinityThread_() const {
        ThreadHandle* affinity = threadHandle();
        if (!affinity || !ThreadHandle::isLive(affinity)) {
            affinity = ThreadHandle::sharedFallbackThread();
        }
        return affinity;
    }

    void postToAffinity_(std::function<void()> task) {
        ThreadHandle* affinity = affinityThread_();
        if (affinity && ThreadHandle::isLive(affinity)) {
            affinity->postTask(std::move(task));
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->postEvent(std::move(task));
            return;
        }
        task();
    }

    static bool hasProcessFlag_(ProcessFlags flags, ProcessFlags bit) {
#if defined(_WIN32)
        return (static_cast<DWORD>(flags) & static_cast<DWORD>(bit)) != 0;
#else
        return (static_cast<int>(flags) & static_cast<int>(bit)) != 0;
#endif
    }

    bool processIsDetached_() const {
        return hasProcessFlag_(activeFlags_, ProcessFlags::Detached);
    }

    void startAsyncRuntime_(uint64_t generation) {
        const std::weak_ptr<int> weakGuard = callbackGuard_;

#if defined(_WIN32)
        if (hStdOutRead_) {
            stdoutThread_ = std::thread([this, generation, weakGuard]() {
                readPipeLoop_(StreamKind::StdOut, generation, weakGuard);
            });
        }
        if (hStdErrRead_) {
            stderrThread_ = std::thread([this, generation, weakGuard]() {
                readPipeLoop_(StreamKind::StdErr, generation, weakGuard);
            });
        }
        if (hProcess_ && !processIsDetached_()) {
            waitThread_ = std::thread([this, generation, weakGuard]() {
                waitForProcessExitLoop_(generation, weakGuard);
            });
        }
#else
        if (stdoutPipe_[0] >= 0) {
            stdoutThread_ = std::thread([this, generation, weakGuard]() {
                readPipeLoop_(StreamKind::StdOut, generation, weakGuard);
            });
        }
        if (stderrPipe_[0] >= 0) {
            stderrThread_ = std::thread([this, generation, weakGuard]() {
                readPipeLoop_(StreamKind::StdErr, generation, weakGuard);
            });
        }
        if (childPid_ > 0 && !processIsDetached_()) {
            waitThread_ = std::thread([this, generation, weakGuard]() {
                waitForProcessExitLoop_(generation, weakGuard);
            });
        }
#endif
    }

    static void joinThread_(std::thread& thread) {
        if (!thread.joinable()) {
            return;
        }
        if (thread.get_id() == std::this_thread::get_id()) {
            thread.detach();
            return;
        }
        thread.join();
    }

    void joinAsyncThreads_() {
        joinThread_(stdoutThread_);
        joinThread_(stderrThread_);
        joinThread_(waitThread_);
    }

    void shutdown_(StopMode stopMode, bool preserveChildProcess = false) {
        if (!isOpen()) {
            swCWarning(kSwLogCategory_SwProcess) << "Process not running!";
            return;
        }

        exitNotificationEnabled_.store(false, std::memory_order_release);
        asyncGeneration_.fetch_add(1, std::memory_order_acq_rel);
        stdoutSignalPending_.store(false, std::memory_order_release);
        stderrSignalPending_.store(false, std::memory_order_release);

        const bool keepChildAlive = preserveChildProcess || processIsDetached_();
        if (!keepChildAlive) {
            requestProcessStop_(stopMode);
        }
        closeStdInHandle_();
        joinAsyncThreads_();
        cleanupHandles_();

        processRunning_.store(false, std::memory_order_release);
        processExited_.store(false, std::memory_order_release);
        activeFlags_ = ProcessFlags::NoFlag;

        clearBuffers_();

        emit deviceClosed();
        emit processFinished();
    }

    void scheduleReadySignal_(StreamKind stream, uint64_t generation, const std::weak_ptr<int>& weakGuard) {
        std::atomic<bool>& pending =
            (stream == StreamKind::StdErr) ? stderrSignalPending_ : stdoutSignalPending_;
        bool expected = false;
        if (!pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        postToAffinity_([this, stream, generation, weakGuard]() {
            if (weakGuard.expired() || !SwObject::isLive(this)) {
                return;
            }
            if (asyncGeneration_.load(std::memory_order_acquire) != generation) {
                return;
            }

            std::atomic<bool>& pendingFlag =
                (stream == StreamKind::StdErr) ? stderrSignalPending_ : stdoutSignalPending_;
            pendingFlag.store(false, std::memory_order_release);

            if (stream == StreamKind::StdErr) {
                emit readyReadStdErr();
                return;
            }

            emit readyRead();
            emit readyReadStdOut();
        });
    }

    void scheduleProcessTerminated_(int exitCode, uint64_t generation, const std::weak_ptr<int>& weakGuard) {
        if (!exitNotificationEnabled_.load(std::memory_order_acquire)) {
            return;
        }

        postToAffinity_([this, exitCode, generation, weakGuard]() {
            if (weakGuard.expired() || !SwObject::isLive(this)) {
                return;
            }
            if (asyncGeneration_.load(std::memory_order_acquire) != generation) {
                return;
            }
            if (!exitNotificationEnabled_.load(std::memory_order_acquire)) {
                return;
            }
            emit processTerminated(exitCode);
        });
    }

    void appendOutput_(StreamKind stream,
                       const char* data,
                       size_t size,
                       uint64_t generation,
                       const std::weak_ptr<int>& weakGuard) {
        if (!data || size == 0) {
            return;
        }
        if (weakGuard.expired()) {
            return;
        }
        if (asyncGeneration_.load(std::memory_order_acquire) != generation) {
            return;
        }

        if (stream == StreamKind::StdErr) {
            std::lock_guard<std::mutex> lock(stderrMutex_);
            stderrBuffer_.append(data, size);
        } else {
            std::lock_guard<std::mutex> lock(stdoutMutex_);
            stdoutBuffer_.append(data, size);
        }

        scheduleReadySignal_(stream, generation, weakGuard);
    }

    SwString takeBuffer_(SwByteArray& buffer, std::mutex& mutex, int64_t maxSize) {
        std::lock_guard<std::mutex> lock(mutex);
        if (buffer.isEmpty()) {
            return SwString();
        }

        size_t bytesToTake = buffer.size();
        if (maxSize > 0 && static_cast<uint64_t>(maxSize) < bytesToTake) {
            bytesToTake = static_cast<size_t>(maxSize);
        }

        SwString out(buffer.constData(), bytesToTake);
        if (bytesToTake == buffer.size()) {
            buffer.clear();
        } else {
            buffer.remove(0, static_cast<int>(bytesToTake));
        }
        return out;
    }

    bool writeStdIn_(const SwString& data) {
        if (data.isEmpty()) {
            return true;
        }

        std::lock_guard<std::mutex> lock(stdinMutex_);

#if defined(_WIN32)
        if (!hStdInWrite_) {
            return false;
        }

        const std::string utf8 = data.toStdString();
        const char* ptr = utf8.data();
        size_t remaining = utf8.size();
        while (remaining > 0) {
            const DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(0x7fffffff)));
            DWORD written = 0;
            if (!::WriteFile(hStdInWrite_, ptr, chunk, &written, NULL)) {
                const DWORD err = ::GetLastError();
                if (err != ERROR_BROKEN_PIPE && err != ERROR_NO_DATA) {
                    swCError(kSwLogCategory_SwProcess) << "WriteFile(stdin) failed: " << err;
                }
                return false;
            }
            if (written == 0) {
                return false;
            }
            ptr += written;
            remaining -= written;
        }
        return true;
#else
        if (stdinPipe_[1] < 0) {
            return false;
        }

        const std::string utf8 = data.toStdString();
        const char* ptr = utf8.data();
        size_t remaining = utf8.size();
        while (remaining > 0) {
            const ssize_t written = ::write(stdinPipe_[1], ptr, remaining);
            if (written > 0) {
                ptr += written;
                remaining -= static_cast<size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written < 0 && errno == EPIPE) {
                return false;
            }
            swCError(kSwLogCategory_SwProcess) << "write(stdin) failed: " << std::strerror(errno);
            return false;
        }
        return true;
#endif
    }

    void closeStdInHandle_() {
        std::lock_guard<std::mutex> lock(stdinMutex_);
#if defined(_WIN32)
        if (hStdInWrite_) {
            ::CloseHandle(hStdInWrite_);
            hStdInWrite_ = NULL;
        }
#else
        if (stdinPipe_[1] >= 0) {
            ::close(stdinPipe_[1]);
            stdinPipe_[1] = -1;
        }
#endif
    }

    bool createPipes_() {
#if defined(_WIN32)
        if (hStdOutRead_ || hStdErrRead_ || hStdInWrite_) {
            return true;
        }

        SECURITY_ATTRIBUTES saAttr{};
        saAttr.nLength = sizeof(saAttr);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        if (!::CreatePipe(&hStdOutRead_, &hStdOutWrite_, &saAttr, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stdout pipe!";
            return false;
        }
        if (!::SetHandleInformation(hStdOutRead_, HANDLE_FLAG_INHERIT, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to configure stdout read handle!";
            return false;
        }

        if (!::CreatePipe(&hStdErrRead_, &hStdErrWrite_, &saAttr, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stderr pipe!";
            return false;
        }
        if (!::SetHandleInformation(hStdErrRead_, HANDLE_FLAG_INHERIT, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to configure stderr read handle!";
            return false;
        }

        if (!::CreatePipe(&hStdInRead_, &hStdInWrite_, &saAttr, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stdin pipe!";
            return false;
        }
        if (!::SetHandleInformation(hStdInWrite_, HANDLE_FLAG_INHERIT, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to configure stdin write handle!";
            return false;
        }

        return true;
#else
        if (stdoutPipe_[0] >= 0 || stderrPipe_[0] >= 0 || stdinPipe_[1] >= 0) {
            return true;
        }

        if (::pipe(stdoutPipe_) != 0) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stdout pipe: " << std::strerror(errno);
            return false;
        }
        if (::pipe(stderrPipe_) != 0) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stderr pipe: " << std::strerror(errno);
            return false;
        }
        if (::pipe(stdinPipe_) != 0) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stdin pipe: " << std::strerror(errno);
            return false;
        }

        return true;
#endif
    }

    bool shouldCreatePipes_(ProcessFlags flags) const {
#if defined(_WIN32)
        const DWORD rawFlags = static_cast<DWORD>(flags);
        if ((rawFlags & static_cast<DWORD>(ProcessFlags::RunAsAdmin)) != 0 ||
            (rawFlags & static_cast<DWORD>(ProcessFlags::Detached)) != 0) {
            return false;
        }
#else
        if (hasProcessFlag_(flags, ProcessFlags::Detached)) {
            return false;
        }
#endif
        return true;
    }

    bool startProcess_(const SwString& program,
                       const SwStringList& arguments,
                       ProcessFlags creationFlags = ProcessFlags::NoFlag,
                       const SwString& workingDirectory = "") {
#if defined(_WIN32)
        auto quoteArg = [](const std::wstring& s) -> std::wstring {
            if (s.empty()) return L"\"\"";
            const bool needsQuotes = (s.find_first_of(L" \t\n\v\"") != std::wstring::npos);
            if (!needsQuotes) return s;

            std::wstring out;
            out.push_back(L'"');

            size_t backslashes = 0;
            for (wchar_t c : s) {
                if (c == L'\\') {
                    ++backslashes;
                    continue;
                }
                if (c == L'"') {
                    out.append(backslashes * 2 + 1, L'\\');
                    out.push_back(L'"');
                    backslashes = 0;
                    continue;
                }
                out.append(backslashes, L'\\');
                backslashes = 0;
                out.push_back(c);
            }

            out.append(backslashes * 2, L'\\');
            out.push_back(L'"');
            return out;
        };

        const std::wstring wideProgram = program.toStdWString();
        std::wstring wideCommand = quoteArg(wideProgram);
        for (size_t i = 0; i < arguments.size(); ++i) {
            wideCommand.push_back(L' ');
            wideCommand += quoteArg(arguments[i].toStdWString());
        }

        std::wstring wideWorkingDirectory = workingDirectory.toStdWString();

        // RunAsAdmin: use ShellExecuteEx with "runas" verb
        if (static_cast<DWORD>(creationFlags) & static_cast<DWORD>(ProcessFlags::RunAsAdmin)) {
            // Build args string (without the exe itself)
            std::wstring wideArgs;
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (i > 0) wideArgs.push_back(L' ');
                wideArgs += quoteArg(arguments[i].toStdWString());
            }

            swCWarning(kSwLogCategory_SwProcess)
                << "RunAsAdmin launch uses ShellExecuteExW; stdin/stdout/stderr redirection is disabled for this process";

            SHELLEXECUTEINFOW sei = {};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"runas";
            sei.lpFile = wideProgram.c_str();
            sei.lpParameters = wideArgs.c_str();
            sei.lpDirectory = wideWorkingDirectory.empty() ? nullptr : wideWorkingDirectory.c_str();
            sei.nShow = (static_cast<DWORD>(creationFlags) & CREATE_NO_WINDOW) ? SW_HIDE : SW_SHOWNORMAL;

            if (!ShellExecuteExW(&sei)) {
                swCError(kSwLogCategory_SwProcess) << "ShellExecuteEx (runas) failed: " << GetLastError();
                return false;
            }

            hProcess_ = sei.hProcess;
            // Note: no stdout/stderr capture with ShellExecuteEx
            assignProcessLifetimeToLauncher_(creationFlags);
            processRunning_.store(true, std::memory_order_release);
            return true;
        }

        STARTUPINFOW si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        const bool hasStdHandles = (hStdErrWrite_ || hStdOutWrite_ || hStdInRead_);
        if (hasStdHandles) {
            si.hStdError = hStdErrWrite_;
            si.hStdOutput = hStdOutWrite_;
            si.hStdInput = hStdInRead_;
            si.dwFlags |= STARTF_USESTDHANDLES;
        }

        LPCWSTR lpWorkingDir = workingDirectory.isEmpty() ? NULL : wideWorkingDirectory.c_str();
        const bool detachedProcess = hasProcessFlag_(creationFlags, ProcessFlags::Detached);
        const bool requestedSuspended = hasProcessFlag_(creationFlags, ProcessFlags::Suspended);
        BOOL currentProcessInJob = FALSE;
        (void)::IsProcessInJob(::GetCurrentProcess(), NULL, &currentProcessInJob);

        DWORD nativeCreationFlags = static_cast<DWORD>(creationFlags);
        if (detachedProcess) {
            nativeCreationFlags |= CREATE_BREAKAWAY_FROM_JOB;
        } else {
            nativeCreationFlags |= CREATE_SUSPENDED;
            if (currentProcessInJob) {
                nativeCreationFlags |= CREATE_BREAKAWAY_FROM_JOB;
            }
        }

        auto tryCreateProcess = [&](DWORD flags) -> bool {
            return ::CreateProcessW(NULL,
                                    &wideCommand[0],
                                    NULL,
                                    NULL,
                                    hasStdHandles ? TRUE : FALSE,
                                    flags,
                                    NULL,
                                    lpWorkingDir,
                                    &si,
                                    &pi) != FALSE;
        };

        if (!tryCreateProcess(nativeCreationFlags)) {
            DWORD error = ::GetLastError();
            const bool canRetryWithoutBreakaway =
                detachedProcess &&
                currentProcessInJob &&
                (nativeCreationFlags & CREATE_BREAKAWAY_FROM_JOB) != 0 &&
                error == ERROR_ACCESS_DENIED;
            if (canRetryWithoutBreakaway) {
                const DWORD retryFlags = nativeCreationFlags & ~CREATE_BREAKAWAY_FROM_JOB;
                if (tryCreateProcess(retryFlags)) {
                    error = ERROR_SUCCESS;
                } else {
                    error = ::GetLastError();
                }
            }
            if (error != ERROR_SUCCESS) {
                swCError(kSwLogCategory_SwProcess) << "CreateProcess failed with error code: " << error;
                return false;
            }
        }

        hProcess_ = pi.hProcess;
        hThread_ = pi.hThread;
        const bool attachedToLauncher = assignProcessLifetimeToLauncher_(creationFlags);
        if (!detachedProcess && !attachedToLauncher) {
            ::TerminateProcess(hProcess_, 1u);
            swCError(kSwLogCategory_SwProcess) << "Attached child could not be bound to launcher lifetime";
            return false;
        }
        if (!detachedProcess && !requestedSuspended) {
            ::ResumeThread(hThread_);
        }

        if (hStdOutWrite_) {
            ::CloseHandle(hStdOutWrite_);
            hStdOutWrite_ = NULL;
        }
        if (hStdErrWrite_) {
            ::CloseHandle(hStdErrWrite_);
            hStdErrWrite_ = NULL;
        }
        if (hStdInRead_) {
            ::CloseHandle(hStdInRead_);
            hStdInRead_ = NULL;
        }

        return true;
#else
        SwList<std::string> storage;
        storage.append(program.toStdString());
        for (size_t i = 0; i < arguments.size(); ++i) {
            storage.append(arguments[i].toStdString());
        }

        SwList<char*> argv;
        for (size_t i = 0; i < storage.size(); ++i) {
            argv.append(const_cast<char*>(storage[i].c_str()));
        }
        argv.append(nullptr);

        const pid_t pid = ::fork();
        if (pid == 0) {
            if (!workingDirectory.isEmpty()) {
                (void)::chdir(workingDirectory.toStdString().c_str());
            }

            if (hasProcessFlag_(creationFlags, ProcessFlags::Detached)) {
                (void)::setsid();
#if defined(__linux__)
            } else {
                (void)::prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
            }

            if (stdinPipe_[0] >= 0) {
                ::dup2(stdinPipe_[0], STDIN_FILENO);
            }
            if (stdoutPipe_[1] >= 0) {
                ::dup2(stdoutPipe_[1], STDOUT_FILENO);
            }
            if (stderrPipe_[1] >= 0) {
                ::dup2(stderrPipe_[1], STDERR_FILENO);
            }

            if (stdinPipe_[0] >= 0) ::close(stdinPipe_[0]);
            if (stdinPipe_[1] >= 0) ::close(stdinPipe_[1]);
            if (stdoutPipe_[0] >= 0) ::close(stdoutPipe_[0]);
            if (stdoutPipe_[1] >= 0) ::close(stdoutPipe_[1]);
            if (stderrPipe_[0] >= 0) ::close(stderrPipe_[0]);
            if (stderrPipe_[1] >= 0) ::close(stderrPipe_[1]);

            ::execvp(argv[0], argv.data());
            _exit(127);
        }

        if (pid < 0) {
            swCError(kSwLogCategory_SwProcess) << "fork failed: " << std::strerror(errno);
            return false;
        }

        childPid_ = pid;

        if (stdoutPipe_[1] >= 0) {
            ::close(stdoutPipe_[1]);
            stdoutPipe_[1] = -1;
        }
        if (stderrPipe_[1] >= 0) {
            ::close(stderrPipe_[1]);
            stderrPipe_[1] = -1;
        }
        if (stdinPipe_[0] >= 0) {
            ::close(stdinPipe_[0]);
            stdinPipe_[0] = -1;
        }

        SW_UNUSED(creationFlags)
        return true;
#endif
    }

#if defined(_WIN32)
    bool ensureLauncherJob_() {
        if (hJob_) {
            return true;
        }

        hJob_ = ::CreateJobObjectW(NULL, NULL);
        if (!hJob_) {
            swCError(kSwLogCategory_SwProcess) << "CreateJobObjectW failed: " << ::GetLastError();
            return false;
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                                JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        if (!::SetInformationJobObject(hJob_,
                                       JobObjectExtendedLimitInformation,
                                       &info,
                                       static_cast<DWORD>(sizeof(info)))) {
            swCError(kSwLogCategory_SwProcess) << "SetInformationJobObject failed: " << ::GetLastError();
            ::CloseHandle(hJob_);
            hJob_ = NULL;
            return false;
        }

        return true;
    }

    bool assignProcessLifetimeToLauncher_(ProcessFlags creationFlags) {
        if (hasProcessFlag_(creationFlags, ProcessFlags::Detached) || !hProcess_) {
            return true;
        }

        if (!ensureLauncherJob_()) {
            return false;
        }

        if (!::AssignProcessToJobObject(hJob_, hProcess_)) {
            swCWarning(kSwLogCategory_SwProcess)
                << "AssignProcessToJobObject failed for attached child: " << ::GetLastError();
            return false;
        }
        return true;
    }
#endif

    void requestProcessStop_(StopMode stopMode) {
#if defined(_WIN32)
        if (!hProcess_) {
            return;
        }
        if (processExited_.load(std::memory_order_acquire)) {
            return;
        }

        const UINT exitCode = (stopMode == StopMode::Kill) ? 1u : 0u;
        if (!::TerminateProcess(hProcess_, exitCode)) {
            const DWORD err = ::GetLastError();
            if (err != ERROR_ACCESS_DENIED && err != ERROR_INVALID_HANDLE) {
                swCError(kSwLogCategory_SwProcess) << "TerminateProcess failed: " << err;
            }
        }
#else
        if (childPid_ <= 0) {
            return;
        }
        if (processExited_.load(std::memory_order_acquire)) {
            return;
        }

        int sig = SIGKILL;
        if (stopMode == StopMode::Terminate) {
            sig = SIGTERM;
        }

        if (::kill(childPid_, sig) != 0 && errno != ESRCH) {
            swCError(kSwLogCategory_SwProcess) << "kill failed: " << std::strerror(errno);
        }
#endif
    }

    void cleanupHandles_() {
#if defined(_WIN32)
        auto closeHandle = [](HANDLE& handle) {
            if (handle) {
                ::CloseHandle(handle);
                handle = NULL;
            }
        };

        closeHandle(hStdOutRead_);
        closeHandle(hStdOutWrite_);
        closeHandle(hStdErrRead_);
        closeHandle(hStdErrWrite_);
        closeHandle(hStdInRead_);
        closeHandle(hStdInWrite_);
        closeHandle(hThread_);
        closeHandle(hProcess_);
        closeHandle(hJob_);
#else
        auto closePipeSide = [](int& fd) {
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        };

        closePipeSide(stdoutPipe_[0]);
        closePipeSide(stdoutPipe_[1]);
        closePipeSide(stderrPipe_[0]);
        closePipeSide(stderrPipe_[1]);
        closePipeSide(stdinPipe_[0]);
        closePipeSide(stdinPipe_[1]);
        childPid_ = -1;
#endif
    }

    void readPipeLoop_(StreamKind stream, uint64_t generation, const std::weak_ptr<int>& weakGuard) {
        char buffer[4096];

        while (true) {
#if defined(_WIN32)
            HANDLE handle = (stream == StreamKind::StdErr) ? hStdErrRead_ : hStdOutRead_;
            if (!handle) {
                break;
            }

            DWORD bytesRead = 0;
            const BOOL ok = ::ReadFile(handle, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, NULL);
            if (!ok) {
                const DWORD err = ::GetLastError();
                if (err != ERROR_BROKEN_PIPE &&
                    err != ERROR_PIPE_NOT_CONNECTED &&
                    err != ERROR_INVALID_HANDLE &&
                    err != ERROR_OPERATION_ABORTED) {
                    swCError(kSwLogCategory_SwProcess)
                        << "ReadFile failed on " << (stream == StreamKind::StdErr ? "stderr" : "stdout")
                        << ": " << err;
                }
                break;
            }
            if (bytesRead == 0) {
                break;
            }

            appendOutput_(stream, buffer, static_cast<size_t>(bytesRead), generation, weakGuard);
#else
            const int fd = (stream == StreamKind::StdErr) ? stderrPipe_[0] : stdoutPipe_[0];
            if (fd < 0) {
                break;
            }

            const ssize_t bytesRead = ::read(fd, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                appendOutput_(stream, buffer, static_cast<size_t>(bytesRead), generation, weakGuard);
                continue;
            }
            if (bytesRead == 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EBADF) {
                swCError(kSwLogCategory_SwProcess)
                    << "read failed on " << (stream == StreamKind::StdErr ? "stderr" : "stdout")
                    << ": " << std::strerror(errno);
            }
            break;
#endif
        }
    }

    void waitForProcessExitLoop_(uint64_t generation, const std::weak_ptr<int>& weakGuard) {
#if defined(_WIN32)
        if (!hProcess_) {
            return;
        }

        const DWORD waitResult = ::WaitForSingleObject(hProcess_, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            swCError(kSwLogCategory_SwProcess) << "WaitForSingleObject failed: " << ::GetLastError();
            return;
        }

        DWORD exitCode = 0;
        if (!::GetExitCodeProcess(hProcess_, &exitCode)) {
            swCError(kSwLogCategory_SwProcess) << "GetExitCodeProcess failed: " << ::GetLastError();
            return;
        }

        processExited_.store(true, std::memory_order_release);
        scheduleProcessTerminated_(static_cast<int>(exitCode), generation, weakGuard);
#else
        if (childPid_ <= 0) {
            return;
        }

        int status = 0;
        pid_t result = -1;
        do {
            result = ::waitpid(childPid_, &status, 0);
        } while (result < 0 && errno == EINTR);

        if (result < 0) {
            if (errno != ECHILD) {
                swCError(kSwLogCategory_SwProcess) << "waitpid failed: " << std::strerror(errno);
            }
            return;
        }

        childPid_ = -1;
        processExited_.store(true, std::memory_order_release);

        int exitCode = -1;
        if (WIFEXITED(status)) {
            exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exitCode = -WTERMSIG(status);
        }

        scheduleProcessTerminated_(exitCode, generation, weakGuard);
#endif
    }
};
