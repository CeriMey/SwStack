#pragma once

/**
 * @file src/core/io/SwTun.h
 * @ingroup core_io
 * @brief TUN (Layer-3) virtual network interface for SwStack.
 *
 * This header belongs to the CoreSw IO layer. It provides a portable TUN adapter
 * that reads and writes raw IP packets through a virtual network interface.
 *
 * Platform back-ends:
 *  - **Windows**: loads wintun.dll at runtime (WireGuard WinTun driver).
 *    Download from https://www.wintun.net/ and place wintun.dll next to the executable.
 *  - **Linux**: uses the kernel /dev/net/tun device (no extra driver needed).
 *
 * The class follows the same monitoring-thread pattern as SwUdpSocket:
 * a background thread polls for incoming packets and delivers them through the
 * `packetReceived` signal on the object's owning thread.
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

#include "SwCoreApplication.h"
#include "SwObject.h"
#include "SwString.h"
#include "SwByteArray.h"
#include "SwList.h"
#include "SwMutex.h"
#include "SwDebug.h"

#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#  include <winsock2.h>
#  include <iphlpapi.h>
#  include "SwTunWintunEmbed.h"   // embedded wintun.dll bytes
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <linux/if_tun.h>
#  include <arpa/inet.h>
#endif

static constexpr const char* kSwLogCategory_SwTun = "sw.core.io.swtun";

// ---------------------------------------------------------------------------
// Windows: WinTun function typedefs (matching wintun.h ABI)
// ---------------------------------------------------------------------------
#if defined(_WIN32)

typedef void* WINTUN_ADAPTER_HANDLE;
typedef void* WINTUN_SESSION_HANDLE;

typedef WINTUN_ADAPTER_HANDLE (WINAPI *WintunCreateAdapter_t)(
    const WCHAR* Name, const WCHAR* TunnelType, const GUID* RequestedGUID);
typedef WINTUN_ADAPTER_HANDLE (WINAPI *WintunOpenAdapter_t)(const WCHAR* Name);
typedef void    (WINAPI *WintunCloseAdapter_t)(WINTUN_ADAPTER_HANDLE);
typedef WINTUN_SESSION_HANDLE (WINAPI *WintunStartSession_t)(
    WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);
typedef void    (WINAPI *WintunEndSession_t)(WINTUN_SESSION_HANDLE);
typedef BYTE*   (WINAPI *WintunReceivePacket_t)(WINTUN_SESSION_HANDLE, DWORD*);
typedef void    (WINAPI *WintunReleaseReceivePacket_t)(WINTUN_SESSION_HANDLE, const BYTE*);
typedef BYTE*   (WINAPI *WintunAllocateSendPacket_t)(WINTUN_SESSION_HANDLE, DWORD);
typedef void    (WINAPI *WintunSendPacket_t)(WINTUN_SESSION_HANDLE, const BYTE*);
typedef HANDLE  (WINAPI *WintunGetReadWaitEvent_t)(WINTUN_SESSION_HANDLE);
typedef void    (WINAPI *WintunGetAdapterLUID_t)(WINTUN_ADAPTER_HANDLE, NET_LUID*);

#endif // _WIN32


/**
 * @brief Portable TUN (Layer-3) virtual network adapter.
 *
 * Usage:
 * @code
 *   SwTun tun;
 *   if (!tun.open("SwVPN", "10.6.7.1", 24)) { ... error ... }
 *
 *   SwObject::connect(&tun, &SwTun::packetReceived, [](const SwByteArray& pkt) {
 *       // pkt is a raw IP packet coming FROM the TUN interface
 *   });
 *
 *   // Inject an IP packet INTO the TUN interface:
 *   tun.writePacket(ipPacketData);
 * @endcode
 *
 * Requires administrator/root privileges.
 */
class SwTun : public SwObject {
    SW_OBJECT(SwTun, SwObject)

public:
    /**
     * @brief Constructs a SwTun instance.
     * @param parent Optional parent object for ownership.
     */
    explicit SwTun(SwObject* parent = nullptr)
        : SwObject(parent)
    {}

    ~SwTun() override {
        close();
    }

    // -- Configuration (call before open) -----------------------------------

