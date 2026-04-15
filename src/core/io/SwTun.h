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
#include <cstring>
#include <cstdint>

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
#  include <net/if.h>
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
typedef void    (WINAPI *WintunCloseAdapter_t)(WINTUN_ADAPTER_HANDLE);
typedef WINTUN_SESSION_HANDLE (WINAPI *WintunStartSession_t)(
    WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);
typedef void    (WINAPI *WintunEndSession_t)(WINTUN_SESSION_HANDLE);
typedef BYTE*   (WINAPI *WintunReceivePacket_t)(WINTUN_SESSION_HANDLE, DWORD*);
typedef void    (WINAPI *WintunReleaseReceivePacket_t)(WINTUN_SESSION_HANDLE, const BYTE*);
typedef BYTE*   (WINAPI *WintunAllocateSendPacket_t)(WINTUN_SESSION_HANDLE, DWORD);
typedef void    (WINAPI *WintunSendPacket_t)(WINTUN_SESSION_HANDLE, const BYTE*);
typedef HANDLE  (WINAPI *WintunGetReadWaitEvent_t)(WINTUN_SESSION_HANDLE);

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

        // Validate adapter name (alphanumeric, dash, underscore, space only — prevents command injection)
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

        // Validate IPv4 address format (d.d.d.d)
        {
            std::string a = address.toStdString();
            int octets[4] = {-1,-1,-1,-1};
            int n = sscanf(a.c_str(), "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]);
            if (n != 4 || octets[0] < 0 || octets[0] > 255 || octets[1] < 0 || octets[1] > 255 ||
                octets[2] < 0 || octets[2] > 255 || octets[3] < 0 || octets[3] > 255) {
                swCError(kSwLogCategory_SwTun) << "[SwTun] Invalid IPv4 address: " << a;
                return false;
            }
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

    static bool runNetshCommand_(const std::wstring& arguments,
                                 DWORD timeoutMs,
                                 DWORD* exitCodeOut = nullptr) {
        wchar_t systemDir[MAX_PATH] = {0};
        const UINT systemDirLen = GetSystemDirectoryW(systemDir, MAX_PATH);
        if (systemDirLen == 0 || systemDirLen >= MAX_PATH) {
            return false;
        }

        const std::wstring netshPath = std::wstring(systemDir) + L"\\netsh.exe";
        std::wstring commandLine = L"\"" + netshPath + L"\" " + arguments;

        STARTUPINFOW startupInfo {};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo {};
        if (!CreateProcessW(netshPath.c_str(),
                            commandLine.empty() ? nullptr : &commandLine[0],
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_NO_WINDOW,
                            nullptr,
                            nullptr,
                            &startupInfo,
                            &processInfo)) {
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeoutMs);
        if (waitResult == WAIT_TIMEOUT) {
            (void)TerminateProcess(processInfo.hProcess, 1);
            (void)WaitForSingleObject(processInfo.hProcess, 1000);
        }

        DWORD exitCode = STILL_ACTIVE;
        (void)GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);

        if (exitCodeOut) {
            *exitCodeOut = exitCode;
        }
        if (waitResult != WAIT_OBJECT_0 || exitCode != 0) {
            SetLastError(waitResult == WAIT_TIMEOUT ? WAIT_TIMEOUT : exitCode);
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
        m_fnClose    = (WintunCloseAdapter_t)    GetProcAddress(m_wintunDll, "WintunCloseAdapter");
        m_fnStart    = (WintunStartSession_t)    GetProcAddress(m_wintunDll, "WintunStartSession");
        m_fnEnd      = (WintunEndSession_t)      GetProcAddress(m_wintunDll, "WintunEndSession");
        m_fnRecv     = (WintunReceivePacket_t)   GetProcAddress(m_wintunDll, "WintunReceivePacket");
        m_fnRelease  = (WintunReleaseReceivePacket_t) GetProcAddress(m_wintunDll, "WintunReleaseReceivePacket");
        m_fnAlloc    = (WintunAllocateSendPacket_t)   GetProcAddress(m_wintunDll, "WintunAllocateSendPacket");
        m_fnSend     = (WintunSendPacket_t)      GetProcAddress(m_wintunDll, "WintunSendPacket");
        m_fnWait     = (WintunGetReadWaitEvent_t) GetProcAddress(m_wintunDll, "WintunGetReadWaitEvent");

        if (!m_fnCreate || !m_fnClose || !m_fnStart || !m_fnEnd ||
            !m_fnRecv || !m_fnRelease || !m_fnAlloc || !m_fnSend || !m_fnWait) {
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

        m_adapter = m_fnCreate(wName.c_str(), wType.c_str(), nullptr);
        if (!m_adapter) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] WintunCreateAdapter failed (err=" << GetLastError() << ")";
            emit errorOccurred(static_cast<int>(GetLastError()));
            FreeLibrary(m_wintunDll);
            m_wintunDll = nullptr;
            return false;
        }

        // Configure IP via netsh (inputs already validated above)
        {
            uint32_t mask = ~0u << (32 - prefix);
            const SwString maskString = SwString::number((mask >> 24) & 0xFF) + "." +
                                        SwString::number((mask >> 16) & 0xFF) + "." +
                                        SwString::number((mask >> 8) & 0xFF) + "." +
                                        SwString::number(mask & 0xFF);
            std::string addressUtf8 = address.toStdString();
            std::string maskUtf8 = maskString.toStdString();
            const int addressWideLen = MultiByteToWideChar(CP_UTF8, 0, addressUtf8.c_str(), -1, nullptr, 0);
            const int maskWideLen = MultiByteToWideChar(CP_UTF8, 0, maskUtf8.c_str(), -1, nullptr, 0);
            if (addressWideLen <= 0 || maskWideLen <= 0) {
                swCError(kSwLogCategory_SwTun) << "[SwTun] Failed to convert netsh arguments to UTF-16";
                m_fnClose(m_adapter);
                m_adapter = nullptr;
                FreeLibrary(m_wintunDll);
                m_wintunDll = nullptr;
                return false;
            }

            std::wstring wAddress(static_cast<size_t>(addressWideLen), L'\0');
            std::wstring wMask(static_cast<size_t>(maskWideLen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, addressUtf8.c_str(), -1, &wAddress[0], addressWideLen);
            MultiByteToWideChar(CP_UTF8, 0, maskUtf8.c_str(), -1, &wMask[0], maskWideLen);

            const std::wstring arguments = std::wstring(L"interface ip set address name=\"") +
                                           std::wstring(wName.c_str()) +
                                           L"\" static " +
                                           std::wstring(wAddress.c_str()) +
                                           L" " +
                                           std::wstring(wMask.c_str());
            DWORD netshExitCode = 0;
            if (!runNetshCommand_(arguments, 5000, &netshExitCode)) {
                swCWarning(kSwLogCategory_SwTun)
                    << "[SwTun] netsh interface setup failed or timed out (code="
                    << static_cast<long long>(netshExitCode)
                    << ", err=" << static_cast<long long>(GetLastError())
                    << ")";
                m_fnClose(m_adapter);
                m_adapter = nullptr;
                FreeLibrary(m_wintunDll);
                m_wintunDll = nullptr;
                return false;
            }
            if (false) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd),
                     "netsh interface ip set address name=\"%s\" static %s %u.%u.%u.%u",
                     name.toStdString().c_str(),
                     address.toStdString().c_str(),
                     (mask >> 24) & 0xFF, (mask >> 16) & 0xFF,
                     (mask >> 8) & 0xFF, mask & 0xFF);
            swCDebug(kSwLogCategory_SwTun) << "[SwTun] " << cmd;
            int ret = system(cmd);
            if (ret != 0) {
                swCWarning(kSwLogCategory_SwTun) << "[SwTun] netsh returned " << ret
                    << " — IP configuration may have failed";
            }
        }

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
    WintunCloseAdapter_t           m_fnClose   = nullptr;
    WintunStartSession_t           m_fnStart   = nullptr;
    WintunEndSession_t             m_fnEnd     = nullptr;
    WintunReceivePacket_t          m_fnRecv    = nullptr;
    WintunReleaseReceivePacket_t   m_fnRelease = nullptr;
    WintunAllocateSendPacket_t     m_fnAlloc   = nullptr;
    WintunSendPacket_t             m_fnSend    = nullptr;
    WintunGetReadWaitEvent_t       m_fnWait    = nullptr;

#else
    // -----------------------------------------------------------------------
    //  Linux back-end (/dev/net/tun)
    // -----------------------------------------------------------------------

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
        std::memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
        std::strncpy(ifr.ifr_name, name.toStdString().c_str(), IFNAMSIZ - 1);

        if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
            swCError(kSwLogCategory_SwTun) << "[SwTun] TUNSETIFF failed: " << strerror(errno);
            emit errorOccurred(errno);
            ::close(fd);
            return false;
        }

        m_tunFd = fd;
        m_linuxIfName = SwString(ifr.ifr_name);

        // Configure IP (inputs already validated in open())
        {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "ip addr add %s/%d dev %s",
                     address.toStdString().c_str(), prefix,
                     m_linuxIfName.toStdString().c_str());
            swCDebug(kSwLogCategory_SwTun) << "[SwTun] " << cmd;
            int ret = system(cmd);
            if (ret != 0) {
                swCWarning(kSwLogCategory_SwTun) << "[SwTun] ip addr add returned " << ret;
            }
        }
        {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "ip link set dev %s up mtu %d",
                     m_linuxIfName.toStdString().c_str(), m_mtu);
            swCDebug(kSwLogCategory_SwTun) << "[SwTun] " << cmd;
            int ret = system(cmd);
            if (ret != 0) {
                swCWarning(kSwLogCategory_SwTun) << "[SwTun] ip link set returned " << ret;
            }
        }

        int flags = fcntl(m_tunFd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(m_tunFd, F_SETFL, flags | O_NONBLOCK);

        return true;
    }

    void closeLinux_() {
        if (m_tunFd >= 0) {
            ::close(m_tunFd);
            m_tunFd = -1;
        }
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
