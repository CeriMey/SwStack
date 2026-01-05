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

#include "SwIODevice.h"
#include "SwIODescriptor.h"
#include "SwDebug.h"
#include <vector>
#include <string>
#include <iostream>
static constexpr const char* kSwLogCategory_SwProcess = "sw.core.io.swprocess";


#if defined(_WIN32)
#include "platform/win/SwWindows.h"
enum class ProcessFlags : DWORD {
    NoFlag = 0,
    CreateNoWindow = CREATE_NO_WINDOW,
    CreateNewConsole = CREATE_NEW_CONSOLE,
    Detached = DETACHED_PROCESS,
    Suspended = CREATE_SUSPENDED
};

inline ProcessFlags operator|(ProcessFlags a, ProcessFlags b) {
    return static_cast<ProcessFlags>(static_cast<DWORD>(a) | static_cast<DWORD>(b));
}

inline ProcessFlags& operator|=(ProcessFlags& a, ProcessFlags b) {
    a = a | b;
    return a;
}
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
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
 * @brief The SwProcess class provides an interface for managing system processes with I/O redirection.
 *
 * This class allows you to launch and manage external processes while providing access to their
 * standard input, output, and error streams. It inherits from SwIODevice to leverage I/O capabilities.
 *
 * Key Features:
 * - Launch processes and monitor their execution state.
 * - Redirect and interact with the process's standard I/O streams.
 * - Periodically monitor the process state using a timer.
 *
 * Example Usage:
 * @code
 * SwProcess* process = new SwProcess();
 * process->start("myExecutable", {"arg1", "arg2"});
 * process->write("Input data");
 * QString output = process->readAllStandardOutput();
 * delete process;
 * @endcode
 *
 * @see SwIODevice
 */
class SwProcess : public SwIODevice {
public:

    /**
     * @brief Constructor for the SwProcess class.
     *
     * Initializes a new SwProcess object with an optional parent.
     * - Sets the process and pipe handles to NULL.
     * - Initializes the I/O descriptors (stdout, stderr, stdin) to `nullptr`.
     * - Creates a timer to monitor the process state.
     * - Connects the timer to the `checkProcessStatus` method for periodic process monitoring.
     *
     * @param parent An optional parent object for hierarchical ownership.
     */
    SwProcess(SwObject* parent = nullptr)
        : SwIODevice(parent), processRunning(false)
#if defined(_WIN32)
        , hProcess(NULL), hThread(NULL),
          hStdOutRead(NULL), hStdOutWrite(NULL),
          hStdErrRead(NULL), hStdErrWrite(NULL),
          hStdInWrite(NULL), hStdInRead(NULL)
#else
        , childPid(-1)
#endif
    {
#if !defined(_WIN32)
        stdoutPipe[0] = stdoutPipe[1] = -1;
        stderrPipe[0] = stderrPipe[1] = -1;
        stdinPipe[0] = stdinPipe[1] = -1;
#endif
        stdoutDescriptor = nullptr;
        stderrDescriptor = nullptr;
        stdinDescriptor = nullptr;
        monitorTimer = new SwTimer(500, this);
        connect(monitorTimer, SIGNAL(timeout), this, &SwProcess::checkProcessStatus);
    }

    /**
     * @brief Destructor for the SwProcess class.
     *
     * Ensures the proper cleanup of resources associated with the process.
     * - If the process is running, calls `close()` to terminate it and release associated resources.
     */
    virtual ~SwProcess() {
        if (isOpen()) {
            close();
        }
    }

    /**
     * @brief Starts a new process with the specified program and arguments.
     *
     * Initializes and launches a new process while setting up input/output pipes.
     * - Ensures no other process is currently running.
     * - Creates communication pipes for standard input, output, and error streams.
     * - Launches the process using the specified program, arguments, flags, and working directory.
     * - Starts monitoring the process state using a timer.
     *
     * @param program The path to the executable to start.
     * @param arguments A vector of arguments to pass to the executable (optional).
     * @param flags Flags to configure the process creation (default: ProcessFlags::NoFlag).
     * @param workingDirectory The working directory for the process (optional).
     *
     * @return `true` if the process started successfully, `false` otherwise.
     */
    bool start(const SwString& program, const SwStringList& arguments = {},
               ProcessFlags flags = ProcessFlags::NoFlag,
               const SwString& workingDirectory = "") {
        if (isOpen()) {
            swCWarning(kSwLogCategory_SwProcess) << "Process already running!";
            return false;
        }

        if (!createPipes()) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create pipes!";
            return false;
        }

