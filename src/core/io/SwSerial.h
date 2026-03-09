#pragma once

/**
 * @file src/core/io/SwSerial.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwSerial in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the serial interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwSerial.
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
#include "SwString.h"
#include "SwTimer.h"
#include "SwEventLoop.h"

#include <mutex>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#endif

class SwSerial : public SwIODevice {
    SW_OBJECT(SwSerial, SwIODevice)

public:
    /**
     * @brief Constructs a `SwSerial` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwSerial(SwObject* parent = nullptr)
        : SwIODevice(parent)
        , m_pollTimer(new SwTimer(10, this))
        , m_baudRate(0)
    {
#if defined(_WIN32)
        m_handle = INVALID_HANDLE_VALUE;
#else
        m_fd = -1;
#endif
        SwObject::connect(m_pollTimer, &SwTimer::timeout, [this]() { pollPort(); });
    }

    /**
     * @brief Destroys the `SwSerial` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwSerial() override {
        close();
    }

    /**
     * @brief Sets the port Name.
     * @param name Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPortName(const SwString& name) { m_portName = name; }
    /**
     * @brief Returns the current port Name.
     * @return The current port Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString portName() const { return m_portName; }

    /**
     * @brief Sets the baud Rate.
     * @param baudRate Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBaudRate(int baudRate) { m_baudRate = baudRate; }
    /**
     * @brief Returns the current baud Rate.
     * @return The current baud Rate.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int baudRate() const { return m_baudRate; }

    /**
     * @brief Opens the underlying resource managed by the object.
     * @param portName Value passed to the method.
     * @param baudRate Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool open(const SwString& portName, int baudRate = 115200) {
        close();
#if defined(_WIN32)
        m_handle = CreateFileW(portName.toStdWString().c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            emit errorOccurred(SwString("Unable to open serial port"));
            return false;
        }

        DCB dcb{};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(m_handle, &dcb)) {
            close();
            emit errorOccurred(SwString("GetCommState failed"));
            return false;
        }
        dcb.BaudRate = baudRate;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        if (!SetCommState(m_handle, &dcb)) {
            close();
            emit errorOccurred(SwString("SetCommState failed"));
            return false;
        }
        SetupComm(m_handle, 4096, 4096);
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 0;
        SetCommTimeouts(m_handle, &timeouts);
#else
        m_fd = ::open(portName.toStdString().c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (m_fd < 0) {
            emit errorOccurred(SwString("Unable to open serial port"));
            return false;
        }
        if (!configureUnixPort(baudRate)) {
            close();
            emit errorOccurred(SwString("Unable to configure serial port"));
            return false;
        }
#endif
        m_portName = portName;
        m_baudRate = baudRate;
        m_pollTimer->start(10);
        return true;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() override {
        m_pollTimer->stop();
#if defined(_WIN32)
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
#endif
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_buffer.clear();
    }

    /**
     * @brief Returns whether the object reports open.
     * @return `true` when the object reports open; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isOpen() const override {
#if defined(_WIN32)
        return m_handle != INVALID_HANDLE_VALUE;
#else
        return m_fd >= 0;
#endif
    }

    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param maxSize Value passed to the method.
     * @return The resulting read.
     */
    SwString read(int64_t maxSize = 0) override {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        if (m_buffer.empty()) {
            return SwString();
        }
        size_t count = m_buffer.size();
        if (maxSize > 0 && static_cast<size_t>(maxSize) < count) {
            count = static_cast<size_t>(maxSize);
        }
        SwString result(std::string(m_buffer.begin(), m_buffer.begin() + count));
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + count);
        return result;
    }

    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param data Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool write(const SwString& data) override {
        if (!isOpen()) {
            return false;
        }
        auto payload = data.toStdString();
#if defined(_WIN32)
        DWORD written = 0;
        BOOL ok = WriteFile(m_handle, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
        return ok && written == payload.size();
#else
        ssize_t sent = ::write(m_fd, payload.data(), payload.size());
        return sent == static_cast<ssize_t>(payload.size());
#endif
    }

    /**
     * @brief Performs the `waitForReadyRead` operation.
     * @param msecs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool waitForReadyRead(int msecs = 30000) {
        if (!isOpen()) {
            return false;
        }
        return waitForCondition([this]() { return hasBufferedData(); }, msecs);
    }

    /**
     * @brief Performs the `waitForBytesWritten` operation.
     * @param msecs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool waitForBytesWritten(int msecs = 30000) {
        if (!isOpen()) {
            return false;
        }
        return waitForCondition([this]() { return !hasPendingWrite(); }, msecs);
    }

signals:
    DECLARE_SIGNAL(errorOccurred, SwString);

private:
    void pollPort() {
        if (!isOpen()) {
            return;
        }
#if defined(_WIN32)
        DWORD errors = 0;
        COMSTAT status{};
        if (!ClearCommError(m_handle, &errors, &status)) {
            emit errorOccurred(SwString("ClearCommError failed"));
            close();
            return;
        }
        if (status.cbInQue == 0) {
            return;
        }
        std::vector<char> temp(status.cbInQue);
        DWORD bytesRead = 0;
        if (!ReadFile(m_handle, temp.data(), static_cast<DWORD>(temp.size()), &bytesRead, nullptr) || bytesRead == 0) {
            return;
        }
        appendBuffer(temp.data(), bytesRead);
#else
        char temp[512];
        ssize_t bytes = ::read(m_fd, temp, sizeof(temp));
        if (bytes > 0) {
            appendBuffer(temp, static_cast<size_t>(bytes));
        } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            emit errorOccurred(SwString("Serial read error"));
        }
#endif
    }

    void appendBuffer(const char* data, size_t size) {
        if (!data || size == 0) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_buffer.insert(m_buffer.end(), data, data + size);
        }
        readyRead();
    }

#if !defined(_WIN32)
    bool configureUnixPort(int baudRate) {
        struct termios options{};
        if (tcgetattr(m_fd, &options) != 0) {
            return false;
        }
        cfmakeraw(&options);
        speed_t speed = B115200;
        switch (baudRate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        default: speed = B115200; break;
        }
        cfsetispeed(&options, speed);
        cfsetospeed(&options, speed);
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;
        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 0;
        if (tcsetattr(m_fd, TCSANOW, &options) != 0) {
            return false;
        }
        return true;
    }
#endif

#if defined(_WIN32)
    HANDLE m_handle;
#else
    int m_fd;
#endif
    SwTimer* m_pollTimer;
    SwString m_portName;
    int m_baudRate;
    std::vector<char> m_buffer;
    std::mutex m_bufferMutex;
    static constexpr int kWaitSleepMs = 2;

    bool hasBufferedData() {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        return !m_buffer.empty();
    }

    bool hasPendingWrite() const {
#if defined(_WIN32)
        if (m_handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD errors = 0;
        COMSTAT status{};
        if (!ClearCommError(m_handle, &errors, &status)) {
            return false;
        }
        return status.cbOutQue > 0;
#else
        if (m_fd < 0) {
            return false;
        }
        int pending = 0;
        if (ioctl(m_fd, TIOCOUTQ, &pending) == -1) {
            return false;
        }
        return pending > 0;
#endif
    }

    template<typename Condition>
    bool waitForCondition(Condition&& condition, int msecs) {
        using namespace std::chrono;
        const int timeout = (msecs < 0) ? -1 : msecs;
        auto start = steady_clock::now();
        while (!condition()) {
            if (!isOpen()) {
                return false;
            }
            if (timeout >= 0) {
                auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
                if (elapsed >= timeout) {
                    return false;
                }
            }
            SwEventLoop::swsleep(kWaitSleepMs);
        }
        return true;
    }
};