    /** @brief Set the MTU for the TUN interface (default: 1500). Clamped to [576, 9000]. */
    void setMtu(int mtu) {
        if (mtu < 576)  mtu = 576;
        if (mtu > 9000) mtu = 9000;
        m_mtu = mtu;
    }
    int mtu() const { return m_mtu; }

    /** @brief Maximum number of packets queued before oldest are dropped. Minimum 1. */
    void setMaxPendingPackets(size_t max) { m_maxPending = (max < 1) ? 1 : max; }
    size_t maxPendingPackets() const { return m_maxPending; }

    // -- Open / Close -------------------------------------------------------

    /**
     * @brief Create and configure the TUN interface.
     * @param name      Adapter display name (e.g. "SwVPN").
     * @param address   IPv4 address to assign (e.g. "10.6.7.1").
     * @param prefix    Subnet prefix length (e.g. 24 for /24).
     * @return true on success.
     *
     * The call requires administrator/root privileges.
     */
    bool open(const SwString& name, const SwString& address, int prefix) {
        if (m_open.load()) {
            swCWarning(kSwLogCategory_SwTun) << "[SwTun] Already open";
            return false;
        }

        // Validate prefix range (avoids UB in bit shift)
        if (prefix < 1 || prefix > 32) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid prefix: " << prefix;
            return false;
        }

        // Validate adapter name (alphanumeric, dash, underscore, space only; prevents command injection)
        {
            std::string n = name.toStdString();
            if (n.empty() || n.size() > 64) {
                swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid adapter name length";
                return false;
            }
            for (char c : n) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != ' ') {
                    swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid character in adapter name: " << c;
                    return false;
                }
            }
        }

        if (!parseIpv4Address_(address)) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid IPv4 address: " << address;
            return false;
        }

#if defined(_WIN32)
        if (!openWindows_(name, address, prefix))
            return false;
#else
        if (!openLinux_(name, address, prefix))
            return false;
#endif

        m_name = name;
        m_address = address;
        m_prefix = prefix;
        m_open.store(true);
        registerDispatcher_();
        return true;
    }

    /**
     * @brief Attach an already-created TUN file descriptor.
     *
     * Android creates VPN/TUN interfaces through android.net.VpnService and
     * hands native code a file descriptor. This entry point lets the portable
     * SwTun read/write path reuse that descriptor without trying to create or
     * configure /dev/net/tun itself.
     */
    bool openFromFileDescriptor(int fd,
                                const SwString& name,
                                const SwString& address,
                                int prefix,
                                bool ownsFd = true) {
#if defined(_WIN32)
        SW_UNUSED(fd);
        SW_UNUSED(name);
        SW_UNUSED(address);
        SW_UNUSED(prefix);
        SW_UNUSED(ownsFd);
        swCError(kSwLogCategory_SwTun)
            << "[SwTun] openFromFileDescriptor is not supported on Windows";
        return false;
#else
        if (m_open.load()) {
            swCWarning(kSwLogCategory_SwTun) << "[SwTun] Already open";
            return false;
        }
        if (fd < 0) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid TUN file descriptor";
            return false;
        }
        if (prefix < 1 || prefix > 32) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid prefix: " << prefix;
            return false;
        }

        if (!parseIpv4Address_(address)) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid IPv4 address: " << address;
            return false;
        }

        if (m_tunFd >= 0) {
            closeLinux_();
        }

        m_tunFd = fd;
        m_ownsTunFd = ownsFd;
        m_linuxIfName = name.isEmpty() ? SwString("tun-fd") : name;

        int flags = fcntl(m_tunFd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(m_tunFd, F_SETFL, flags | O_NONBLOCK);
        }

        m_name = m_linuxIfName;
        m_address = address;
        m_prefix = prefix;
        m_open.store(true);
        registerDispatcher_();
        return true;
