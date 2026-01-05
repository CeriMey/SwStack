#ifndef SWSERIALINFO_H
#define SWSERIALINFO_H
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

#include "SwString.h"
#include "SwList.h"
#include "SwSerial.h"

#include <cstdint>
#include <algorithm>
#include <vector>
#include <functional>
#include <cwctype>
#include <cwchar>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <initguid.h>
#pragma comment(lib, "setupapi.lib")
#else
#include <glob.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

class SwSerialInfo {
public:
    SwSerialInfo();
    explicit SwSerialInfo(const SwString& portName);
    SwSerialInfo(const SwSerial& serial);

    SwSerialInfo(const SwSerialInfo&) = default;
    SwSerialInfo(SwSerialInfo&&) noexcept = default;
    SwSerialInfo& operator=(const SwSerialInfo&) = default;
    SwSerialInfo& operator=(SwSerialInfo&&) noexcept = default;
    ~SwSerialInfo() = default;

    void swap(SwSerialInfo& other) noexcept;

    bool isNull() const;
    bool isValid() const;
    bool isBusy() const;

    SwString portName() const;
    SwString systemLocation() const;
    SwString description() const;
    SwString manufacturer() const;
    SwString serialNumber() const;

    bool hasVendorIdentifier() const;
    uint16_t vendorIdentifier() const;
    bool hasProductIdentifier() const;
    uint16_t productIdentifier() const;

    bool operator==(const SwSerialInfo& other) const;
    bool operator!=(const SwSerialInfo& other) const { return !(*this == other); }

    static SwList<SwSerialInfo> availablePorts();
    static SwList<int> standardBaudRates();

private:
#if defined(_WIN32)
    static SwString readRegistryString(HKEY key, const wchar_t* valueName);
    static bool extractVidPid(const std::wstring& hardwareId, uint16_t& vid, uint16_t& pid);
    static void fillFromSetupDi(SwSerialInfo& info, HDEVINFO devInfoSet, SP_DEVINFO_DATA& devInfo);
#else
    static bool tryOpenDevice(const char* path);
    static void appendFromGlob(SwList<SwSerialInfo>& list, const char* pattern);
#endif

    SwString portName_;
    SwString systemLocation_;
    SwString description_;
    SwString manufacturer_;
    SwString serialNumber_;
    bool busy_;
    bool hasVendorId_;
    uint16_t vendorId_;
    bool hasProductId_;
    uint16_t productId_;

    void refreshFromSystem();
};

inline SwSerialInfo::SwSerialInfo()
    : busy_(false)
    , hasVendorId_(false)
    , vendorId_(0)
    , hasProductId_(false)
    , productId_(0) {}

inline SwSerialInfo::SwSerialInfo(const SwString& portName)
    : SwSerialInfo() {
    portName_ = portName;
    refreshFromSystem();
}

inline SwSerialInfo::SwSerialInfo(const SwSerial& serial)
    : SwSerialInfo(serial.portName()) {
    busy_ = serial.isOpen();
}

inline void SwSerialInfo::swap(SwSerialInfo& other) noexcept {
    std::swap(portName_, other.portName_);
    std::swap(systemLocation_, other.systemLocation_);
    std::swap(description_, other.description_);
    std::swap(manufacturer_, other.manufacturer_);
    std::swap(serialNumber_, other.serialNumber_);
    std::swap(busy_, other.busy_);
    std::swap(hasVendorId_, other.hasVendorId_);
    std::swap(vendorId_, other.vendorId_);
    std::swap(hasProductId_, other.hasProductId_);
    std::swap(productId_, other.productId_);
}

inline bool SwSerialInfo::isNull() const {
    return portName_.isEmpty() && systemLocation_.isEmpty();
}

inline bool SwSerialInfo::isValid() const {
    return !portName_.isEmpty() || !systemLocation_.isEmpty();
}

inline bool SwSerialInfo::isBusy() const {
    return busy_;
}

inline SwString SwSerialInfo::portName() const {
    return portName_;
}

inline SwString SwSerialInfo::systemLocation() const {
    return systemLocation_;
}

inline SwString SwSerialInfo::description() const {
    return description_;
}

inline SwString SwSerialInfo::manufacturer() const {
    return manufacturer_;
}

inline SwString SwSerialInfo::serialNumber() const {
    return serialNumber_;
}

inline bool SwSerialInfo::hasVendorIdentifier() const {
    return hasVendorId_;
}

inline uint16_t SwSerialInfo::vendorIdentifier() const {
    return vendorId_;
}

inline bool SwSerialInfo::hasProductIdentifier() const {
    return hasProductId_;
}

inline uint16_t SwSerialInfo::productIdentifier() const {
    return productId_;
}

inline bool SwSerialInfo::operator==(const SwSerialInfo& other) const {
    return portName_ == other.portName_ &&
           systemLocation_ == other.systemLocation_ &&
           description_ == other.description_ &&
           manufacturer_ == other.manufacturer_ &&
           serialNumber_ == other.serialNumber_ &&
           busy_ == other.busy_ &&
           hasVendorId_ == other.hasVendorId_ &&
           vendorId_ == other.vendorId_ &&
           hasProductId_ == other.hasProductId_ &&
           productId_ == other.productId_;
}