        if (!startProcess(program, arguments, flags, workingDirectory)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to start process!";
            return false;
        }

        processRunning = true;
        emit deviceOpened();
        monitorTimer->start();
        m_timerDercriptor->start();
        return true;
    }

    /**
     * @brief Starts the process using previously set program, arguments, and working directory.
     *
     * Uses the stored program path, arguments, and working directory to launch the process.
     * - Validates that the program path is set before starting.
     * - Delegates the actual process start to the overloaded `start` method.
     *
     * @param flags Flags to configure the process creation (default: ProcessFlags::NoFlag).
     *
     * @return `true` if the process started successfully, `false` otherwise.
     */
    bool start(ProcessFlags flags = ProcessFlags::NoFlag) {
        if (m_program.isEmpty()) {
            swCError(kSwLogCategory_SwProcess) << "Program is not set!";
            return false;
        }
        return start(m_program, m_arguments, flags, m_workingDirectory);
    }

    /**
     * @brief Closes the process stdin pipe (signals EOF to the child) without terminating the process.
     *
     * Useful for programs that read their full input from stdin and only proceed after receiving EOF.
     */
    void closeStdIn() {
        if (!stdinDescriptor) return;
        safeDelete(stdinDescriptor);
    }

    /**
     * @brief Sets the program path for the process.
     *
     * Specifies the executable file to be used when starting the process.
     *
     * @param program The path to the executable file.
     */
    void setProgram(const SwString& program) { m_program = program; }

    /**
     * @brief Retrieves the program path for the process.
     *
     * Returns the path to the executable file set for the process.
     *
     * @return A string containing the program path.
     */
    SwString program() const { return m_program; }

    /**
     * @brief Sets the arguments to be passed to the process.
     *
     * Specifies the command-line arguments that will be used when starting the process.
     *
     * @param arguments A vector of strings containing the arguments.
     */
    void setArguments(const SwStringList& arguments) { m_arguments = arguments; }

    /**
     * @brief Retrieves the arguments set for the process.
     *
     * Returns the command-line arguments that will be passed to the process when started.
     *
     * @return A vector of strings containing the arguments.
     */
    SwStringList arguments() const { return m_arguments; }

    /**
     * @brief Sets the working directory for the process.
     *
     * Specifies the directory in which the process will run.
     *
     * @param dir The path to the working directory.
     */
    void setWorkingDirectory(const SwString& dir) { m_workingDirectory = dir; }

    /**
     * @brief Retrieves the working directory set for the process.
     *
     * Returns the directory in which the process will execute.
     *
     * @return A string containing the path to the working directory.
     */
    SwString workingDirectory() const { return m_workingDirectory; }

    /**
     * @brief Closes the currently running process and releases associated resources.
     *
     * Ensures proper termination of the process and cleanup of allocated resources:
     * - Stops monitoring timers and descriptors.
     * - Removes standard input/output/error descriptors from SwIODevice.
     * - Terminates the process and waits for it to fully stop.
     * - Releases all process handles and emits relevant signals.
     *
     * @note If the process is not running, a message is displayed, and no action is taken.
     */
    void close() override {
        if (!isOpen()) {
            swCWarning(kSwLogCategory_SwProcess) << "Process not running!";
            return;
        }

        processRunning = false;
        m_timerDercriptor->stop();

        removeDescriptor(stdoutDescriptor);
        removeDescriptor(stderrDescriptor);
        safeDelete(stdinDescriptor);

#if defined(_WIN32)
        TerminateProcess(hProcess, 0);
        WaitForSingleObject(hProcess, INFINITE);
        CloseHandle(hProcess);
        if (hThread) {
            CloseHandle(hThread);
            hThread = NULL;
        }
        hProcess = NULL;
#else
        auto closePipe = [](int (&pipefd)[2]) {
            for (int i = 0; i < 2; ++i) {
                if (pipefd[i] >= 0) {
                    ::close(pipefd[i]);
                    pipefd[i] = -1;
                }
            }
        };
        closePipe(stdoutPipe);
        closePipe(stderrPipe);
        closePipe(stdinPipe);

        if (childPid > 0) {
            int status = 0;
            waitpid(childPid, &status, 0);
            childPid = -1;
        }
#endif

        emit deviceClosed();
        emit processFinished();
        monitorTimer->stop();
    }

    /**
     * @brief Checks if the process is currently running.
     *
     * Determines whether the process is open and actively running.
     *
     * @return `true` if the process is running, `false` otherwise.
     */
    bool isOpen() const override {
        return processRunning;
    }

    /**
     * @brief Reads data from the standard output of the process.
     *
     * Reads up to the specified maximum size of data from the process's standard output.
     *
     * @param maxSize The maximum number of bytes to read (default: 0, which means no limit).
     *
     * @return A string containing the data read from the process's standard output.
     *         Returns an empty string if the standard output descriptor is unavailable.
     */
    SwString read(int64_t maxSize = 0) override {
        if (!stdoutDescriptor) return "";
        return stdoutDescriptor->read();
    }

    /**
     * @brief Reads data from the standard error stream of the process.
     *
     * Retrieves the output written to the process's standard error stream.
     *
     * @return A string containing the data read from the process's standard error stream.
     *         Returns an empty string if the standard error descriptor is unavailable.
     */
    SwString readStdErr() {
        if (!stderrDescriptor) return SwString();
        return stderrDescriptor->read();
    }

    /**
     * @brief Writes data to the standard input of the process.
     *
     * Sends the specified string to the process's standard input stream.
     *
     * @param data The string to write to the standard input.
     *
     * @return `true` if the data was successfully written, `false` if the standard input descriptor is unavailable.
     */
    bool write(const SwString& data) override {
        if (!stdinDescriptor) return false;
        return stdinDescriptor->write(data);
    }