#endif
    }

    /**
     * @brief Tear down the TUN interface and release resources.
     */
    void close() {
        if (!m_open.exchange(false))
            return;
        unregisterDispatcher_();

#if defined(_WIN32)
        closeWindows_();
#else
        closeLinux_();
#endif

        SwMutexLocker lock(m_queueMutex);
        m_pending.clear();
        m_totalReceived.store(0);
        m_totalDropped.store(0);
    }

    /**
     * @brief Write a raw IP packet into the TUN interface (inject towards the OS network stack).
     * @param packet Raw IP packet bytes.
     * @return true on success.
     */
    bool writePacket(const SwByteArray& packet) {
        if (!m_open.load() || packet.isEmpty())
            return false;
#if defined(_WIN32)
        return writeWindows_(packet);
#else
        return writeLinux_(packet);
#endif
    }

    // -- Pending packet queue (read side) -----------------------------------

    /** @brief Returns true if there is at least one queued received packet. */
    bool hasPendingPackets() const {
        SwMutexLocker lock(m_queueMutex);
        return !m_pending.isEmpty();
    }

    /** @brief Number of pending received packets in the queue. */
    size_t pendingPacketCount() const {
        SwMutexLocker lock(m_queueMutex);
        return m_pending.size();
    }

    /**
     * @brief Dequeue and return the next received IP packet (FIFO).
     * @return The raw IP packet, or an empty SwByteArray if none available.
     */
    SwByteArray readPacket() {
        SwMutexLocker lock(m_queueMutex);
        if (m_pending.isEmpty())
            return {};
        SwByteArray pkt = m_pending[0];
        m_pending.removeAt(0);
        return pkt;
    }

    // -- Status -------------------------------------------------------------

    bool isOpen() const { return m_open.load(); }
    SwString name() const { return m_name; }
    SwString address() const { return m_address; }
    int prefix() const { return m_prefix; }
    uint64_t totalReceivedPackets() const { return m_totalReceived.load(); }
    uint64_t totalDroppedPackets() const { return m_totalDropped.load(); }

signals:
    /**
     * @brief Emitted when one or more IP packets have been received from the TUN interface.
     *
     * Connect to this signal and call readPacket() in a loop to drain the queue.
     */
    DECLARE_SIGNAL_VOID(packetReceived)

    /**
     * @brief Emitted when an error occurs.
     * @param errorCode Platform-specific error code.
     */
    DECLARE_SIGNAL(errorOccurred, int)

    // =======================================================================
    // Private implementation
    // =======================================================================
private:

    static bool parseIpv4Address_(const SwString& address, uint32_t* hostOrderOut = nullptr) {
        const std::string text = address.toStdString();
        uint32_t octets[4] = {0, 0, 0, 0};
        size_t pos = 0;

        for (int i = 0; i < 4; ++i) {
            if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
                return false;
            }

            uint32_t value = 0;
            int digits = 0;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                value = value * 10u + static_cast<uint32_t>(text[pos] - '0');
                if (value > 255u || ++digits > 3) {
                    return false;
                }
                ++pos;
            }

            octets[i] = value;
            if (i < 3) {
                if (pos >= text.size() || text[pos] != '.') {
                    return false;
                }
                ++pos;
            }
        }

        if (pos != text.size()) {
            return false;
        }

        if (hostOrderOut) {
            *hostOrderOut = (octets[0] << 24) |
                            (octets[1] << 16) |
                            (octets[2] << 8) |
                            octets[3];
        }
        return true;
    }

    void registerDispatcher_() {
        unregisterDispatcher_();
        if (!m_open.load()) {
            return;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return;
        }

        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }

#if defined(_WIN32)
        if (!m_readEvent) {
            return;
        }
        m_dispatchToken = app->ioDispatcher().watchHandle(
            m_readEvent,
            [affinity](std::function<void()> task) mutable {
                if (affinity && ThreadHandle::isLive(affinity) &&
                    ThreadHandle::currentThread() != affinity) {
                    affinity->postTask(std::move(task));
                    return;
                }
                task();
            },
            [this]() {
                if (!SwObject::isLive(this) || !m_open.load()) {
                    return;
                }
                pollTun_();
            });