inline SwList<SwSerialInfo> SwSerialInfo::availablePorts() {
    SwList<SwSerialInfo> result;
#if defined(_WIN32)
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return result;
    }
    SP_DEVINFO_DATA devInfo{};
    devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(deviceInfoSet, index, &devInfo); ++index) {
        SwSerialInfo info;
        info.description_ = SwString("Serial Port");
        info.busy_ = false;
        info.hasProductId_ = false;
        info.hasVendorId_ = false;
        fillFromSetupDi(info, deviceInfoSet, devInfo);
        if (!info.portName_.isEmpty()) {
            result.append(info);
        }
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
#else
    static const char* patterns[] = {
        "/dev/ttyS*",
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
        "/dev/ttyAMA*",
        "/dev/cu.*",
        "/dev/tty.*"
    };
    for (const char* pattern : patterns) {
        appendFromGlob(result, pattern);
    }
#endif
    return result;
}

inline SwList<int> SwSerialInfo::standardBaudRates() {
    static const int rates[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400 };
    SwList<int> list;
    for (int rate : rates) {
        list.append(rate);
    }
    return list;
}

inline void SwSerialInfo::refreshFromSystem() {
    if (portName_.isEmpty()) {
        return;
    }
    auto ports = availablePorts();
    for (const auto& port : ports) {
        if (port.portName() == portName_) {
            *this = port;
            return;
        }
    }
    systemLocation_ = portName_;
    description_.clear();
    manufacturer_.clear();
    serialNumber_.clear();
    busy_ = false;
    hasVendorId_ = false;
    hasProductId_ = false;
    vendorId_ = 0;
    productId_ = 0;
}

#if defined(_WIN32)
inline SwString SwSerialInfo::readRegistryString(HKEY key, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ) {
        return SwString();
    }
    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, valueName, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&buffer[0]), &size) != ERROR_SUCCESS) {
        return SwString();
    }
    buffer.resize((size / sizeof(wchar_t)) - 1);
    return SwString::fromWString(buffer);
}

inline bool SwSerialInfo::extractVidPid(const std::wstring& hardwareId, uint16_t& vid, uint16_t& pid) {
    auto upper = hardwareId;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
    auto vidPos = upper.find(L"VID_");
    auto pidPos = upper.find(L"PID_");
    if (vidPos == std::wstring::npos || pidPos == std::wstring::npos) {
        return false;
    }
    vid = static_cast<uint16_t>(std::wcstoul(upper.c_str() + vidPos + 4, nullptr, 16));
    pid = static_cast<uint16_t>(std::wcstoul(upper.c_str() + pidPos + 4, nullptr, 16));
    return true;
}

inline void SwSerialInfo::fillFromSetupDi(SwSerialInfo& info, HDEVINFO devInfoSet, SP_DEVINFO_DATA& devInfo) {
    WCHAR friendly[256];
    if (SetupDiGetDeviceRegistryPropertyW(devInfoSet, &devInfo, SPDRP_FRIENDLYNAME,
                                          nullptr, reinterpret_cast<PBYTE>(friendly), sizeof(friendly), nullptr)) {
        info.description_ = SwString::fromWString(friendly);
    }

    WCHAR manufacturer[256];
    if (SetupDiGetDeviceRegistryPropertyW(devInfoSet, &devInfo, SPDRP_MFG,
                                          nullptr, reinterpret_cast<PBYTE>(manufacturer), sizeof(manufacturer), nullptr)) {
        info.manufacturer_ = SwString::fromWString(manufacturer);
    }

    WCHAR hardwareId[256];
    if (SetupDiGetDeviceRegistryPropertyW(devInfoSet, &devInfo, SPDRP_HARDWAREID,
                                          nullptr, reinterpret_cast<PBYTE>(hardwareId), sizeof(hardwareId), nullptr)) {
        uint16_t vid = 0;
        uint16_t pid = 0;
        if (extractVidPid(hardwareId, vid, pid)) {
            info.hasVendorId_ = true;
            info.vendorId_ = vid;
            info.hasProductId_ = true;
            info.productId_ = pid;
        }
    }

    HKEY devKey = SetupDiOpenDevRegKey(devInfoSet, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (devKey != INVALID_HANDLE_VALUE) {
        SwString name = readRegistryString(devKey, L"PortName");
        if (!name.isEmpty()) {
            info.portName_ = name;
            info.systemLocation_ = SwString("\\\\.\\") + name;
        }
        RegCloseKey(devKey);
    }

    if (!info.portName_.isEmpty()) {
        HANDLE handle = CreateFileW(info.systemLocation_.toStdWString().c_str(),
                                    GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            info.busy_ = (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION);
        } else {
            info.busy_ = false;
            CloseHandle(handle);
        }
    }
}
#else
inline bool SwSerialInfo::tryOpenDevice(const char* path) {
    int fd = ::open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    return true;
}

inline void SwSerialInfo::appendFromGlob(SwList<SwSerialInfo>& list, const char* pattern) {
    glob_t globResult{};
    if (glob(pattern, GLOB_NOSORT, nullptr, &globResult) != 0) {
        globfree(&globResult);
        return;
    }
    for (size_t i = 0; i < globResult.gl_pathc; ++i) {
        SwSerialInfo info;
        info.systemLocation_ = SwString(globResult.gl_pathv[i]);
        std::string path(globResult.gl_pathv[i]);
        auto pos = path.find_last_of('/');
        info.portName_ = SwString(pos == std::string::npos ? path : path.substr(pos + 1));
        info.description_ = SwString("Serial Port");
        info.manufacturer_.clear();
        info.serialNumber_.clear();
        info.hasVendorId_ = false;
        info.hasProductId_ = false;
        info.busy_ = !tryOpenDevice(globResult.gl_pathv[i]);
        list.append(info);
    }
    globfree(&globResult);
}
#endif

#endif // SWSERIALINFO_H