public slots:

    /**
     * @brief Slot to forcibly terminate the running process.
     *
     * Immediately stops the process by calling `TerminateProcess`.
     * - Waits for the process to fully terminate using `WaitForSingleObject`.
     * - Emits cleanup signals and calls `close()` to release resources.
     * - Logs an error message if the termination fails.
     *
     * @note This method should be used for immediate termination without waiting for the process to finish naturally.
     */
    void kill() {
        if (!isOpen()) {
            swCWarning(kSwLogCategory_SwProcess) << "Process not running!";
            return;
        }

#if defined(_WIN32)
        if (!TerminateProcess(hProcess, 1)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to kill process: " << GetLastError();
        } else {
            WaitForSingleObject(hProcess, INFINITE);
            swCDebug(kSwLogCategory_SwProcess) << "Process killed.";
            close();
        }
#else
        if (childPid > 0) {
            if (::kill(childPid, SIGKILL) == -1) {
                swCError(kSwLogCategory_SwProcess) << "Failed to kill process: " << std::strerror(errno);
            } else {
                waitpid(childPid, nullptr, 0);
                swCDebug(kSwLogCategory_SwProcess) << "Process killed.";
            }
            childPid = -1;
            close();
        }
#endif
    }

    /**
     * @brief Slot to gracefully terminate the running process.
     *
     * Attempts to stop the process by checking its status and sending termination signals:
     * - If the process is still active, tries to terminate it gracefully (e.g., using `WM_CLOSE` for GUI applications).
     * - If graceful termination fails, calls `TerminateProcess` as a fallback.
     * - Waits for the process to fully terminate using `WaitForSingleObject`.
     * - Emits cleanup signals and calls `close()` to release resources.
     * - Logs the exit code if the process is already terminated.
     *
     * @note This method prioritizes graceful termination before resorting to forced termination.
     */
    void terminate() {
        if (!isOpen()) {
            swCWarning(kSwLogCategory_SwProcess) << "Process not running!";
            return;
        }

#if defined(_WIN32)
        DWORD exitCode = 0;
        GetExitCodeProcess(hProcess, &exitCode);
        if (exitCode == STILL_ACTIVE) {
            swCDebug(kSwLogCategory_SwProcess) << "Attempting to close process...";
            if (!TerminateProcess(hProcess, 0)) {
                swCError(kSwLogCategory_SwProcess) << "Failed to terminate process: " << GetLastError();
            } else {
                WaitForSingleObject(hProcess, INFINITE);
                swCDebug(kSwLogCategory_SwProcess) << "Process terminated.";
                close();
            }
        } else {
            swCWarning(kSwLogCategory_SwProcess) << "Process already terminated with exit code: " << exitCode;
        }
#else
        if (childPid > 0) {
            if (::kill(childPid, SIGTERM) == -1) {
                swCError(kSwLogCategory_SwProcess) << "Failed to terminate process: " << std::strerror(errno);
            } else {
                waitpid(childPid, nullptr, 0);
                swCDebug(kSwLogCategory_SwProcess) << "Process terminated.";
            }
            childPid = -1;
            close();
        }
#endif
    }