#else
        if (m_tunFd < 0) {
            return;
        }
        m_dispatchToken = app->ioDispatcher().watchFd(
            m_tunFd,
            SwIoDispatcher::Readable,
            [affinity](std::function<void()> task) mutable {
                if (affinity && ThreadHandle::isLive(affinity) &&
                    ThreadHandle::currentThread() != affinity) {
                    affinity->postTask(std::move(task));
                    return;
                }
                task();
            },
            [this](uint32_t events) {
                if (!SwObject::isLive(this) || !m_open.load()) {
                    return;
                }
                if (events & (SwIoDispatcher::Error | SwIoDispatcher::Hangup)) {
                    close();
                    return;
                }
                if (events & SwIoDispatcher::Readable) {
                    pollTun_();
                }
            });
#endif
    }

    void unregisterDispatcher_() {
        if (!m_dispatchToken) {
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->ioDispatcher().remove(m_dispatchToken);
        }
        m_dispatchToken = 0;
        m_readyReadPosted.store(false);
    }

    void schedulePacketReceived_() {
        if (m_readyReadPosted.exchange(true))
            return;
        auto notify = [this]() {
            if (!SwObject::isLive(this))
                return;
            m_readyReadPosted.store(false);
            if (!hasPendingPackets())
                return;
            emit packetReceived();
        };
        ThreadHandle* affinity = threadHandle();
        if (affinity && ThreadHandle::currentThread() != affinity) {
            affinity->postTask(std::move(notify));
            return;
        }
        notify();
    }

    // -----------------------------------------------------------------------
    //  Windows back-end (WinTun)
    // -----------------------------------------------------------------------
