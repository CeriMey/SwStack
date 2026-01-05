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

#include <iostream>
#include <functional>
#include <string>
#include <algorithm>

#include "SwDebug.h"
static constexpr const char* kSwLogCategory_SwIODescriptor = "sw.core.io.swiodescriptor";


#if defined(_WIN32)
#include "platform/win/SwWindows.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

class SwIODescriptor {
public:
    using Descriptor = HANDLE;

    SwIODescriptor(HANDLE hFile, std::string descriptorName = "")
        : handle(hFile), m_descriptorName(std::move(descriptorName)) {}

    virtual ~SwIODescriptor() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }

    bool waitForEvent(bool& readyToRead, bool& readyToWrite, int timeoutMs = -1) {
        (void)timeoutMs;
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(handle, NULL, 0, NULL, &bytesAvailable, NULL)) {
            const DWORD err = GetLastError();
            if (err != ERROR_BROKEN_PIPE && err != ERROR_PIPE_NOT_CONNECTED) {
                swCError(kSwLogCategory_SwIODescriptor) << "PeekNamedPipe failed: " << err;
            }
            return false;
        }

        readyToRead = (bytesAvailable > 0);
        readyToWrite = false;
        return true;
    }

    virtual std::string read() {
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(handle, NULL, 0, NULL, &bytesAvailable, NULL)) {
            const DWORD err = GetLastError();
            if (err != ERROR_BROKEN_PIPE && err != ERROR_PIPE_NOT_CONNECTED) {
                swCError(kSwLogCategory_SwIODescriptor) << "PeekNamedPipe failed: " << err;
            }
            return "";
        }

        if (bytesAvailable == 0) {
            return "";
        }

        char buffer[1024];
        DWORD bytesRead = 0;
        const DWORD toRead = (std::min)(bytesAvailable, static_cast<DWORD>(sizeof(buffer) - 1));
        const BOOL success = ReadFile(handle, buffer, toRead, &bytesRead, nullptr);
        if (success && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            return std::string(buffer);
        }

        const DWORD err = GetLastError();
        if (err != ERROR_BROKEN_PIPE && err != ERROR_PIPE_NOT_CONNECTED) {
            swCError(kSwLogCategory_SwIODescriptor) << "ReadFile failed: " << err;
        }
        return "";
    }

    virtual bool write(const std::string& data) {
        DWORD bytesWritten;
        BOOL success = WriteFile(handle, data.c_str(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr);
        return success && (bytesWritten == data.size());
    }

    HANDLE descriptor() const { return handle; }

    void setDescriptorName(const std::string& name) { m_descriptorName = name; }

    std::string descriptorName() const { return m_descriptorName; }

protected:
    HANDLE handle;
    std::string m_descriptorName;
};

#else
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

class SwIODescriptor {
public:
    using Descriptor = int;

    SwIODescriptor(int fd = -1, std::string descriptorName = "")
        : handle(fd), m_descriptorName(std::move(descriptorName)) {
        if (handle >= 0) {
            int flags = fcntl(handle, F_GETFL, 0);
            if (flags != -1) {
                fcntl(handle, F_SETFL, flags | O_NONBLOCK);
            }
        }
    }

    virtual ~SwIODescriptor() {
        if (handle >= 0) {
            close(handle);
            handle = -1;
        }
    }

    bool waitForEvent(bool& readyToRead, bool& readyToWrite, int timeoutMs = -1) {
        if (handle < 0) {
            return false;
        }

        struct pollfd pfd;
        pfd.fd = handle;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int result = poll(&pfd, 1, (timeoutMs < 0) ? -1 : timeoutMs);
        if (result < 0) {
            if (errno != EINTR) {
                swCError(kSwLogCategory_SwIODescriptor) << "poll failed: " << std::strerror(errno);
            }
            return false;
        }

        readyToRead = (pfd.revents & POLLIN) != 0;
        readyToWrite = false;
        return true;
    }

    virtual std::string read() {
        if (handle < 0) {
            return "";
        }

        char buffer[1024];
        ssize_t bytesRead = ::read(handle, buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            return std::string(buffer);
        }

        if (bytesRead == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            swCError(kSwLogCategory_SwIODescriptor) << "read failed: " << std::strerror(errno);
        }
        return "";
    }

    virtual bool write(const std::string& data) {
        if (handle < 0) {
            return false;
        }

        ssize_t written = ::write(handle, data.c_str(), data.size());
        return written == static_cast<ssize_t>(data.size());
    }

    int descriptor() const { return handle; }

    void setDescriptorName(const std::string& name) { m_descriptorName = name; }

    std::string descriptorName() const { return m_descriptorName; }

protected:
    int handle{-1};
    std::string m_descriptorName;
};

#endif