signals:
    DECLARE_SIGNAL_VOID(deviceOpened)
    DECLARE_SIGNAL_VOID(deviceClosed)
    DECLARE_SIGNAL_VOID(processFinished)
    DECLARE_SIGNAL(processTerminated, int)

    

private:
    SwTimer* monitorTimer;
    bool processRunning;

    SwString m_program;
    SwStringList m_arguments;
    SwString m_workingDirectory;

#if defined(_WIN32)
    HANDLE hProcess;
    HANDLE hThread;
    HANDLE hStdOutRead;
    HANDLE hStdOutWrite;
    HANDLE hStdErrRead;
    HANDLE hStdErrWrite;
    HANDLE hStdInWrite;
    HANDLE hStdInRead;
#else
    pid_t childPid;
    int stdoutPipe[2];
    int stderrPipe[2];
    int stdinPipe[2];
#endif

    SwIODescriptor* stdoutDescriptor;
    SwIODescriptor* stderrDescriptor;
    SwIODescriptor* stdinDescriptor;

    /**
     * @brief Creates pipes for process communication (stdin, stdout, and stderr).
     *
     * Sets up the standard input, output, and error pipes for the process:
     * - Ensures the descriptors are not already initialized.
     * - Uses `CreatePipe` to create communication pipes.
     * - Configures the handles to prevent inheritance using `SetHandleInformation`.
     * - Wraps the handles in `SwIODescriptor` objects for easier management and adds them to the device.
     *
     * @return `true` if all pipes were successfully created and configured, `false` otherwise.
     *
     * @note This is a private helper function used during process initialization.
     */
    bool createPipes() {
        if (stdoutDescriptor || stderrDescriptor || stdinDescriptor) {
             return true;
         }

#if defined(_WIN32)
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Créer les pipes pour stdout
        if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stdout pipe!";
            return false;
        }
        if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to set handle information for stdout!";
            return false;
        }

        // Créer les pipes pour stderr
        if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &saAttr, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stderr pipe!";
            return false;
        }
        if (!SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to set handle information for stderr!";
            return false;
        }

        // Créer les pipes pour stdin
        if (!CreatePipe(&hStdInRead, &hStdInWrite, &saAttr, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stdin pipe!";
            return false;
        }
        if (!SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0)) {
            swCError(kSwLogCategory_SwProcess) << "Failed to set handle information for stdin!";
            return false;
        }

        stdoutDescriptor = new SwIODescriptor(hStdOutRead, "StdOut");
        stderrDescriptor = new SwIODescriptor(hStdErrRead, "StdErr");
        stdinDescriptor = new SwIODescriptor(hStdInWrite, "StdIn");
        addDescriptor(stdoutDescriptor);
        addDescriptor(stderrDescriptor);
        return true;
#else
        if (pipe(stdoutPipe) == -1) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stdout pipe: " << std::strerror(errno);
            return false;
        }
        if (pipe(stderrPipe) == -1) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stderr pipe: " << std::strerror(errno);
            return false;
        }
        if (pipe(stdinPipe) == -1) {
            swCError(kSwLogCategory_SwProcess) << "Failed to create stdin pipe: " << std::strerror(errno);
            return false;
        }

        stdoutDescriptor = new SwIODescriptor(stdoutPipe[0], "StdOut");
        stderrDescriptor = new SwIODescriptor(stderrPipe[0], "StdErr");
        stdinDescriptor = new SwIODescriptor(stdinPipe[1], "StdIn");
        addDescriptor(stdoutDescriptor);
        addDescriptor(stderrDescriptor);
        return true;