#if defined(_WIN32)

    /**
     * @brief Extract the embedded wintun.dll to a temp path and load it.
     *
     * The DLL bytes are compiled into the binary via SwTunWintunEmbed.h.
     * We write them to %TEMP%\sw_wintun.dll if the file doesn't already exist
     * (or if the size doesn't match), then LoadLibrary from there.
     */
    static HMODULE loadEmbeddedWintun_() {
        // Try loading from PATH / current dir first (user-provided override)
        HMODULE h = LoadLibraryW(L"wintun.dll");
        if (h) return h;

        // Extract embedded DLL to temp directory
        wchar_t tmpDir[MAX_PATH];
        DWORD len = GetTempPathW(MAX_PATH, tmpDir);
        if (len == 0 || len >= MAX_PATH) return nullptr;

        std::wstring path = std::wstring(tmpDir) + L"sw_wintun.dll";

        // Check if already extracted with correct size
        bool needWrite = true;
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz;
            if (GetFileSizeEx(hFile, &sz) && sz.QuadPart == static_cast<LONGLONG>(kWintunDllSize))
                needWrite = false;
            CloseHandle(hFile);
        }

        if (needWrite) {
            hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
                                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) return nullptr;
            DWORD written = 0;
            BOOL ok = WriteFile(hFile, kWintunDllData, static_cast<DWORD>(kWintunDllSize), &written, nullptr);
            CloseHandle(hFile);
            if (!ok || written != static_cast<DWORD>(kWintunDllSize)) return nullptr;
        }

        return LoadLibraryW(path.c_str());
    }

    static void removeWindowsIpv4Addresses_(const NET_LUID& adapterLuid) {
        PMIB_UNICASTIPADDRESS_TABLE addressTable = nullptr;
        const DWORD tableStatus = GetUnicastIpAddressTable(AF_INET, &addressTable);
        if (tableStatus != NO_ERROR || !addressTable) {
            return;
        }

        for (ULONG i = 0; i < addressTable->NumEntries; ++i) {
            MIB_UNICASTIPADDRESS_ROW row = addressTable->Table[i];
            if (row.InterfaceLuid.Value != adapterLuid.Value) {
                continue;
            }
            const DWORD deleteStatus = DeleteUnicastIpAddressEntry(&row);
            if (deleteStatus != NO_ERROR && deleteStatus != ERROR_NOT_FOUND) {
                swCWarning(kSwLogCategory_SwTun)
                    << "[SwTun] DeleteUnicastIpAddressEntry failed (err="
                    << static_cast<unsigned long>(deleteStatus)
                    << ")";
            }
        }

        FreeMibTable(addressTable);
    }

    static bool setWindowsIpv4Address_(const NET_LUID& adapterLuid,
                                       const SwString& address,
                                       int prefix) {
        uint32_t addressHostOrder = 0;
        if (!parseIpv4Address_(address, &addressHostOrder)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }

        removeWindowsIpv4Addresses_(adapterLuid);

        MIB_UNICASTIPADDRESS_ROW addressRow;
        InitializeUnicastIpAddressEntry(&addressRow);
        addressRow.InterfaceLuid = adapterLuid;
        addressRow.Address.si_family = AF_INET;
        addressRow.Address.Ipv4.sin_family = AF_INET;
        addressRow.Address.Ipv4.sin_addr.S_un.S_addr = htonl(addressHostOrder);
        addressRow.OnLinkPrefixLength = static_cast<UINT8>(prefix);
        addressRow.PrefixOrigin = IpPrefixOriginManual;
        addressRow.SuffixOrigin = IpSuffixOriginManual;
        addressRow.ValidLifetime = 0xFFFFFFFFu;
        addressRow.PreferredLifetime = 0xFFFFFFFFu;
        addressRow.SkipAsSource = FALSE;

        DWORD status = CreateUnicastIpAddressEntry(&addressRow);
        if (status == ERROR_OBJECT_ALREADY_EXISTS) {
            status = SetUnicastIpAddressEntry(&addressRow);
        }
        if (status != NO_ERROR) {
            SetLastError(status);
            return false;
        }
        return true;
    }

    static bool setWindowsMtu_(const NET_LUID& adapterLuid, int mtu) {
        MIB_IPINTERFACE_ROW interfaceRow;
        InitializeIpInterfaceEntry(&interfaceRow);
        interfaceRow.Family = AF_INET;
        interfaceRow.InterfaceLuid = adapterLuid;

        DWORD status = GetIpInterfaceEntry(&interfaceRow);
        if (status != NO_ERROR) {
            SetLastError(status);
            return false;
        }

        interfaceRow.NlMtu = static_cast<ULONG>(mtu);
        status = SetIpInterfaceEntry(&interfaceRow);
        if (status != NO_ERROR) {
            SetLastError(status);
            return false;
        }
        return true;
    }

    static bool configureWindowsInterface_(const NET_LUID& adapterLuid,
                                           const SwString& address,
                                           int prefix,
                                           int mtu) {
        if (!setWindowsIpv4Address_(adapterLuid, address, prefix)) {
            return false;
        }
        if (!setWindowsMtu_(adapterLuid, mtu)) {
            return false;
        }
        return true;
    }

    bool openWindows_(const SwString& name, const SwString& address, int prefix) {
        // Load DLL (embedded or from PATH)
        m_wintunDll = loadEmbeddedWintun_();
        if (!m_wintunDll) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Failed to load wintun.dll (err=" << GetLastError() << ")";
            emit errorOccurred(static_cast<int>(GetLastError()));
            return false;
        }

        m_fnCreate   = (WintunCreateAdapter_t)  GetProcAddress(m_wintunDll, "WintunCreateAdapter");
        m_fnOpen     = (WintunOpenAdapter_t)    GetProcAddress(m_wintunDll, "WintunOpenAdapter");
        m_fnClose    = (WintunCloseAdapter_t)    GetProcAddress(m_wintunDll, "WintunCloseAdapter");
        m_fnStart    = (WintunStartSession_t)    GetProcAddress(m_wintunDll, "WintunStartSession");
        m_fnEnd      = (WintunEndSession_t)      GetProcAddress(m_wintunDll, "WintunEndSession");
        m_fnRecv     = (WintunReceivePacket_t)   GetProcAddress(m_wintunDll, "WintunReceivePacket");
        m_fnRelease  = (WintunReleaseReceivePacket_t) GetProcAddress(m_wintunDll, "WintunReleaseReceivePacket");
        m_fnAlloc    = (WintunAllocateSendPacket_t)   GetProcAddress(m_wintunDll, "WintunAllocateSendPacket");
        m_fnSend     = (WintunSendPacket_t)      GetProcAddress(m_wintunDll, "WintunSendPacket");
        m_fnWait     = (WintunGetReadWaitEvent_t) GetProcAddress(m_wintunDll, "WintunGetReadWaitEvent");
        m_fnGetLuid  = (WintunGetAdapterLUID_t)   GetProcAddress(m_wintunDll, "WintunGetAdapterLUID");

        if (!m_fnCreate || !m_fnClose || !m_fnStart || !m_fnEnd ||
            !m_fnRecv || !m_fnRelease || !m_fnAlloc || !m_fnSend ||
            !m_fnWait || !m_fnGetLuid) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Failed to resolve wintun.dll exports";
            FreeLibrary(m_wintunDll);
            m_wintunDll = nullptr;
            return false;
        }

        // Convert name to wide string (safe ASCII-only conversion, already validated)
        std::string nameUtf8 = name.toStdString();
        int wLen = MultiByteToWideChar(CP_UTF8, 0, nameUtf8.c_str(), -1, nullptr, 0);
        std::wstring wName(static_cast<size_t>(wLen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, nameUtf8.c_str(), -1, &wName[0], wLen);
        std::wstring wType(L"SwStack");

        if (m_fnOpen) {
            m_adapter = m_fnOpen(wName.c_str());
            if (m_adapter) {
                swCDebug(kSwLogCategory_SwTun) << "[SwTun] Reusing existing adapter '" << name << "'";
            }
        }

        if (!m_adapter) {
            m_adapter = m_fnCreate(wName.c_str(), wType.c_str(), nullptr);
        }
        if (!m_adapter) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Unable to open/create adapter (err=" << GetLastError() << ")";
            emit errorOccurred(static_cast<int>(GetLastError()));
            FreeLibrary(m_wintunDll);
            m_wintunDll = nullptr;
            return false;
        }

        NET_LUID adapterLuid;
        std::memset(&adapterLuid, 0, sizeof(adapterLuid));
        m_fnGetLuid(m_adapter, &adapterLuid);
        if (!configureWindowsInterface_(adapterLuid, address, prefix, m_mtu)) {
            const DWORD setupError = GetLastError();
            swCWarning(kSwLogCategory_SwTun)
                << "[SwTun] Native Windows interface setup failed (err="
                << static_cast<unsigned long>(setupError)
                << "); continuing with deferred interface configuration";
        }

        // Start session (4 MB ring)
        m_session = m_fnStart(m_adapter, 0x400000);
        if (!m_session) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] WintunStartSession failed (err=" << GetLastError() << ")";
            emit errorOccurred(static_cast<int>(GetLastError()));
            m_fnClose(m_adapter);
            m_adapter = nullptr;
            FreeLibrary(m_wintunDll);
            m_wintunDll = nullptr;
            return false;
        }

        m_readEvent = m_fnWait(m_session);
        return true;
    }

    void closeWindows_() {
        if (m_session) { m_fnEnd(m_session);   m_session = nullptr; }
        if (m_adapter) { m_fnClose(m_adapter); m_adapter = nullptr; }
        if (m_wintunDll) { FreeLibrary(m_wintunDll); m_wintunDll = nullptr; }
        m_readEvent = nullptr;
    }

    bool writeWindows_(const SwByteArray& packet) {
        if (!m_fnAlloc || !m_fnSend || !m_session) return false;
        BYTE* buf = m_fnAlloc(m_session, static_cast<DWORD>(packet.size()));
        if (!buf) return false;
        std::memcpy(buf, packet.constData(), packet.size());
        m_fnSend(m_session, buf);
        return true;
    }

    void pollTun_() {
        // Snapshot handles to avoid race with close()
        auto fnRecv    = m_fnRecv;
        auto fnRelease = m_fnRelease;
        auto session   = m_session;
        if (!m_readEvent || !fnRecv || !fnRelease || !session) return;

        bool gotAny = false;
        while (m_open.load()) {
            DWORD pktSize = 0;
            BYTE* pkt = fnRecv(session, &pktSize);
            if (!pkt) break;

            // Guard against oversized packets
            if (pktSize > 65535) {
                fnRelease(session, pkt);
                continue;
            }

            SwByteArray data(reinterpret_cast<const char*>(pkt), static_cast<int>(pktSize));
            fnRelease(session, pkt);
            ++m_totalReceived;

            {
                SwMutexLocker lock(m_queueMutex);
                if (m_pending.size() >= m_maxPending) {
                    m_pending.removeAt(0);
                    ++m_totalDropped;
                }
                m_pending.append(data);
            }
            gotAny = true;
        }
        if (gotAny) {
            schedulePacketReceived_();
        }
    }

    // WinTun state
    HMODULE m_wintunDll = nullptr;
    WINTUN_ADAPTER_HANDLE  m_adapter  = nullptr;
    WINTUN_SESSION_HANDLE  m_session  = nullptr;
    HANDLE                 m_readEvent = nullptr;

    WintunCreateAdapter_t          m_fnCreate  = nullptr;
    WintunOpenAdapter_t            m_fnOpen    = nullptr;
    WintunCloseAdapter_t           m_fnClose   = nullptr;
    WintunStartSession_t           m_fnStart   = nullptr;
    WintunEndSession_t             m_fnEnd     = nullptr;
    WintunReceivePacket_t          m_fnRecv    = nullptr;
    WintunReleaseReceivePacket_t   m_fnRelease = nullptr;
    WintunAllocateSendPacket_t     m_fnAlloc   = nullptr;
    WintunSendPacket_t             m_fnSend    = nullptr;
    WintunGetReadWaitEvent_t       m_fnWait    = nullptr;
    WintunGetAdapterLUID_t         m_fnGetLuid = nullptr;