#endif
    }

    /**
     * @brief Launches a new process with the specified program and arguments.
     *
     * Constructs the command line from the program and arguments, sets up process startup information,
     * and creates the process using the Windows API `CreateProcessW`. Configures standard input, output,
     * and error streams to redirect to the respective pipes.
     *
     * @param program The path to the executable to start.
     * @param arguments A vector of strings representing the arguments to pass to the executable.
     * @param creationFlags Flags to customize the process creation behavior (default: ProcessFlags::NoFlag).
     * @param workingDirectory The directory in which the process should run (optional, defaults to the current directory).
     *
     * @return `true` if the process is successfully created, `false` otherwise.
     *
     * @note This is a private helper function used internally during process initialization.
     */
    bool startProcess(const SwString& program, const SwStringList& arguments,
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
        for (const auto& arg : arguments) {
            wideCommand.push_back(L' ');
            wideCommand += quoteArg(arg.toStdWString());
        }
        std::wstring wideWorkingDirectory = workingDirectory.toStdWString();

        STARTUPINFOW si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdError = hStdErrWrite;
        si.hStdOutput = hStdOutWrite;
        si.hStdInput = hStdInRead;
        si.dwFlags |= STARTF_USESTDHANDLES;

        ZeroMemory(&pi, sizeof(pi));

        LPCWSTR lpWorkingDir = workingDirectory.isEmpty() ? NULL : wideWorkingDirectory.c_str();

        // Utiliser 'creationFlags' fourni par l'appelant
        if (!CreateProcessW(NULL, &wideCommand[0], NULL, NULL, TRUE, static_cast<DWORD>(creationFlags), NULL, lpWorkingDir, &si, &pi)) {
            DWORD error = GetLastError(); // Récupérer l'erreur
            swCError(kSwLogCategory_SwProcess) << "CreateProcess failed with error code: " << error;
            return false;
        }

        hProcess = pi.hProcess;
        hThread = pi.hThread;
        return true;
#else
        std::vector<std::string> storage;
        storage.push_back(program.toStdString());
        for (const auto& arg : arguments) {
            storage.push_back(arg.toStdString());
        }
        std::vector<char*> argv;
        for (auto& s : storage) {
            argv.push_back(const_cast<char*>(s.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid == 0) {
            if (!workingDirectory.isEmpty()) {
                if (::chdir(workingDirectory.toStdString().c_str()) != 0) {
                    swCError(kSwLogCategory_SwProcess) << "chdir failed: " << std::strerror(errno);
                }
            }

            dup2(stdinPipe[0], STDIN_FILENO);
            dup2(stdoutPipe[1], STDOUT_FILENO);
            dup2(stderrPipe[1], STDERR_FILENO);

            ::close(stdinPipe[1]);
            ::close(stdoutPipe[0]);
            ::close(stderrPipe[0]);

            execvp(argv[0], argv.data());
            _exit(127);
        } else if (pid < 0) {
            swCError(kSwLogCategory_SwProcess) << "fork failed: " << std::strerror(errno);
            return false;
        }

        childPid = pid;
        ::close(stdoutPipe[1]);
        ::close(stderrPipe[1]);
        ::close(stdinPipe[0]);
        return true;
#endif
    }

private slots:

    /**
     * @brief Slot to check the current status of the process.
     *
     * Monitors the running process to determine if it has terminated.
     * - Retrieves the exit code of the process.
     * - If the process has terminated, emits the `processTerminated` signal with the exit code and calls `close()` to clean up resources.
     * - Logs an error message if unable to retrieve the process's exit code.
     *
     * @note This slot is typically connected to a timer for periodic status monitoring.
     */
    void checkProcessStatus() {
        if (!processRunning) return;

#if defined(_WIN32)
        DWORD exitCode;
        if (GetExitCodeProcess(hProcess, &exitCode)) {
            if (exitCode != STILL_ACTIVE) {
                swCDebug(kSwLogCategory_SwProcess) << "Process terminated with exit code: " << exitCode;
                // Stop the monitor timer *before* emitting: slots may delete this object.
                monitorTimer->stop();
                emit processTerminated(exitCode);
                return;
            }
        }
        else {
            swCError(kSwLogCategory_SwProcess) << "Failed to get process exit code: " << GetLastError();
        }
#else
        if (childPid <= 0) {
            return;
        }
        int status = 0;
        pid_t result = waitpid(childPid, &status, WNOHANG);
        if (result > 0) {
            int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            swCDebug(kSwLogCategory_SwProcess) << "Process terminated with exit code: " << exitCode;
            // Stop the monitor timer *before* emitting: slots may delete this object.
            monitorTimer->stop();
            emit processTerminated(exitCode);
            return;
        } else if (result == -1 && errno != ECHILD) {
            swCError(kSwLogCategory_SwProcess) << "waitpid failed: " << std::strerror(errno);
        }
#endif
    }
};