#else
    // -----------------------------------------------------------------------
    //  Linux back-end (/dev/net/tun)
    // -----------------------------------------------------------------------

    static bool fillLinuxIfreqName_(struct ifreq& ifr, const SwString& ifName) {
        const std::string name = ifName.toStdString();
        if (name.empty() || name.size() >= IFNAMSIZ) {
            errno = EINVAL;
            swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid Linux interface name: " << ifName;
            return false;
        }

        std::memset(&ifr, 0, sizeof(ifr));
        std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);
        return true;
    }

    static bool setLinuxIpv4Field_(int controlFd,
                                   const SwString& ifName,
                                   unsigned long request,
                                   uint32_t ipv4HostOrder,
                                   const char* label) {
        struct ifreq ifr;
        if (!fillLinuxIfreqName_(ifr, ifName)) {
            return false;
        }

        struct sockaddr_in* socketAddress = reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr);
        socketAddress->sin_family = AF_INET;
        socketAddress->sin_addr.s_addr = htonl(ipv4HostOrder);

        if (ioctl(controlFd, request, &ifr) < 0) {
            swCError(kSwLogCategory_SwTun)
                << "[SwTun] " << label << " failed on " << ifName << ": " << strerror(errno);
            return false;
        }
        return true;
    }

    static bool setLinuxMtu_(int controlFd, const SwString& ifName, int mtu) {
        struct ifreq ifr;
        if (!fillLinuxIfreqName_(ifr, ifName)) {
            return false;
        }

        ifr.ifr_mtu = mtu;
        if (ioctl(controlFd, SIOCSIFMTU, &ifr) < 0) {
            swCError(kSwLogCategory_SwTun)
                << "[SwTun] SIOCSIFMTU failed on " << ifName << ": " << strerror(errno);
            return false;
        }
        return true;
    }

    static bool setLinuxInterfaceUp_(int controlFd, const SwString& ifName) {
        struct ifreq ifr;
        if (!fillLinuxIfreqName_(ifr, ifName)) {
            return false;
        }

        if (ioctl(controlFd, SIOCGIFFLAGS, &ifr) < 0) {
            swCError(kSwLogCategory_SwTun)
                << "[SwTun] SIOCGIFFLAGS failed on " << ifName << ": " << strerror(errno);
            return false;
        }

        ifr.ifr_flags = static_cast<short>(ifr.ifr_flags | IFF_UP);
        if (ioctl(controlFd, SIOCSIFFLAGS, &ifr) < 0) {
            swCError(kSwLogCategory_SwTun)
                << "[SwTun] SIOCSIFFLAGS failed on " << ifName << ": " << strerror(errno);
            return false;
        }
        return true;
    }

    static bool configureLinuxInterface_(const SwString& ifName,
                                         const SwString& address,
                                         int prefix,
                                         int mtu) {
        uint32_t addressHostOrder = 0;
        if (!parseIpv4Address_(address, &addressHostOrder)) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid IPv4 address: " << address;
            return false;
        }

        const uint32_t maskHostOrder = (prefix == 32) ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - prefix));
        const int controlFd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (controlFd < 0) {
            swCError(kSwLogCategory_SwTun)
                << "[SwTun] Failed to open interface control socket: " << strerror(errno);
            return false;
        }

        const bool ok =
            setLinuxIpv4Field_(controlFd, ifName, SIOCSIFADDR, addressHostOrder, "SIOCSIFADDR") &&
            setLinuxIpv4Field_(controlFd, ifName, SIOCSIFNETMASK, maskHostOrder, "SIOCSIFNETMASK") &&
            setLinuxMtu_(controlFd, ifName, mtu) &&
            setLinuxInterfaceUp_(controlFd, ifName);

        if (!ok) {
            const int savedErrno = errno;
            ::close(controlFd);
            errno = savedErrno;
            return false;
        }

        ::close(controlFd);
        return true;
    }

    bool openLinux_(const SwString& name, const SwString& address, int prefix) {
        if (m_tunFd >= 0) {
            swCWarning(kSwLogCategory_SwTun) << "[SwTun] fd already open, closing first";
            ::close(m_tunFd);
            m_tunFd = -1;
        }

        int fd = ::open("/dev/net/tun", O_RDWR);
        if (fd < 0) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] Failed to open /dev/net/tun: " << strerror(errno);
            emit errorOccurred(errno);
            return false;
        }

        struct ifreq ifr;
        if (!fillLinuxIfreqName_(ifr, name)) {
            emit errorOccurred(errno);
            ::close(fd);
            return false;
        }
        ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

        if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] TUNSETIFF failed: " << strerror(errno);
            emit errorOccurred(errno);
            ::close(fd);
            return false;
        }

        m_tunFd = fd;
        m_ownsTunFd = true;
        m_linuxIfName = SwString(ifr.ifr_name);

        if (!configureLinuxInterface_(m_linuxIfName, address, prefix, m_mtu)) {
            emit errorOccurred(errno);
            closeLinux_();
            return false;
        }

        int flags = fcntl(m_tunFd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(m_tunFd, F_SETFL, flags | O_NONBLOCK);

        return true;
    }

    void closeLinux_() {
        if (m_tunFd >= 0) {
            if (m_ownsTunFd) {
                ::close(m_tunFd);
            }
            m_tunFd = -1;
        }
        m_ownsTunFd = true;
    }

    bool writeLinux_(const SwByteArray& packet) {
        ssize_t written = ::write(m_tunFd, packet.constData(), packet.size());
        return written == static_cast<ssize_t>(packet.size());
    }

    void pollTun_() {
        bool gotAny = false;
        uint8_t buf[65536];
        while (m_open.load()) {
            ssize_t n = ::read(m_tunFd, buf, sizeof(buf));
            if (n <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                emit errorOccurred(errno);
                close();
                return;
            }
            if (n > 65535) continue; // sanity

            SwByteArray data(reinterpret_cast<const char*>(buf), static_cast<int>(n));
            ++m_totalReceived;

            {
                SwMutexLocker lock(m_queueMutex);
                if (m_pending.size() >= m_maxPending) {
                    m_pending.removeAt(0);
                    ++m_totalDropped;
                }
                m_pending.append(data);
            }
            gotAny = true;
        }
        if (gotAny)
            schedulePacketReceived_();
    }

    int m_tunFd = -1;
    bool m_ownsTunFd = true;
    SwString m_linuxIfName;

#endif // _WIN32

    // -- Shared state -------------------------------------------------------

    SwString   m_name;
    SwString   m_address;
    int        m_prefix = 0;
    int        m_mtu = 1500;
    size_t     m_maxPending = 4096;

    mutable SwMutex       m_queueMutex;
    SwList<SwByteArray>   m_pending;

    std::atomic<bool>     m_open{false};
    std::atomic<bool>     m_readyReadPosted{false};
    std::atomic<uint64_t> m_totalReceived{0};
    std::atomic<uint64_t> m_totalDropped{0};
    size_t                m_dispatchToken{0};
};
