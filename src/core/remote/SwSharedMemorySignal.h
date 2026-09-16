#pragma once

/**
 * @file src/core/remote/SwSharedMemorySignal.h
 * @ingroup core_remote
 * @brief Declares the public interface exposed by SwSharedMemorySignal in the CoreSw remote and
 * IPC layer.
 *
 * This header belongs to the CoreSw remote and IPC layer. It provides the abstractions used to
 * expose objects across process boundaries and to transport data or signals between peers.
 *
 * Within that layer, this file focuses on the shared memory signal interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Remote-facing declarations in this area usually coordinate identity, proxying, serialization,
 * and synchronization across runtimes.
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

#include "SwAny.h"
#include "SwByteArray.h"
#include "SwJsonArray.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwMutex.h"
#include "SwString.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <cstdlib>

#include "SwIpcTypeName.h"
#include "SwIpcScratchBuffer.h"
#include "SwSharedMemoryAllocation.h"
#include "SwSharedMemoryLifetime.h"
#include "SwEventLoop.h"
#include "SwTimer.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <sys/file.h>
#  include <dlfcn.h>
#  ifdef __linux__
#    include <link.h>
#  endif
#  ifdef __APPLE__
#    include <libproc.h>
#  endif
#  include <sys/types.h>
#  include <sys/file.h>
#  include <sys/un.h>
#  include <pthread.h>
#  include <signal.h>
#  include <time.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#if defined(__ANDROID__)
inline int swAndroidShmOpenUnavailable_(const char*, int, mode_t) {
    errno = ENOSYS;
    return -1;
}

inline int swAndroidShmUnlinkUnavailable_(const char*) {
    errno = ENOSYS;
    return -1;
}

#  define shm_open swAndroidShmOpenUnavailable_
#  define shm_unlink swAndroidShmUnlinkUnavailable_
#  define SW_SHARED_MEMORY_SIGNAL_ANDROID_SHM_STUBS
#endif

namespace sw {
namespace ipc {

namespace detail {

class ProcessHooksSpinGuard_ {
public:
    explicit ProcessHooksSpinGuard_(std::atomic_flag& flag)
        : flag_(flag) {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    ~ProcessHooksSpinGuard_() {
        flag_.clear(std::memory_order_release);
    }

    ProcessHooksSpinGuard_(const ProcessHooksSpinGuard_&) = delete;
    ProcessHooksSpinGuard_& operator=(const ProcessHooksSpinGuard_&) = delete;

private:
    std::atomic_flag& flag_;
};

#ifndef _WIN32
inline void ensureSharedMemoryPermissions_(int fd) {
    if (fd >= 0) {
        (void)::fchmod(fd, 0666);
    }
}
#endif

inline uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < s.size(); ++i) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(s[i]));
        h *= 1099511628211ull;
    }
    return h;
}

inline SwString hex64(uint64_t v) {
    std::ostringstream oss;
    oss.setf(std::ios::hex, std::ios::basefield);
    oss.width(16);
    oss.fill('0');
    oss << v;
    return SwString(oss.str());
}

inline SwString make_shm_name(const SwString& domain,
                              const SwString& object,
                              const SwString& signal) {
    const std::string key = domain.toStdString() + "|" + object.toStdString() + "|" + signal.toStdString();
    const SwString suffix = hex64(fnv1a64(key));
#ifdef _WIN32
    return SwString("sw_sig_") + suffix;
#else
    return SwString("/sw_sig_") + suffix;
#endif
}

// -------------------------------------------------------------------------
// Shared-memory "meta" signal used to notify subscribers when the per-domain
// registry changes (signals appear/disappear, heartbeat updates, ...).
// -------------------------------------------------------------------------
inline const SwString& registryEventsObjectName_() {
    static const SwString kObject("__sw_ipc__");
    return kObject;
}

inline const SwString& registryEventsSignalName_() {
    static const SwString kSignal("__registryChanged__");
    return kSignal;
}

inline bool isRegistryEventsSignal_(const SwString& object, const SwString& signal) {
    return object == registryEventsObjectName_() && signal == registryEventsSignalName_();
}

// Forward-declared here because it uses SwIpcSignal<> (defined later in the file).
inline void notifyRegistryChangedBestEffort_(const SwString& domain);

// -------------------------------------------------------------------------
// Shared registries (best-effort) to allow IPC introspection:
// - Apps registry (global): "sw_ipc_apps_r3"
//     - lists which "soft" (domain) is alive, with pid list + lastSeen
// - Signals registry (per-domain): "sw_ipc_registry_r3_<domain>"
//     - maps shm hashes back to readable info (domain/object/signal/typeId/typeName)
// - Not required for normal operation
// -------------------------------------------------------------------------
struct RegistryEntry {
    uint64_t hash;
    uint64_t typeId;
    uint64_t lastSeenMs;
    uint32_t pid;
    uint32_t reserved;
    char shmName[64];
    char domain[64];
    char object[64];
    char signal[160];
    char typeName[256];
};

template <size_t MaxEntries>
struct RegistryLayout {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
#ifndef _WIN32
    pthread_mutex_t mtx;
#endif
    RegistryEntry entries[MaxEntries];

    static const uint32_t kMagic = 0x52454731u;   // 'REG1'
    static const uint32_t kVersion = 3;
    static const uint32_t kTimeBaseTag = 0x544D5331u; // 'TMS1'
};

struct AppEntry {
    uint64_t domainHash;
    uint64_t lastSeenMs;
    uint32_t pidCount;
    uint32_t reserved;
    uint32_t pids[16];
    uint64_t pidLastSeenMs[16];
    char domain[64];
    char signalsRegistryName[64];
};

template <size_t MaxApps>
struct AppLayout {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
#ifndef _WIN32
    pthread_mutex_t mtx;
#endif
    AppEntry apps[MaxApps];

    static const uint32_t kMagic = 0x41505031u;   // 'APP1'
    static const uint32_t kVersion = 3;
    static const uint32_t kTimeBaseTag = 0x544D5331u; // 'TMS1'
};

struct SubscriberEntry {
    uint64_t hash;
    uint64_t lastSeenMs;
    uint32_t subPid;
    uint32_t refCount;
    char domain[64];
    char object[64];
    char signal[160];
};

template <size_t MaxEntries>
struct SubscribersLayout {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
#ifndef _WIN32
    pthread_mutex_t mtx;
#endif
    SubscriberEntry entries[MaxEntries];

    static const uint32_t kMagic = 0x53554231u;   // 'SUB1'
    static const uint32_t kVersion = 3;
    static const uint32_t kTimeBaseTag = 0x544D5331u; // 'TMS1'
};

inline uint64_t nowMs() {
#ifdef _WIN32
    return static_cast<uint64_t>(::GetTickCount64());
#else
    struct timespec ts;
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
#endif
}

inline uint32_t currentPid() {
#ifdef _WIN32
    return static_cast<uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<uint32_t>(::getpid());
#endif
}

enum class PidState {
    Alive,
    Dead,
    Unknown,
};

inline PidState pidStateBestEffort_(uint32_t pid) {
    if (pid == 0) return PidState::Dead;
#ifdef _WIN32
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) {
        const DWORD e = ::GetLastError();
        if (e == ERROR_INVALID_PARAMETER) return PidState::Dead;
        return PidState::Unknown;
    }
    DWORD code = 0;
    const BOOL ok = ::GetExitCodeProcess(h, &code);
    ::CloseHandle(h);
    if (!ok) return PidState::Unknown;
    if (code == STILL_ACTIVE) return PidState::Alive;
    return PidState::Dead;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return PidState::Alive;
    if (errno == ESRCH) return PidState::Dead;
    // EPERM means it exists but we can't signal it
    return PidState::Alive;
#endif
}

// Observe each peer once during cleanup. Nothing survives this pass: a PID
// may exit or be reused before the next publication.
template <size_t Capacity>
class PidProbePass_ {
public:
    explicit PidProbePass_(uint32_t self) : self_(self) {}
    template <class Probe>
    bool stale(uint32_t pid, uint64_t lastSeen, uint64_t now,
               uint64_t ttl, bool allowTtl, const Probe& probe) {
        if (pid == 0 || lastSeen == 0) return true;
        if (allowTtl && now >= lastSeen && now - lastSeen > ttl) return true;
        if (pid == self_) return false;
        for (size_t i = 0; i < size_; ++i)
            if (states_[i].pid == pid) return states_[i].state == PidState::Dead;
        const auto state = probe(pid);
        if (size_ < Capacity) {
            states_[size_].pid = pid;
            states_[size_++].state = state;
        }
        return state == PidState::Dead; // Unknown keeps the existing entry.
    }
private:
    struct Entry { uint32_t pid; PidState state; };
    const uint32_t self_;
    // No allocation while the process-shared registry mutex is held. At most
    // Capacity rows can supply distinct PIDs; unused entries remain unread.
    std::array<Entry, Capacity> states_;
    size_t size_{0};
};

inline void copyTrunc(char* dst, size_t cap, const SwString& s) {
    if (!dst || cap == 0) return;
    std::string tmp = s.toStdString();
    if (tmp.size() >= cap) tmp.resize(cap - 1);
    std::memcpy(dst, tmp.data(), tmp.size());
    dst[tmp.size()] = '\0';
}

// ---------------------------------------------------------------------
// Subscriber identity context (best-effort)
//
// Used to attach a "subscriber object" (ex: a SwRemoteObject objectFqn) to the
// subscribers registry without changing the SHM layout size. This enables
// correct graph introspection for multi-node-per-process setups (containers).
// ---------------------------------------------------------------------

inline SwString& subscriberObjectContextTls_() {
    static thread_local SwString s;
    return s;
}

class ScopedSubscriberObject {
public:
    /**
     * @brief Constructs a `ScopedSubscriberObject` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit ScopedSubscriberObject(const SwString& subscriberObject)
        : prev_(subscriberObjectContextTls_()) {
        subscriberObjectContextTls_() = subscriberObject;
    }

    /**
     * @brief Destroys the `ScopedSubscriberObject` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~ScopedSubscriberObject() { subscriberObjectContextTls_() = prev_; }

    /**
     * @brief Constructs a `ScopedSubscriberObject` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    ScopedSubscriberObject(const ScopedSubscriberObject&) = delete;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    ScopedSubscriberObject& operator=(const ScopedSubscriberObject&) = delete;

private:
    SwString prev_;
};

inline SwString currentSubscriberObject_() { return subscriberObjectContextTls_(); }

inline void packCstrAfterNul_(char* primary, size_t cap, const SwString& extra) {
    if (!primary || cap == 0) return;

    size_t n = 0;
    while (n < cap && primary[n] != '\0') ++n;
    if (n >= cap) {
        primary[cap - 1] = '\0';
        n = cap - 1;
    }

    const size_t off = n + 1;
    if (off >= cap) return;

    std::string tmp = extra.toStdString();
    if (tmp.size() >= (cap - off)) tmp.resize((cap - off) - 1);
    if (!tmp.empty()) std::memcpy(primary + off, tmp.data(), tmp.size());
    primary[off + tmp.size()] = '\0';
}

inline SwString unpackCstrAfterNul_(const char* primary, size_t cap) {
    if (!primary || cap == 0) return SwString();

    size_t n = 0;
    while (n < cap && primary[n] != '\0') ++n;
    if (n >= cap) return SwString();

    const size_t off = n + 1;
    if (off >= cap) return SwString();
    if (primary[off] == '\0') return SwString();

    size_t end = off;
    while (end < cap && primary[end] != '\0') ++end;
    if (end <= off) return SwString();

    return SwString(std::string(primary + off, end - off));
}

inline SwString sanitizeRegistrySuffix_(const SwString& domain) {
    std::string s = domain.toStdString();
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            (c == '_') || (c == '-') || (c == '.');
        if (!ok) s[i] = '_';
    }
    while (!s.empty() && s.front() == '_') s.erase(0, 1);
    while (!s.empty() && s.back() == '_') s.pop_back();
    if (s.empty()) s = "root";

    // Leave room for the prefix, hash and terminator in AppEntry's 64-byte
    // signalsRegistryName (20 + 26 + 1 + 16 + 1).
    const size_t kMax = 26;
    if (s.size() > kMax) {
        const SwString h = hex64(fnv1a64(domain.toStdString()));
        s.resize(kMax);
        s.append("_");
        s.append(h.toStdString());
    }
    return SwString(s);
}

inline SwString signalsRegistryNameForDomain_(const SwString& domain) {
    const SwString suffix = sanitizeRegistrySuffix_(domain);
#ifdef _WIN32
    return SwString("sw_ipc_registry_r3_") + suffix;
#else
    return SwString("/sw_ipc_registry_r3_") + suffix;
#endif
}

inline SwString signalsRegistryMutexNameForDomain_(const SwString& domain) {
#ifdef _WIN32
    return SwString("sw_ipc_registry_r3_") + sanitizeRegistrySuffix_(domain) + "_mtx";
#else
    return SwString();
#endif
}

inline SwString subscribersRegistryNameForDomain_(const SwString& domain) {
    const SwString suffix = sanitizeRegistrySuffix_(domain);
#ifdef _WIN32
    return SwString("sw_ipc_subs_r3_") + suffix;
#else
    return SwString("/sw_ipc_subs_r3_") + suffix;
#endif
}

inline SwString subscribersRegistryMutexNameForDomain_(const SwString& domain) {
#ifdef _WIN32
    return SwString("sw_ipc_subs_r3_") + sanitizeRegistrySuffix_(domain) + "_mtx";
#else
    return SwString();
#endif
}

#include "ipc/RegistryMapping.inl"

template <size_t MaxApps = 64>
class AppsRegistryTable {
public:
    typedef AppLayout<MaxApps> Layout;
    static const uint64_t kPidTtlMs = 15000; // heartbeat is 1s; keep margin

    /**
     * @brief Performs the `registerDomain` operation.
     * @param domain Value passed to the method.
     * @return The requested register Domain.
     */
    static void registerDomain(const SwString& domain) {
        try {
            ProcessHooks::trackDomain(domain);
            ProcessHooks::ensureHeartbeat(domain);

            std::shared_ptr<Mapping> map = openOrCreate_();
            if (!map) return;

            lock_(map);
            Layout* L = map->layout();

            const uint64_t t = nowMs();
            const uint32_t pid = currentPid();
            const uint64_t dh = fnv1a64(domain.toStdString());
            const SwString sigReg = signalsRegistryNameForDomain_(domain);

            cleanupStale_locked_(L, t);

            for (uint32_t i = 0; i < L->count && i < MaxApps; ++i) {
                AppEntry& e = L->apps[i];
                if (e.domainHash != dh) continue;

                e.lastSeenMs = t;
                copyTrunc(e.domain, sizeof(e.domain), domain);
                copyTrunc(e.signalsRegistryName, sizeof(e.signalsRegistryName), sigReg);

                bool hasPid = false;
                uint32_t pidIndex = 0;
                for (uint32_t k = 0; k < e.pidCount && k < 16; ++k) {
                    if (e.pids[k] == pid) { hasPid = true; pidIndex = k; break; }
                }
                if (!hasPid && e.pidCount < 16) {
                    pidIndex = e.pidCount;
                    e.pids[e.pidCount] = pid;
                    e.pidLastSeenMs[e.pidCount] = t;
                    e.pidCount++;
                } else if (hasPid && pidIndex < 16) {
                    e.pidLastSeenMs[pidIndex] = t;
                }

                unlock_(map);
                return;
            }

            if (L->count < MaxApps) {
                AppEntry& e = L->apps[L->count++];
                std::memset(&e, 0, sizeof(e));
                e.domainHash = dh;
                e.lastSeenMs = t;
                e.pidCount = 0;
                e.pids[e.pidCount++] = pid;
                e.pidLastSeenMs[0] = t;
                copyTrunc(e.domain, sizeof(e.domain), domain);
                copyTrunc(e.signalsRegistryName, sizeof(e.signalsRegistryName), sigReg);
            }

            unlock_(map);
        } catch (...) {
        }
    }

    /**
     * @brief Performs the `unregisterCurrentPid` operation.
     * @param domain Value passed to the method.
     * @return The requested unregister Current Pid.
     */
    static void unregisterCurrentPid(const SwString& domain) {
        try {
            std::shared_ptr<Mapping> map = openOrCreate_();
            if (!map) return;

            lock_(map);
            Layout* L = map->layout();

            const uint64_t t = nowMs();
            cleanupStale_locked_(L, t);

            const uint32_t pid = currentPid();
            const uint64_t dh = fnv1a64(domain.toStdString());

            for (uint32_t i = 0; i < L->count && i < MaxApps; ++i) {
                AppEntry& e = L->apps[i];
                if (e.domainHash != dh) continue;

                removePid_locked_(e, pid);
                if (e.pidCount == 0) {
                    removeAppAt_locked_(*L, i);
                } else {
                    e.lastSeenMs = t;
                }
                break;
            }

            unlock_(map);
        } catch (...) {
        }
    }

    /**
     * @brief Returns the current snapshot.
     * @return The current snapshot.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwJsonArray snapshot() {
        SwJsonArray arr;
        try {
            std::shared_ptr<Mapping> map = openOrCreate_();
            if (!map) return arr;

            lock_(map);
            Layout* L = map->layout();
            cleanupStale_locked_(L, nowMs());
            const uint32_t n = (L->count < MaxApps) ? L->count : static_cast<uint32_t>(MaxApps);
            for (uint32_t i = 0; i < n; ++i) {
                const AppEntry& e = L->apps[i];
                SwJsonObject o;
                o["domain"] = SwString(e.domain);
                o["domainHash"] = hex64(e.domainHash);
                o["lastSeenMs"] = static_cast<double>(e.lastSeenMs);
                o["clientCount"] = static_cast<int>(e.pidCount);
                o["signalsRegistryName"] = SwString(e.signalsRegistryName);
                SwJsonArray pids;
                for (uint32_t k = 0; k < e.pidCount && k < 16; ++k) {
                    SwJsonObject p;
                    p["pid"] = static_cast<int>(e.pids[k]);
                    p["lastSeenMs"] = static_cast<double>(e.pidLastSeenMs[k]);
                    pids.append(p);
                }
                o["pids"] = pids;
                arr.append(o);
            }
            unlock_(map);
        } catch (...) {
        }
        return arr;
    }

private:
    static SwString appsRegistryName_() {
        return appsRegistrySegmentName_();
    }

    static SwString appsRegistryMutexName_() {
#ifdef _WIN32
        return SwString("sw_ipc_apps_r3_mtx");
#else
        return SwString();
#endif
    }

    class Mapping {
    public:
        /**
         * @brief Constructs a `Mapping` instance.
         * @param L Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit Mapping(Layout* L) : L_(L) {}
        /**
         * @brief Destroys the `Mapping` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~Mapping() {
#ifdef _WIN32
            if (mem_) {
                ::UnmapViewOfFile(mem_);
                mem_ = nullptr;
            }
            if (hMap_) {
                ::CloseHandle(hMap_);
                hMap_ = NULL;
            }
            if (hMtx_) {
                ::CloseHandle(hMtx_);
                hMtx_ = NULL;
            }
#else
            if (mem_) {
                ::munmap(mem_, sizeof(Layout));
                mem_ = nullptr;
            }
#endif
        }

        /**
         * @brief Returns the current layout.
         * @return The current layout.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Layout* layout() const { return L_; }

#ifdef _WIN32
        void* mem_{nullptr};
        HANDLE hMap_{NULL};
        HANDLE hMtx_{NULL};
#else
        void* mem_{nullptr};
#endif

    private:
        Layout* L_{nullptr};
    };

    static std::shared_ptr<Mapping> openOrCreate_() {
        // NOTE: This cache can be touched from std::atexit handlers (ProcessHooks::onExit_).
        // If it were a normal function-local static, it could be destroyed before the atexit
        // handler depending on initialization order, leading to UB at shutdown. Keep it leaky.
        static std::atomic_flag cachedLock = ATOMIC_FLAG_INIT;
        static std::shared_ptr<Mapping>* cached = new std::shared_ptr<Mapping>();

        {
            ProcessHooksSpinGuard_ lk(cachedLock);
            if (*cached) return *cached;
        }

#ifdef _WIN32
        const std::string nameA = appsRegistryName_().toStdString();
        HANDLE hMap = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                           static_cast<DWORD>(sizeof(Layout)), nameA.c_str());
        if (!hMap) return std::shared_ptr<Mapping>();
        void* mem = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Layout));
        if (!mem) {
            ::CloseHandle(hMap);
            return std::shared_ptr<Mapping>();
        }
        Layout* L = static_cast<Layout*>(mem);

        const std::string mtxName = appsRegistryMutexName_().toStdString();
        HANDLE hMtx = ::CreateMutexA(NULL, FALSE, mtxName.c_str());
        if (!hMtx) {
            ::UnmapViewOfFile(mem);
            ::CloseHandle(hMap);
            return std::shared_ptr<Mapping>();
        }

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        map->hMap_ = hMap;
        map->hMtx_ = hMtx;

        ::WaitForSingleObject(map->hMtx_, INFINITE);
        if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->count = 0;
        }
        if (L->reserved != Layout::kTimeBaseTag) {
            // Old build (different time base) -> reset to avoid comparing incomparable timestamps.
            L->count = 0;
            std::memset(L->apps, 0, sizeof(L->apps));
            L->reserved = Layout::kTimeBaseTag;
        }
        ::ReleaseMutex(map->hMtx_);

        {
            ProcessHooksSpinGuard_ lk(cachedLock);
            *cached = map;
        }
        return map;
#else
        const std::string nameA = appsRegistryName_().toStdString();
        void* mem = openRegistryMemory_<Layout>(SwString(nameA));
        Layout* L = static_cast<Layout*>(mem);

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        {
            ProcessHooksSpinGuard_ lk(cachedLock);
            *cached = map;
        }
        return map;
#endif
    }

    static void lock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return;
#ifdef _WIN32
        if (map->hMtx_) ::WaitForSingleObject(map->hMtx_, INFINITE);
#else
        (void)lockRegistryMemory_(map->layout(), false, [](Layout* L) {
            cleanupStale_locked_(L, nowMs());
        });
#endif
    }

    static bool tryLock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return false;
#ifdef _WIN32
        if (!map->hMtx_) return false;
        const DWORD wr = ::WaitForSingleObject(map->hMtx_, 0);
        return (wr == WAIT_OBJECT_0 || wr == WAIT_ABANDONED);
#else
        return lockRegistryMemory_(map->layout(), true, [](Layout* L) {
            cleanupStale_locked_(L, nowMs());
        });
#endif
    }

    static void unlock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return;
#ifdef _WIN32
        if (map->hMtx_) ::ReleaseMutex(map->hMtx_);
#else
        pthread_mutex_unlock(&map->layout()->mtx);
#endif
    }

    static bool shouldPrunePid_(uint32_t pid, uint64_t lastSeenMs, uint64_t nowMs, bool allowTtl) {
        if (lastSeenMs == 0) return true;
        if (allowTtl && nowMs >= lastSeenMs && (nowMs - lastSeenMs) > kPidTtlMs) return true;

        const PidState st = pidStateBestEffort_(pid);
        return st == PidState::Dead;
    }

    static void removePid_locked_(AppEntry& e, uint32_t pid) {
        for (uint32_t k = 0; k < e.pidCount && k < 16; ++k) {
            if (e.pids[k] != pid) continue;
            const uint32_t last = (e.pidCount > 0) ? (e.pidCount - 1) : 0;
            if (k != last && last < 16) {
                e.pids[k] = e.pids[last];
                e.pidLastSeenMs[k] = e.pidLastSeenMs[last];
            }
            if (e.pidCount > 0) e.pidCount--;
            return;
        }
    }

    static void removeAppAt_locked_(Layout& L, uint32_t i) {
        if (i >= L.count) return;
        const uint32_t last = (L.count > 0) ? (L.count - 1) : 0;
        if (i != last && last < MaxApps) {
            L.apps[i] = L.apps[last];
        }
        if (L.count > 0) L.count--;
    }

    static void cleanupStale_locked_(Layout* L, uint64_t now) {
        if (!L) return;
        const bool allowTtl = (L->reserved == Layout::kTimeBaseTag);

        uint32_t i = 0;
        while (i < L->count && i < MaxApps) {
            AppEntry& e = L->apps[i];

            uint32_t k = 0;
            while (k < e.pidCount && k < 16) {
                const uint32_t pid = e.pids[k];
                const uint64_t ls = e.pidLastSeenMs[k];
                if (shouldPrunePid_(pid, ls, now, allowTtl)) {
                    const uint32_t last = (e.pidCount > 0) ? (e.pidCount - 1) : 0;
                    if (k != last && last < 16) {
                        e.pids[k] = e.pids[last];
                        e.pidLastSeenMs[k] = e.pidLastSeenMs[last];
                    }
                    if (e.pidCount > 0) e.pidCount--;
                    continue;
                }
                ++k;
            }

            if (e.pidCount == 0) {
                removeAppAt_locked_(*L, i);
                continue;
            }
            ++i;
        }
    }

    static void touchDomainBestEffort_(const SwString& domain) {
        try {
            std::shared_ptr<Mapping> map = openOrCreate_();
            if (!map) return;
            if (!tryLock_(map)) return;

            Layout* L = map->layout();
            const uint64_t t = nowMs();
            if (L->reserved != Layout::kTimeBaseTag) L->reserved = Layout::kTimeBaseTag;
            cleanupStale_locked_(L, t);

            const uint32_t pid = currentPid();
            const uint64_t dh = fnv1a64(domain.toStdString());
            const SwString sigReg = signalsRegistryNameForDomain_(domain);

            for (uint32_t i = 0; i < L->count && i < MaxApps; ++i) {
                AppEntry& e = L->apps[i];
                if (e.domainHash != dh) continue;
                e.lastSeenMs = t;
                copyTrunc(e.domain, sizeof(e.domain), domain);
                copyTrunc(e.signalsRegistryName, sizeof(e.signalsRegistryName), sigReg);
                bool hasPid = false;
                uint32_t pidIndex = 0;
                for (uint32_t k = 0; k < e.pidCount && k < 16; ++k) {
                    if (e.pids[k] == pid) { hasPid = true; pidIndex = k; break; }
                }
                if (!hasPid && e.pidCount < 16) {
                    pidIndex = e.pidCount;
                    e.pids[e.pidCount] = pid;
                    e.pidLastSeenMs[e.pidCount] = t;
                    e.pidCount++;
                } else if (hasPid && pidIndex < 16) {
                    e.pidLastSeenMs[pidIndex] = t;
                }
                unlock_(map);
                return;
            }

            // Self-heal: if the domain entry was pruned/cleared, recreate it.
            if (L->count < MaxApps) {
                AppEntry& e = L->apps[L->count++];
                std::memset(&e, 0, sizeof(e));
                e.domainHash = dh;
                e.lastSeenMs = t;
                e.pidCount = 0;
                e.pids[e.pidCount++] = pid;
                e.pidLastSeenMs[0] = t;
                copyTrunc(e.domain, sizeof(e.domain), domain);
                copyTrunc(e.signalsRegistryName, sizeof(e.signalsRegistryName), sigReg);
            }

            unlock_(map);
        } catch (...) {
        }
    }

    static void unregisterCurrentPidBestEffortStd_(const std::string& domain) {
        try {
            std::shared_ptr<Mapping> map = openOrCreate_();
            if (!map) return;
            if (!tryLock_(map)) return;

            Layout* L = map->layout();
            const uint64_t t = nowMs();
            cleanupStale_locked_(L, t);

            const uint32_t pid = currentPid();
            const uint64_t dh = fnv1a64(domain);

            for (uint32_t i = 0; i < L->count && i < MaxApps; ++i) {
                AppEntry& e = L->apps[i];
                if (e.domainHash != dh) continue;
                removePid_locked_(e, pid);
                if (e.pidCount == 0) removeAppAt_locked_(*L, i);
                else e.lastSeenMs = t;
                break;
            }

            unlock_(map);
        } catch (...) {
        }
    }

    struct ProcessHooks {
        /**
         * @brief Performs the `trackDomain` operation.
         * @param domain Value passed to the method.
         * @return The requested track Domain.
         */
        static void trackDomain(const SwString& domain) {
            ensureInstalled_();
            const std::string d = domain.toStdString();
            ProcessHooksSpinGuard_ lk(lock_());
            domains_().insert(d);
        }

        /**
         * @brief Performs the `ensureHeartbeat` operation.
         * @param domain Value passed to the method.
         * @return The requested ensure Heartbeat.
         */
        static void ensureHeartbeat(const SwString& domain) {
            ensureInstalled_();
            const std::string key = domain.toStdString();
            ProcessHooksSpinGuard_ lk(lock_());
            if (heartbeats_().count(key)) return;

            const SwString domCopy = domain;
            SwEventLoop::RuntimeHandle h = SwEventLoop::installSlowRuntime(1000, [domCopy]() {
                AppsRegistryTable::touchDomainBestEffort_(domCopy);
            });
            heartbeats_()[key] = h;
        }

    private:
        static std::atomic_flag& lock_() {
            static std::atomic_flag flag = ATOMIC_FLAG_INIT;
            return flag;
        }

        static std::set<std::string>& domains_() {
            static std::set<std::string>* s = new std::set<std::string>();
            return *s;
        }

        static std::map<std::string, SwEventLoop::RuntimeHandle>& heartbeats_() {
            static std::map<std::string, SwEventLoop::RuntimeHandle>* m =
                new std::map<std::string, SwEventLoop::RuntimeHandle>();
            return *m;
        }

        static void cleanup_(bool bestEffort) {
            std::vector<std::string> doms;
            {
                ProcessHooksSpinGuard_ lk(lock_());
                for (const auto& d : domains_()) doms.push_back(d);
            }

            for (size_t i = 0; i < doms.size(); ++i) {
                if (bestEffort) unregisterCurrentPidBestEffortStd_(doms[i]);
                else unregisterCurrentPid(SwString(doms[i]));
            }

            // stop heartbeats (best-effort)
            {
                ProcessHooksSpinGuard_ lk(lock_());
                for (auto& kv : heartbeats_()) {
                    SwEventLoop::uninstallRuntime(kv.second);
                }
            }
        }

#ifdef _WIN32
        static BOOL WINAPI ctrlHandler_(DWORD type) {
            switch (type) {
                case CTRL_C_EVENT:
                case CTRL_BREAK_EVENT:
                case CTRL_CLOSE_EVENT:
                case CTRL_LOGOFF_EVENT:
                case CTRL_SHUTDOWN_EVENT:
                    cleanup_(true);
                    break;
                default:
                    break;
            }
            return FALSE; // keep default behavior (terminate)
        }
#endif

        static void onExit_() { cleanup_(true); }

        static void ensureInstalled_() {
            static std::once_flag once;
            std::call_once(once, []() {
                std::atexit(&ProcessHooks::onExit_);
#ifdef _WIN32
                ::SetConsoleCtrlHandler(&ProcessHooks::ctrlHandler_, TRUE);
#endif
            });
        }
    };
};

template <size_t MaxEntries = 256>
class RegistryTable {
public:
    typedef RegistryLayout<MaxEntries> Layout;
    static const uint64_t kEntryTtlMs = 15000; // must be >= heartbeat period

    /**
     * @brief Performs the `registerSignal` operation.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     * @param signal Value passed to the method.
     * @param shmName Value passed to the method.
     * @param typeId Value passed to the method.
     * @param typeName Value passed to the method.
     * @return The requested register Signal.
     */
    static void registerSignal(const SwString& domain,
                               const SwString& object,
                               const SwString& signal,
                               const SwString& shmName,
                               uint64_t typeId,
                               const SwString& typeName) {
        try {
            ProcessHooks::ensureHeartbeat(domain);
            AppsRegistryTable<>::registerDomain(domain);

            std::shared_ptr<Mapping> map = openOrCreate_(domain);
            if (!map) return;

            lock_(map);
            Layout* L = map->layout();

            const uint64_t h = fnv1a64(domain.toStdString() + "|" + object.toStdString() + "|" + signal.toStdString());
            const uint64_t t = nowMs();
            const uint32_t pid = currentPid();
            cleanupStale_locked_(L, t);
            bool changed = false;

            for (uint32_t i = 0; i < L->count && i < MaxEntries; ++i) {
                RegistryEntry& e = L->entries[i];
                if (e.hash == h) {
                    e.typeId = typeId;
                    // Don't steal ownership when another process merely opens the signal.
                    // A delayed heartbeat does not transfer a live publisher's
                    // ownership to a reader opening the same signal.
                    if (e.pid == 0 || e.pid == pid ||
                        pidStateBestEffort_(e.pid) == PidState::Dead) {
                        e.lastSeenMs = t;
                        e.pid = pid;
                    }
                    copyTrunc(e.shmName, sizeof(e.shmName), shmName);
                    copyTrunc(e.domain, sizeof(e.domain), domain);
                    copyTrunc(e.object, sizeof(e.object), object);
                    copyTrunc(e.signal, sizeof(e.signal), signal);
                    copyTrunc(e.typeName, sizeof(e.typeName), typeName);
                    changed = true;
                    break;
                }
            }

            if (!changed && L->count < MaxEntries) {
                RegistryEntry& e = L->entries[L->count++];
                std::memset(&e, 0, sizeof(e));
                e.hash = h;
                e.typeId = typeId;
                e.lastSeenMs = t;
                e.pid = pid;
                copyTrunc(e.shmName, sizeof(e.shmName), shmName);
                copyTrunc(e.domain, sizeof(e.domain), domain);
                copyTrunc(e.object, sizeof(e.object), object);
                copyTrunc(e.signal, sizeof(e.signal), signal);
                copyTrunc(e.typeName, sizeof(e.typeName), typeName);
                changed = true;
            }

            unlock_(map);

            if (changed && !isRegistryEventsSignal_(object, signal)) {
                notifyRegistryChangedBestEffort_(domain);
            }
        } catch (...) {
        }
    }

    /**
     * @brief Performs the `snapshot` operation.
     * @param domain Value passed to the method.
     * @return The requested snapshot.
     */
    static SwJsonArray snapshot(const SwString& domain) {
        SwJsonArray arr;
        try {
            std::shared_ptr<Mapping> map = openOrCreate_(domain);
            if (!map) return arr;

            // Allocate before locking and serialize after unlocking: registry
            // readers must not hold up IPC publishers while building JSON.
            std::vector<RegistryEntry> rows(MaxEntries);
            lock_(map);
            Layout* L = map->layout();
            const uint64_t t = nowMs();
            cleanupStale_locked_(L, t);
            const bool allowTtl = L->reserved == Layout::kTimeBaseTag;
            const uint32_t n = (L->count < MaxEntries) ? L->count : static_cast<uint32_t>(MaxEntries);
            std::memcpy(rows.data(), L->entries, n * sizeof(RegistryEntry));
            unlock_(map);
            for (uint32_t i = 0; i < n; ++i) {
                const RegistryEntry& e = rows[i];
                if (allowTtl && t >= e.lastSeenMs && t - e.lastSeenMs > kEntryTtlMs) continue;
                SwJsonObject o;
                o["hash"] = hex64(e.hash);
                o["typeId"] = hex64(e.typeId);
                o["pid"] = static_cast<int>(e.pid);
                o["lastSeenMs"] = static_cast<double>(e.lastSeenMs);
                o["shmName"] = SwString(e.shmName);
                o["domain"] = SwString(e.domain);
                o["object"] = SwString(e.object);
                o["signal"] = SwString(e.signal);
                o["typeName"] = SwString(e.typeName);
                arr.append(o);
            }
        } catch (...) {
        }
        return arr;
    }

    static SwJsonArray snapshotFresh(const SwString& domain) {
        dropCachedMapping_(domain);
        return snapshot(domain);
    }

private:
    static SwString registryName_(const SwString& domain) { return signalsRegistryNameForDomain_(domain); }
    static SwString registryMutexName_(const SwString& domain) { return signalsRegistryMutexNameForDomain_(domain); }

    class Mapping {
    public:
        /**
         * @brief Constructs a `Mapping` instance.
         * @param L Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit Mapping(Layout* L) : L_(L) {}
        /**
         * @brief Destroys the `Mapping` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~Mapping() {
#ifdef _WIN32
            if (mem_) {
                ::UnmapViewOfFile(mem_);
                mem_ = nullptr;
            }
            if (hMap_) {
                ::CloseHandle(hMap_);
                hMap_ = NULL;
            }
            if (hMtx_) {
                ::CloseHandle(hMtx_);
                hMtx_ = NULL;
            }
#else
            if (mem_) {
                ::munmap(mem_, sizeof(Layout));
                mem_ = nullptr;
            }
#endif
        }

        /**
         * @brief Returns the current layout.
         * @return The current layout.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Layout* layout() const { return L_; }

#ifdef _WIN32
        void* mem_{nullptr};
        HANDLE hMap_{NULL};
        HANDLE hMtx_{NULL};
#else
        void* mem_{nullptr};
#endif

    private:
        Layout* L_{nullptr};
    };

    static std::atomic_flag& mappingCacheLock_() {
        static std::atomic_flag gLock = ATOMIC_FLAG_INIT;
        return gLock;
    }

    static std::map<std::string, std::shared_ptr<Mapping>>& mappingCache_() {
        static std::map<std::string, std::shared_ptr<Mapping>> cache;
        return cache;
    }

    static void dropCachedMapping_(const SwString& domain) {
        ProcessHooksSpinGuard_ lk(mappingCacheLock_());
        mappingCache_().erase(domain.toStdString());
    }

    static std::shared_ptr<Mapping> openOrCreate_(const SwString& domain) {
        const std::string key = domain.toStdString();
        {
            ProcessHooksSpinGuard_ lk(mappingCacheLock_());
            auto& cache = mappingCache_();
            auto it = cache.find(key);
            if (it != cache.end() && it->second) return it->second;
        }

#ifdef _WIN32
        const std::string nameA = registryName_(domain).toStdString();
        HANDLE hMap = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                           static_cast<DWORD>(sizeof(Layout)), nameA.c_str());
        if (!hMap) return std::shared_ptr<Mapping>();
        void* mem = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Layout));
        if (!mem) {
            ::CloseHandle(hMap);
            return std::shared_ptr<Mapping>();
        }
        Layout* L = static_cast<Layout*>(mem);

        const std::string mtxName = registryMutexName_(domain).toStdString();
        HANDLE hMtx = ::CreateMutexA(NULL, FALSE, mtxName.c_str());

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        map->hMap_ = hMap;
        map->hMtx_ = hMtx; // may be NULL: registry is best-effort

        if (map->hMtx_) ::WaitForSingleObject(map->hMtx_, INFINITE);
        if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->count = 0;
        }
        if (L->reserved != Layout::kTimeBaseTag) {
            L->count = 0;
            std::memset(L->entries, 0, sizeof(L->entries));
            L->reserved = Layout::kTimeBaseTag;
        }
        if (map->hMtx_) ::ReleaseMutex(map->hMtx_);

        {
            ProcessHooksSpinGuard_ lk(mappingCacheLock_());
            mappingCache_()[key] = map;
        }
        return map;
#else
        const std::string nameA = registryName_(domain).toStdString();
        void* mem = openRegistryMemory_<Layout>(SwString(nameA));
        Layout* L = static_cast<Layout*>(mem);

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        {
            ProcessHooksSpinGuard_ lk(mappingCacheLock_());
            mappingCache_()[key] = map;
        }
        return map;
#endif
    }

    static void lock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return;
#ifdef _WIN32
        ::WaitForSingleObject(map->hMtx_, INFINITE);
#else
        (void)lockRegistryMemory_(map->layout(), false, [](Layout* L) {
            cleanupStale_locked_(L, nowMs());
        });
#endif
    }

    static void unlock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return;
#ifdef _WIN32
        ::ReleaseMutex(map->hMtx_);
#else
        pthread_mutex_unlock(&map->layout()->mtx);
#endif
    }

    static void removeAt_locked_(Layout& L, uint32_t i) {
        if (i >= L.count) return;
        const uint32_t last = (L.count > 0) ? (L.count - 1) : 0;
        if (i != last && last < MaxEntries) {
            L.entries[i] = L.entries[last];
        }
        if (L.count > 0) L.count--;
    }

    static void cleanupStale_locked_(Layout* L, uint64_t nowMs) {
        if (!L) return;
        PidProbePass_<MaxEntries> probes(currentPid());

        uint32_t i = 0;
        while (i < L->count && i < MaxEntries) {
            RegistryEntry& e = L->entries[i];
            const uint32_t pid = e.pid;
            const uint64_t ls = e.lastSeenMs;

            // Expiry affects visibility, not registration lifetime. Otherwise
            // one missed heartbeat deletes the rows that a resumed heartbeat
            // needs to refresh. Reclaim rows when their owning process exits.
            const bool stale = probes.stale(pid, ls, nowMs, kEntryTtlMs,
                                            false, pidStateBestEffort_);

            if (stale) {
                removeAt_locked_(*L, i);
                continue;
            }
            ++i;
        }
    }

    static bool tryLock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return false;
#ifdef _WIN32
        if (!map->hMtx_) return false;
        const DWORD wr = ::WaitForSingleObject(map->hMtx_, 0);
        return (wr == WAIT_OBJECT_0 || wr == WAIT_ABANDONED);
#else
        return lockRegistryMemory_(map->layout(), true, [](Layout* L) {
            cleanupStale_locked_(L, nowMs());
        });
#endif
    }

    static void touchCurrentPidBestEffort_(const SwString& domain) {
        try {
            std::shared_ptr<Mapping> map = openOrCreate_(domain);
            if (!map) return;
            if (!tryLock_(map)) return;

            Layout* L = map->layout();
            const uint64_t t = nowMs();
            if (L->reserved != Layout::kTimeBaseTag) L->reserved = Layout::kTimeBaseTag;
            cleanupStale_locked_(L, t);

            const uint32_t pid = currentPid();
            bool touched = false;
            for (uint32_t i = 0; i < L->count && i < MaxEntries; ++i) {
                RegistryEntry& e = L->entries[i];
                if (e.pid != pid) continue;
                e.lastSeenMs = t;
                touched = true;
            }

            unlock_(map);
            if (touched) {
                notifyRegistryChangedBestEffort_(domain);
            }
        } catch (...) {
        }
    }

    struct ProcessHooks {
        /**
         * @brief Performs the `ensureHeartbeat` operation.
         * @param domain Value passed to the method.
         * @return The requested ensure Heartbeat.
         */
        static void ensureHeartbeat(const SwString& domain) {
            const std::string key = domain.toStdString();
            ProcessHooksSpinGuard_ lk(lock_());
            if (heartbeats_().count(key)) return;

            const SwString domCopy = domain;
            SwEventLoop::RuntimeHandle h = SwEventLoop::installSlowRuntime(1000, [domCopy]() {
                RegistryTable::touchCurrentPidBestEffort_(domCopy);
            });
            heartbeats_()[key] = h;
        }

    private:
        static std::atomic_flag& lock_() {
            static std::atomic_flag flag = ATOMIC_FLAG_INIT;
            return flag;
        }

        static std::map<std::string, SwEventLoop::RuntimeHandle>& heartbeats_() {
            static std::map<std::string, SwEventLoop::RuntimeHandle> m;
            return m;
        }
    };
};

template <size_t MaxEntries = 512>
class SubscribersRegistryTable {
public:
    typedef SubscribersLayout<MaxEntries> Layout;
    static const uint64_t kEntryTtlMs = 15000; // must be >= heartbeat period

    /**
     * @brief Performs the `registerSubscription` operation.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     * @param signal Value passed to the method.
     * @param subPid Value passed to the method.
     * @return The requested register Subscription.
     */
    static void registerSubscription(const SwString& domain,
                                     const SwString& object,
                                     const SwString& signal,
                                     uint32_t subPid = currentPid()) {
        registerSubscription(domain, object, signal, subPid, currentSubscriberObject_());
    }

    /**
     * @brief Performs the `registerSubscription` operation.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     * @param signal Value passed to the method.
     * @param subPid Value passed to the method.
     * @param subscriberObject Value passed to the method.
     * @return The requested register Subscription.
     */
    static void registerSubscription(const SwString& domain,
                                     const SwString& object,
                                     const SwString& signal,
                                     uint32_t subPid,
                                     const SwString& subscriberObject) {
        try {
            ProcessHooks::ensureHeartbeat(domain);

            std::shared_ptr<Mapping> map = openOrCreate_(domain);
            if (!map) return;

            lock_(map);
            Layout* L = map->layout();

            const uint64_t h = subscriptionHash_(domain, object, signal, subPid, subscriberObject);
            const uint64_t t = nowMs();
            cleanupStale_locked_(L, t);

            for (uint32_t i = 0; i < L->count && i < MaxEntries; ++i) {
                SubscriberEntry& e = L->entries[i];
                if (e.hash != h) continue;
                e.lastSeenMs = t;
                e.subPid = subPid;
                if (e.refCount < 0xffffffffu) e.refCount++;
                copyTrunc(e.domain, sizeof(e.domain), domain);
                copyTrunc(e.object, sizeof(e.object), object);
                copyTrunc(e.signal, sizeof(e.signal), signal);
                packCstrAfterNul_(e.signal, sizeof(e.signal), subscriberObject);
                unlock_(map);
                return;
            }

            if (L->count < MaxEntries) {
                SubscriberEntry& e = L->entries[L->count++];
                std::memset(&e, 0, sizeof(e));
                e.hash = h;
                e.lastSeenMs = t;
                e.subPid = subPid;
                e.refCount = 1;
                copyTrunc(e.domain, sizeof(e.domain), domain);
                copyTrunc(e.object, sizeof(e.object), object);
                copyTrunc(e.signal, sizeof(e.signal), signal);
                packCstrAfterNul_(e.signal, sizeof(e.signal), subscriberObject);
            }

            unlock_(map);
        } catch (...) {
        }
    }

    /**
     * @brief Performs the `unregisterSubscription` operation.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     * @param signal Value passed to the method.
     * @param subPid Value passed to the method.
     * @return The requested unregister Subscription.
     */
    static void unregisterSubscription(const SwString& domain,
                                       const SwString& object,
                                       const SwString& signal,
                                       uint32_t subPid = currentPid()) {
        unregisterSubscription(domain, object, signal, subPid, SwString());
    }

    /**
     * @brief Performs the `unregisterSubscription` operation.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     * @param signal Value passed to the method.
     * @param subPid Value passed to the method.
     * @param subscriberObject Value passed to the method.
     * @return The requested unregister Subscription.
     */
    static void unregisterSubscription(const SwString& domain,
                                       const SwString& object,
                                       const SwString& signal,
                                       uint32_t subPid,
                                       const SwString& subscriberObject) {
        try {
            std::shared_ptr<Mapping> map = openOrCreate_(domain);
            if (!map) return;

            lock_(map);
            Layout* L = map->layout();
            cleanupStale_locked_(L, nowMs());

            const uint64_t h = subscriptionHash_(domain, object, signal, subPid, subscriberObject);

            uint32_t i = 0;
            while (i < L->count && i < MaxEntries) {
                SubscriberEntry& e = L->entries[i];
                if (e.hash != h) { ++i; continue; }
                if (e.refCount > 1) {
                    e.refCount--;
                } else {
                    removeAt_locked_(*L, i);
                }
                break;
            }

            unlock_(map);
        } catch (...) {
        }
    }

    /**
     * @brief Performs the `snapshot` operation.
     * @param domain Value passed to the method.
     * @return The requested snapshot.
     */
    static SwJsonArray snapshot(const SwString& domain) {
        SwJsonArray arr;
        try {
            std::shared_ptr<Mapping> map = openOrCreate_(domain);
            if (!map) return arr;

            std::vector<SubscriberEntry> rows(MaxEntries);
            lock_(map);
            Layout* L = map->layout();
            const uint64_t t = nowMs();
            cleanupStale_locked_(L, t);
            const bool allowTtl = L->reserved == Layout::kTimeBaseTag;
            const uint32_t n = (L->count < MaxEntries) ? L->count : static_cast<uint32_t>(MaxEntries);
            std::memcpy(rows.data(), L->entries, n * sizeof(SubscriberEntry));
            unlock_(map);
            for (uint32_t i = 0; i < n; ++i) {
                const SubscriberEntry& e = rows[i];
                if (allowTtl && t >= e.lastSeenMs && t - e.lastSeenMs > kEntryTtlMs) continue;
                SwJsonObject o;
                o["hash"] = hex64(e.hash);
                o["subPid"] = static_cast<int>(e.subPid);
                o["refCount"] = static_cast<int>(e.refCount);
                o["lastSeenMs"] = static_cast<double>(e.lastSeenMs);
                o["domain"] = SwString(e.domain);
                o["object"] = SwString(e.object);
                o["signal"] = SwString(e.signal);
                {
                    const SwString subObj = unpackCstrAfterNul_(e.signal, sizeof(e.signal));
                    if (!subObj.isEmpty()) {
                        const std::string dom = std::string(e.domain);
                        o["subObject"] = subObj;
                        o["subTarget"] = (dom.empty() ? domain : SwString(dom)) + "/" + subObj;
                    }
                }
                arr.append(o);
            }
        } catch (...) {
        }
        return arr;
    }
 
    // Returns the list of subscriber PIDs for a given (domain, object, signal).
    // Used by publishers to deliver a best-effort wakeup notification.
    /**
     * @brief Performs the `listSubscriberPids` operation.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     * @param signal Value passed to the method.
     * @param outPids Output value filled by the method.
     * @return The requested list Subscriber Pids.
     */
    static void listSubscriberPids(const SwString& domain,
                                  const SwString& object,
                                  const SwString& signal,
                                  std::vector<uint32_t>& outPids) {
        outPids.clear();
        try {
            std::shared_ptr<Mapping> map = openOrCreate_(domain);
            if (!map) return;

            const std::string dom = domain.toStdString();
            const std::string obj = object.toStdString();
            const std::string sig = signal.toStdString();

            struct Unlocker {
                std::shared_ptr<Mapping> map;
                /**
                 * @brief Constructs a `Unlocker` instance.
                 *
                 * @details The instance is initialized and prepared for immediate use.
                 */
                explicit Unlocker(std::shared_ptr<Mapping> m) : map(std::move(m)) {}
                /**
                 * @brief Destroys the `Unlocker` instance.
                 * @param map Value passed to the method.
                 *
                 * @details Use this hook to release any resources that remain associated with the instance.
                 */
                ~Unlocker() { unlock_(map); }
            };

            auto cstrEq = [](const char* buf, size_t cap, const std::string& s) -> bool {
                if (!buf) return false;
                size_t n = 0;
                while (n < cap && buf[n] != '\0') ++n;
                if (n != s.size()) return false;
                return std::memcmp(buf, s.data(), n) == 0;
            };

            lock_(map);
            Unlocker unlocker(map);
            Layout* L = map->layout();
            cleanupStale_locked_(L, nowMs());

            const uint32_t n = (L->count < MaxEntries) ? L->count : static_cast<uint32_t>(MaxEntries);
            for (uint32_t i = 0; i < n; ++i) {
                const SubscriberEntry& e = L->entries[i];
                if (e.subPid == 0) continue;
                if (!cstrEq(e.domain, sizeof(e.domain), dom)) continue;
                if (!cstrEq(e.object, sizeof(e.object), obj)) continue;
                if (!cstrEq(e.signal, sizeof(e.signal), sig)) continue;
                outPids.push_back(e.subPid);
            }
        } catch (...) {
        }
    }

private:
    static uint64_t subscriptionHash_(const SwString& domain,
                                     const SwString& object,
                                     const SwString& signal,
                                     uint32_t subPid,
                                     const SwString& subscriberObject) {
        std::string key = domain.toStdString() + "|" + object.toStdString() + "|" + signal.toStdString() +
                          "|sub|" + std::to_string(subPid);
        if (!subscriberObject.isEmpty()) {
            key += "|subObject|" + subscriberObject.toStdString();
        }
        return fnv1a64(key);
    }

    static SwString registryName_(const SwString& domain) { return subscribersRegistryNameForDomain_(domain); }
    static SwString registryMutexName_(const SwString& domain) { return subscribersRegistryMutexNameForDomain_(domain); }

    class Mapping {
    public:
        /**
         * @brief Constructs a `Mapping` instance.
         * @param L Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit Mapping(Layout* L) : L_(L) {}
        /**
         * @brief Destroys the `Mapping` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~Mapping() {
#ifdef _WIN32
            if (mem_) {
                ::UnmapViewOfFile(mem_);
                mem_ = nullptr;
            }
            if (hMap_) {
                ::CloseHandle(hMap_);
                hMap_ = NULL;
            }
            if (hMtx_) {
                ::CloseHandle(hMtx_);
                hMtx_ = NULL;
            }
#else
            if (mem_) {
                ::munmap(mem_, sizeof(Layout));
                mem_ = nullptr;
            }
#endif
        }

        /**
         * @brief Returns the current layout.
         * @return The current layout.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Layout* layout() const { return L_; }

#ifdef _WIN32
        void* mem_{nullptr};
        HANDLE hMap_{NULL};
        HANDLE hMtx_{NULL};
#else
        void* mem_{nullptr};
#endif

    private:
        Layout* L_{nullptr};
    };

    static std::shared_ptr<Mapping> openOrCreate_(const SwString& domain) {
        static std::atomic_flag gLock = ATOMIC_FLAG_INIT;
        static std::map<std::string, std::shared_ptr<Mapping>> cache;

        const std::string key = domain.toStdString();
        {
            ProcessHooksSpinGuard_ lk(gLock);
            auto it = cache.find(key);
            if (it != cache.end() && it->second) return it->second;
        }

#ifdef _WIN32
        const std::string nameA = registryName_(domain).toStdString();
        HANDLE hMap = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                           static_cast<DWORD>(sizeof(Layout)), nameA.c_str());
        if (!hMap) return std::shared_ptr<Mapping>();
        void* mem = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Layout));
        if (!mem) {
            ::CloseHandle(hMap);
            return std::shared_ptr<Mapping>();
        }
        Layout* L = static_cast<Layout*>(mem);

        const std::string mtxName = registryMutexName_(domain).toStdString();
        HANDLE hMtx = ::CreateMutexA(NULL, FALSE, mtxName.c_str());

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        map->hMap_ = hMap;
        map->hMtx_ = hMtx; // may be NULL: registry is best-effort

        if (map->hMtx_) ::WaitForSingleObject(map->hMtx_, INFINITE);
        if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->count = 0;
        }
        if (L->reserved != Layout::kTimeBaseTag) {
            L->count = 0;
            std::memset(L->entries, 0, sizeof(L->entries));
            L->reserved = Layout::kTimeBaseTag;
        }
        if (map->hMtx_) ::ReleaseMutex(map->hMtx_);

        {
            ProcessHooksSpinGuard_ lk(gLock);
            cache[key] = map;
        }
        return map;
#else
        const std::string nameA = registryName_(domain).toStdString();
        void* mem = openRegistryMemory_<Layout>(SwString(nameA));
        Layout* L = static_cast<Layout*>(mem);

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        {
            ProcessHooksSpinGuard_ lk(gLock);
            cache[key] = map;
        }
        return map;
#endif
    }

    static void lock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return;
#ifdef _WIN32
        if (map->hMtx_) ::WaitForSingleObject(map->hMtx_, INFINITE);
#else
        (void)lockRegistryMemory_(map->layout(), false, [](Layout* L) {
            cleanupStale_locked_(L, nowMs());
        });
#endif
    }

    static bool tryLock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return false;
#ifdef _WIN32
        if (!map->hMtx_) return false;
        const DWORD wr = ::WaitForSingleObject(map->hMtx_, 0);
        return (wr == WAIT_OBJECT_0 || wr == WAIT_ABANDONED);
#else
        return lockRegistryMemory_(map->layout(), true, [](Layout* L) {
            cleanupStale_locked_(L, nowMs());
        });
#endif
    }

    static void unlock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return;
#ifdef _WIN32
        if (map->hMtx_) ::ReleaseMutex(map->hMtx_);
#else
        pthread_mutex_unlock(&map->layout()->mtx);
#endif
    }

    static void removeAt_locked_(Layout& L, uint32_t i) {
        if (i >= L.count) return;
        const uint32_t last = (L.count > 0) ? (L.count - 1) : 0;
        if (i != last && last < MaxEntries) {
            L.entries[i] = L.entries[last];
        }
        if (L.count > 0) L.count--;
    }

    static void cleanupStale_locked_(Layout* L, uint64_t nowMs) {
        if (!L) return;
        PidProbePass_<MaxEntries> probes(currentPid());

        uint32_t i = 0;
        while (i < L->count && i < MaxEntries) {
            SubscriberEntry& e = L->entries[i];
            const uint32_t pid = e.subPid;
            const uint64_t ls = e.lastSeenMs;

            // Keep a live receiver's subscriptions (and reference counts)
            // across pauses. Publishers must still be able to wake it up.
            const bool stale = e.refCount == 0 ||
                probes.stale(pid, ls, nowMs, kEntryTtlMs, false, pidStateBestEffort_);

            if (stale) {
                removeAt_locked_(*L, i);
                continue;
            }
            ++i;
        }
    }

    static void touchCurrentPidBestEffort_(const SwString& domain) {
        try {
            std::shared_ptr<Mapping> map = openOrCreate_(domain);
            if (!map) return;
            if (!tryLock_(map)) return;

            Layout* L = map->layout();
            const uint64_t t = nowMs();
            if (L->reserved != Layout::kTimeBaseTag) L->reserved = Layout::kTimeBaseTag;
            cleanupStale_locked_(L, t);

            const uint32_t pid = currentPid();
            for (uint32_t i = 0; i < L->count && i < MaxEntries; ++i) {
                SubscriberEntry& e = L->entries[i];
                if (e.subPid != pid) continue;
                e.lastSeenMs = t;
            }

            unlock_(map);
        } catch (...) {
        }
    }

    struct ProcessHooks {
        /**
         * @brief Performs the `ensureHeartbeat` operation.
         * @param domain Value passed to the method.
         * @return The requested ensure Heartbeat.
         */
        static void ensureHeartbeat(const SwString& domain) {
            const std::string key = domain.toStdString();
            ProcessHooksSpinGuard_ lk(lock_());
            if (heartbeats_().count(key)) return;

            const SwString domCopy = domain;
            SwEventLoop::RuntimeHandle h = SwEventLoop::installSlowRuntime(1000, [domCopy]() {
                SubscribersRegistryTable::touchCurrentPidBestEffort_(domCopy);
            });
            heartbeats_()[key] = h;
        }

    private:
        static std::atomic_flag& lock_() {
            static std::atomic_flag flag = ATOMIC_FLAG_INIT;
            return flag;
        }

        static std::map<std::string, SwEventLoop::RuntimeHandle>& heartbeats_() {
            static std::map<std::string, SwEventLoop::RuntimeHandle> m;
            return m;
        }
    };
};

template <class... Args>
inline uint64_t type_id() {
#if defined(__clang__) || defined(__GNUC__)
    static const uint64_t id = fnv1a64(__PRETTY_FUNCTION__);
#elif defined(_MSC_VER)
    static const uint64_t id = fnv1a64(__FUNCSIG__);
#else
    static const uint64_t id = fnv1a64("sw::ipc::detail::type_id<unknown>");
#endif
    return id;
}

template <class... Args>
inline SwString type_name() {
    return wireTupleName<Args...>();
}

// Byte framing and all typed serialization belong to SwAny.
using Encoder = SwAny::BinaryWriter;
using Decoder = SwAny::BinaryReader;

inline bool writeAll(Encoder&) { return true; }
template <typename T, typename... Rest>
inline bool writeAll(Encoder& enc, const T& v, const Rest&... rest) {
    return SwAny::serializeBinary(enc, v) && writeAll(enc, rest...);
}

inline bool readAll(Decoder&) { return true; }
template <typename T, typename... Rest>
inline bool readAll(Decoder& dec, T& out, Rest&... rest) {
    return SwAny::deserializeBinary(dec, out) && readAll(dec, rest...);
}

#ifdef _WIN32
typedef BOOL (WINAPI *WaitOnAddressFn)(volatile VOID* Address, PVOID CompareAddress, SIZE_T AddressSize, DWORD dwMilliseconds);
typedef VOID (WINAPI *WakeByAddressAllFn)(PVOID Address);

inline WaitOnAddressFn win_wait_on_address_fn() {
    static WaitOnAddressFn fn = NULL;
    static bool inited = false;
    if (!inited) {
        inited = true;
        HMODULE h = ::GetModuleHandleA("kernel32.dll");
        if (h) fn = reinterpret_cast<WaitOnAddressFn>(::GetProcAddress(h, "WaitOnAddress"));
    }
    return fn;
}

inline WakeByAddressAllFn win_wake_by_address_all_fn() {
    static WakeByAddressAllFn fn = NULL;
    static bool inited = false;
    if (!inited) {
        inited = true;
        HMODULE h = ::GetModuleHandleA("kernel32.dll");
        if (h) fn = reinterpret_cast<WakeByAddressAllFn>(::GetProcAddress(h, "WakeByAddressAll"));
    }
    return fn;
}

inline void win_wake_by_address_all(void* addr) {
    WakeByAddressAllFn fn = win_wake_by_address_all_fn();
    if (fn) fn(addr);
}

inline void win_wait_on_address(volatile void* addr, void* compare, size_t size, DWORD timeoutMs) {
    WaitOnAddressFn fn = win_wait_on_address_fn();
    if (fn) {
        fn(const_cast<volatile VOID*>(reinterpret_cast<volatile VOID const*>(addr)),
           compare,
           static_cast<SIZE_T>(size),
           timeoutMs);
        return;
    }
    // Fallback (no kernel wait available): yield the CPU slice.
    std::this_thread::yield();
}
#endif

inline uint64_t atomic_load_u64(const uint64_t* p) {
#ifdef _WIN32
    return static_cast<uint64_t>(
        ::InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(const_cast<uint64_t*>(p)), 0, 0));
#elif defined(__clang__) || defined(__GNUC__)
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#else
    return *p;
#endif
}

// -----------------------------------------------------------------------------
// Shared "loop" dispatcher: event-driven wakeup (no polling)
// -----------------------------------------------------------------------------
struct LoopPollerDispatchRegistry {
    // Cross-module dispatcher list (EXE + DLLs) for this process.
    //
    // Why: core is header-only, so each module gets its own LoopPoller singleton.
    // We keep a small shared table of dispatch function pointers so a single OS wakeup
    // can dispatch *all* LoopPollers (and avoid starvation with auto-reset events).
    static const uint32_t kMagic = 0x44535031u;   // 'DSP1'
    static const uint32_t kVersion = 1;
    static const size_t kMax = 64;

    struct Table {
        uint32_t magic;
        uint32_t version;
        uintptr_t fns[kMax];
    };

    /**
     * @brief Returns the current table.
     * @return The current table.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static Table* table() {
        static Table* s_tbl = nullptr;
        if (s_tbl) return s_tbl;

        const uint32_t pid = currentPid();
#ifdef _WIN32
        const std::string name = std::string("Local\\sw_ipc_dispatch_tbl_v1_") + std::to_string(pid);
        static HANDLE s_mapping = nullptr;
        if (!s_mapping) {
            s_mapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                             static_cast<DWORD>(sizeof(Table)), name.c_str());
        }
        if (!s_mapping) return nullptr;
        s_tbl = reinterpret_cast<Table*>(
            ::MapViewOfFile(s_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Table)));
        if (!s_tbl) return nullptr;
#else
        std::string shmName = std::string("/sw_ipc_dispatch_tbl_v1_") + std::to_string(pid);
#if defined(__linux__) && !defined(__ANDROID__)
        static std::shared_ptr<SharedMemoryLease> lease;
        SharedMemoryLease::recoverAtProcessStart();
        SharedMemoryNamespaceLock namespaceLock;
        if (!lease) lease = std::make_shared<SharedMemoryLease>(shmName);
#endif
        static int s_fd = -1;
        if (s_fd < 0) {
            s_fd = ::shm_open(shmName.c_str(), O_RDWR | O_CREAT, 0666);
        }
        if (s_fd < 0) return nullptr;
        ensureSharedMemoryPermissions_(s_fd);
        if (sw::ipc::detail::reserveSharedMemory_(s_fd, static_cast<off_t>(sizeof(Table))) != 0) return nullptr;
        void* mem = ::mmap(NULL, sizeof(Table), PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, 0);
        if (mem == MAP_FAILED) return nullptr;
        s_tbl = reinterpret_cast<Table*>(mem);
#endif

        if (s_tbl->magic != kMagic || s_tbl->version != kVersion) {
            s_tbl->magic = kMagic;
            s_tbl->version = kVersion;
            for (size_t i = 0; i < kMax; ++i) {
                s_tbl->fns[i] = 0;
            }
        }
        return s_tbl;
    }

    /**
     * @brief Performs the `registerFn` operation.
     * @return The requested register Fn.
     */
    static void registerFn(void (*fn)()) {
        if (!fn) return;
        Table* t = table();
        if (!t) return;

        const uintptr_t v = reinterpret_cast<uintptr_t>(fn);
        if (v == 0) return;

        // First pass: already present?
        for (size_t i = 0; i < kMax; ++i) {
#ifdef _WIN32
            const uintptr_t cur =
                reinterpret_cast<uintptr_t>(::InterlockedCompareExchangePointer(
                    reinterpret_cast<PVOID volatile*>(&t->fns[i]), nullptr, nullptr));
#elif defined(__clang__) || defined(__GNUC__)
            const uintptr_t cur = __atomic_load_n(&t->fns[i], __ATOMIC_ACQUIRE);
#else
            const uintptr_t cur = t->fns[i];
#endif
            if (cur == v) return;
        }

        // Second pass: try to claim an empty slot (best-effort, duplicates are acceptable).
        for (size_t i = 0; i < kMax; ++i) {
#ifdef _WIN32
            const PVOID prev = ::InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(&t->fns[i]),
                reinterpret_cast<PVOID>(v),
                nullptr);
            if (prev == nullptr) return;
#elif defined(__clang__) || defined(__GNUC__)
            uintptr_t expected = 0;
            if (__atomic_compare_exchange_n(&t->fns[i], &expected, v, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                return;
            }
#else
            if (t->fns[i] == 0) {
                t->fns[i] = v;
                return;
            }
#endif
        }
    }

    /**
     * @brief Returns the current dispatch All.
     * @return The current dispatch All.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static void dispatchAll() {
        Table* t = table();
        if (!t) return;
        for (size_t i = 0; i < kMax; ++i) {
#ifdef _WIN32
            const uintptr_t cur =
                reinterpret_cast<uintptr_t>(::InterlockedCompareExchangePointer(
                    reinterpret_cast<PVOID volatile*>(&t->fns[i]), nullptr, nullptr));
#elif defined(__clang__) || defined(__GNUC__)
            const uintptr_t cur = __atomic_load_n(&t->fns[i], __ATOMIC_ACQUIRE);
#else
            const uintptr_t cur = t->fns[i];
#endif
            if (!cur) continue;
            void (*fn)() = reinterpret_cast<void (*)()>(cur);
            if (fn) fn();
        }
    }

    /**
     * @brief Performs the `unregisterFn` operation.
     * @return The requested unregister Fn.
     */
    static void unregisterFn(void (*fn)()) {
        if (!fn) return;
        Table* t = table();
        if (!t) return;

        const uintptr_t v = reinterpret_cast<uintptr_t>(fn);
        if (v == 0) return;

        for (size_t i = 0; i < kMax; ++i) {
#ifdef _WIN32
            (void)::InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(&t->fns[i]),
                nullptr,
                reinterpret_cast<PVOID>(v));
#elif defined(__clang__) || defined(__GNUC__)
            uintptr_t expected = v;
            (void)__atomic_compare_exchange_n(&t->fns[i], &expected, 0, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#else
            if (t->fns[i] == v) t->fns[i] = 0;
#endif
        }
    }
};

class LoopPoller {
    class IpcWakeup;
public:
    struct Task {
        std::atomic_bool active;
        /**
         * @brief Returns the current function<void.
         * @return The current function<void.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        std::function<void()> fn;
        /**
         * @brief Constructs a `Task` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Task(std::function<void()> f) : active(true), fn(std::move(f)) {}
    };

    /**
     * @brief Performs the `dispatch` operation.
     */
    void dispatch() { tick_(); }
    /**
     * @brief Performs the `notifyProcess` operation.
     * @param pid Value passed to the method.
     * @return The requested notify Process.
     */
    static void notifyProcess(uint32_t pid);

    /**
     * @brief Adds the specified add.
     * @param fn Value passed to the method.
     * @return The requested add.
     */
    size_t add(std::function<void()> fn) {
        LoopPollerDispatchRegistry::registerFn(&LoopPoller::dispatchSelf_);
        std::shared_ptr<Task> t(new Task(std::move(fn)));
        size_t id = 0;
        {
            ProcessHooksSpinGuard_ lk(lock_);
            id = nextId_++;
            tasks_[id] = t;
        }
        ensureIpcWakeup_();
        return id;
    }

    /**
     * @brief Removes the specified remove.
     * @param id Value passed to the method.
     */
    void remove(size_t id) {
        std::shared_ptr<Task> t;
        bool shouldDetach = false;
        {
            ProcessHooksSpinGuard_ lk(lock_);
            auto it = tasks_.find(id);
            if (it == tasks_.end()) return;
            t = it->second;
            tasks_.erase(it);
            shouldDetach = tasks_.empty();
            if (shouldDetach) {
                // When a module's last LoopPoller task is removed, detach OS wakeups and unregister
                // its dispatcher so the plugin DLL can be unloaded safely.
                LoopPollerDispatchRegistry::unregisterFn(&LoopPoller::dispatchSelf_);
                IpcWakeup::instance().detach();
            }
        }
        if (t) t->active.store(false, std::memory_order_release);
    }

    /**
     * @brief Returns the current instance.
     * @return The current instance.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static LoopPoller& instance() {
        static LoopPoller* p = new LoopPoller();
        return *p;
    }

private:
    LoopPoller() = default;

    static void dispatchSelf_() { LoopPoller::instance().dispatch(); }

    class IpcWakeup {
    public:
        /**
         * @brief Returns the current instance.
         * @return The current instance.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        static IpcWakeup& instance() {
            static IpcWakeup* n = new IpcWakeup();
            return *n;
        }

        /**
         * @brief Performs the `ensureAttached` operation.
         */
        void ensureAttached() {
            if (attached_.load(std::memory_order_acquire)) return;
            ProcessHooksSpinGuard_ lk(lock_);
            if (attached_.load(std::memory_order_relaxed)) return;
            attachLocked_();
        }

        /**
         * @brief Performs the `detach` operation.
         */
        void detach() {
            if (!attached_.load(std::memory_order_acquire)) return;
            ProcessHooksSpinGuard_ lk(lock_);
            if (!attached_.load(std::memory_order_relaxed)) return;

            SwCoreApplication* app = SwCoreApplication::instance(false);
            if (app && watchToken_ != 0) {
                app->ioDispatcher().remove(watchToken_);
            }
            watchToken_ = 0;

#ifdef _WIN32
            if (event_) {
                ::CloseHandle(event_);
                event_ = NULL;
            }
#else
            if (sock_ >= 0) {
                ::close(sock_);
                sock_ = -1;
            }
#endif

            attached_.store(false, std::memory_order_release);
        }

        /**
         * @brief Performs the `notifyPid` operation.
         * @param pid Value passed to the method.
         * @return The requested notify Pid.
         */
        static void notifyPid(uint32_t pid) {
#ifdef _WIN32
            const std::string name = std::string("Local\\sw_ipc_notify_") + std::to_string(pid);
            HANDLE h = ::CreateEventA(NULL, FALSE, FALSE, name.c_str());
            if (!h) return;
            ::SetEvent(h);
            ::CloseHandle(h);
#else
            int s = senderSocket_();
            if (s < 0) return;
            sockaddr_un addr;
            socklen_t len = 0;
            fillAddr_(pid, addr, len);
            const uint8_t b = 1;
            const ssize_t n = ::sendto(s, &b, sizeof(b), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&addr), len);
            (void)n;
#endif
        }

    private:
        IpcWakeup() = default;

// EXCEPTION DOCUMENTEE (Tâche 15): Les sockets AF_UNIX utilisées ici pour la signalisation IPC
// inter-processus (abstract namespace Linux) constituent une exception acceptée au paradigme SwObject.
// Elles ne peuvent pas être remplacées par SwUdpSocket ou un autre type Sw existant car :
// - Elles utilisent l'espace de nommage abstrait Linux (sun_path[0] == '\0'), non portable
// - La sémantique est différente de UDP réseau (pas de résolution d'adresse, broadcast local)
// - Créer un SwLocalSocket nécessiterait un chantier architectural à part entière
// Si SwLocalSocket est créé à l'avenir, cette section devra être refactorisée.
#ifndef _WIN32
        static int senderSocket_() {
            static std::atomic<int> socket{-1};
            int descriptor = socket.load(std::memory_order_acquire);
            if (descriptor >= 0) return descriptor;
            static std::mutex creation;
            std::lock_guard<std::mutex> lock(creation);
            descriptor = socket.load(std::memory_order_relaxed);
            if (descriptor < 0) {
                descriptor = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
                socket.store(descriptor, std::memory_order_release);
            }
            return descriptor;
        }

        static void fillAddr_(uint32_t pid, sockaddr_un& out, socklen_t& lenOut) {
            std::memset(&out, 0, sizeof(out));
            out.sun_family = AF_UNIX;
            // Format the existing abstract address directly in its fixed
            // buffer, without allocating a temporary string for every wakeup.
            static constexpr char prefix[] = "sw_ipc_notify_";
            static constexpr size_t prefixLength = sizeof(prefix) - 1;
            static_assert(sizeof(out.sun_path) >= prefixLength + 12, "IPC address buffer too small");
            std::memcpy(out.sun_path + 1, prefix, prefixLength);
            // uint32_t needs at most 10 decimal digits. Keep this path
            // allocation-free without requiring C++17's std::to_chars.
            char digits[10];
            char* first = digits + sizeof(digits);
            do {
                *--first = static_cast<char>('0' + pid % 10);
                pid /= 10;
            } while (pid != 0);
            const size_t digitCount = static_cast<size_t>(digits + sizeof(digits) - first);
            std::memcpy(out.sun_path + 1 + prefixLength, first, digitCount);
            lenOut = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                           1 + prefixLength + digitCount);
        }
#endif

        void attachLocked_() {
            SwCoreApplication* app = SwCoreApplication::instance(false);
            if (!app) return;
            const auto controlPoster = [app](std::function<void()> task) {
                if (app) {
                    app->postEventOnLane(std::move(task), SwFiberLane::Control);
                    return;
                }
                task();
            };

            const uint32_t pid = currentPid();
#ifdef _WIN32
            const std::string name = std::string("Local\\sw_ipc_notify_") + std::to_string(pid);
            event_ = ::CreateEventA(NULL, FALSE, FALSE, name.c_str());
            if (!event_) return;
            watchToken_ = app->ioDispatcher().watchHandle(
                event_,
                controlPoster,
                []() {
                    // Coalesced wakeups: a single dispatch handles all pending signals.
                    LoopPollerDispatchRegistry::dispatchAll();
                });
#else
            sock_ = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            if (sock_ < 0) return;
            sockaddr_un addr;
            socklen_t len = 0;
            fillAddr_(pid, addr, len);
            if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
                ::close(sock_);
                sock_ = -1;
                return;
            }

            const int fdCopy = sock_;
            watchToken_ = app->ioDispatcher().watchFd(
                sock_,
                SwIoDispatcher::Readable,
                controlPoster,
                [fdCopy](uint32_t events) {
                    if ((events & SwIoDispatcher::Readable) == 0) {
                        return;
                    }
                    // Drain datagrams (best-effort) then dispatch.
                    uint8_t buf[64];
                    while (true) {
                        const ssize_t n = ::recv(fdCopy, buf, sizeof(buf), MSG_DONTWAIT);
                        if (n > 0) continue;
                        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                        break;
                    }
                    LoopPollerDispatchRegistry::dispatchAll();
                });
#endif

            if (watchToken_ != 0) {
                attached_.store(true, std::memory_order_release);
            }
        }

        std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
        std::atomic_bool attached_{false};
        SwIoDispatcher::Token watchToken_{0};

#ifdef _WIN32
        HANDLE event_{NULL};
#else
        int sock_{-1};
#endif
    };

    void ensureIpcWakeup_() {
        IpcWakeup::instance().ensureAttached();

        // FireInitial must happen even if no publish occurs; schedule one local dispatch.
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (app) {
            app->postEvent([]() { LoopPollerDispatchRegistry::dispatchAll(); });
        }
    }

    void tick_() {
        std::vector<std::shared_ptr<Task>> snapshot;
        {
            ProcessHooksSpinGuard_ lk(lock_);
            snapshot.reserve(tasks_.size());
            for (auto& kv : tasks_) snapshot.push_back(kv.second);
        }
        for (size_t i = 0; i < snapshot.size(); ++i) {
            const std::shared_ptr<Task>& t = snapshot[i];
            if (!t) continue;
            if (!t->active.load(std::memory_order_acquire)) continue;
            t->fn();
        }
    }

    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    size_t nextId_{1};
    std::map<size_t, std::shared_ptr<Task>> tasks_;
};

inline void LoopPoller::notifyProcess(uint32_t pid) { IpcWakeup::notifyPid(pid); }

template <size_t... I>
struct index_sequence {};

template <size_t N, size_t... I>
struct make_index_sequence_impl : make_index_sequence_impl<N - 1, N - 1, I...> {};

template <size_t... I>
struct make_index_sequence_impl<0, I...> {
    typedef index_sequence<I...> type;
};

template <size_t N>
struct make_index_sequence {
    typedef typename make_index_sequence_impl<N>::type type;
};

template <typename Tuple, size_t I>
struct tuple_element_decay {
    typedef typename std::decay<typename std::tuple_element<I, Tuple>::type>::type type;
};

template <typename Tuple, size_t I = 0>
inline typename std::enable_if<I == std::tuple_size<Tuple>::value, bool>::type
readTuple(Decoder&, Tuple&) {
    return true;
}

template <typename Tuple, size_t I = 0>
inline typename std::enable_if<I < std::tuple_size<Tuple>::value, bool>::type
readTuple(Decoder& dec, Tuple& t) {
    typedef typename tuple_element_decay<Tuple, I>::type T;
    T& elem = std::get<I>(t);
    if (!SwAny::deserializeBinary(dec, elem)) return false;
    return readTuple<Tuple, I + 1>(dec, t);
}

template <typename Fn, typename Tuple, size_t... I>
inline void invokeWithTupleImpl(Fn& fn, Tuple& t, index_sequence<I...>) {
    fn(std::get<I>(t)...);
}

template <typename Fn, typename Tuple>
inline void invokeWithTuple(Fn& fn, Tuple& t) {
    invokeWithTupleImpl(fn, t, typename make_index_sequence<std::tuple_size<Tuple>::value>::type());
}

} // namespace detail

class Registry {
public:
    /**
     * @brief Constructs a `Registry` instance.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    Registry(const SwString& domain, const SwString& object)
        : domain_(domain), object_(object) {}

    /**
     * @brief Returns the current domain.
     * @return The current domain.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& domain() const { return domain_; }
    /**
     * @brief Returns the current object.
     * @return The current object.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& object() const { return object_; }

private:
    SwString domain_;
    SwString object_;
};

// Best-effort snapshot of running "soft" (domain) registry: who is alive (pid list, lastSeen).
// Segment name: "sw_ipc_registry".
inline SwJsonArray shmAppsSnapshot() {
    return detail::AppsRegistryTable<>::snapshot();
}

// Best-effort snapshot of a domain signals registry (signals/config channels created so far).
// Segment name: "sw_ipc_registry_r3_<domain>".
inline SwJsonArray shmRegistrySnapshot(const SwString& domain) {
    return detail::RegistryTable<>::snapshot(domain);
}

inline SwJsonArray shmRegistrySnapshotFresh(const SwString& domain) {
    return detail::RegistryTable<>::snapshotFresh(domain);
}

inline SwString shmRegistrySegmentName(const SwString& domain) {
    return detail::signalsRegistryNameForDomain_(domain);
}

// Best-effort snapshot of a domain subscribers registry (who is connected to what).
// Segment name: "sw_ipc_subs_r3_<domain>".
inline SwJsonArray shmSubscribersSnapshot(const SwString& domain) {
    return detail::SubscribersRegistryTable<>::snapshot(domain);
}

// -------------------------------------------------------------------------
// Ring queue: multi-producer / single-consumer (shared-memory)
// -------------------------------------------------------------------------

template <size_t MaxPayload, size_t Capacity>
struct ShmQueueLayout {
    uint32_t magic;
    uint32_t version;
    uint64_t typeId;

#ifndef _WIN32
    pthread_mutex_t mtx;
    pthread_cond_t cv;
#endif

    // Writer sequence (last published item) and reader sequence (last consumed item).
    // Invariants (when all parties are well-behaved):
    //   - readSeq <= seq
    //   - (seq - readSeq) <= Capacity
    uint64_t seq;
    uint64_t readSeq;

    // Windows fallback when WaitOnAddress is not available:
    // number of consumers currently waiting on the semaphore.
    uint32_t winWaiters;
    uint32_t reserved0;

    struct Slot {
        uint64_t seq;
        uint32_t size;
        uint8_t data[MaxPayload];
    };

    Slot entries[Capacity];
    uint8_t reserved[64];

    static const uint32_t kMagic = 0x51554531u; // 'QUE1'
    static const uint32_t kVersion = 1;

    /**
     * @brief Performs the `initLayout` operation.
     * @param L Value passed to the method.
     * @return The requested init Layout.
     */
    static void initLayout(ShmQueueLayout* L) {
        if (!L) return;
        L->seq = 0;
        L->readSeq = 0;
        L->winWaiters = 0;
        L->reserved0 = 0;
        for (size_t i = 0; i < Capacity; ++i) {
            L->entries[i].seq = 0;
            L->entries[i].size = 0;
        }
    }
};

#include "ipc/FixedMapping.inl"

template <size_t Capacity, class... Args>
class RingQueue {
public:
    static const size_t kMaxPayload = 4096;
    typedef ShmQueueLayout<kMaxPayload, Capacity> Layout;
    typedef ShmMappingT<Layout> Mapping;

    class Subscription {
    public:
        /**
         * @brief Constructs a `Subscription` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Subscription() {}
        /**
         * @brief Constructs a `Subscription` instance.
         * @param pollerId Value passed to the method.
         * @param domain Value passed to the method.
         * @param object Value passed to the method.
         * @param signal Value passed to the method.
         * @param subPid Value passed to the method.
         * @param true Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Subscription(size_t pollerId,
                     const SwString& domain,
                     const SwString& object,
                     const SwString& signal,
                     uint32_t subPid,
                     const SwString& subscriberObject)
            : pollerId_(pollerId),
              domain_(domain),
              object_(object),
              signal_(signal),
              subPid_(subPid),
              subscriberObject_(subscriberObject),
              registered_(true) {}

#ifdef _WIN32
        /**
         * @brief Constructs a `Subscription` instance.
         * @param app Value passed to the method.
         * @param waitableId Value passed to the method.
         * @param domain Value passed to the method.
         * @param object Value passed to the method.
         * @param signal Value passed to the method.
         * @param subPid Value passed to the method.
         * @param true Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Subscription(SwCoreApplication* app,
                     SwIoDispatcher::Token watchToken,
                     const SwString& domain,
                     const SwString& object,
                     const SwString& signal,
                     uint32_t subPid,
                     const SwString& subscriberObject)
            : watchToken_(watchToken),
              app_(app),
              domain_(domain),
              object_(object),
              signal_(signal),
              subPid_(subPid),
              subscriberObject_(subscriberObject),
              registered_(true) {}
#endif

        /**
         * @brief Constructs a `Subscription` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Subscription(const Subscription&) = delete;
        /**
         * @brief Performs the `operator=` operation.
         * @return The requested operator =.
         */
        Subscription& operator=(const Subscription&) = delete;

        /**
         * @brief Constructs a `Subscription` instance.
         * @param this Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Subscription(Subscription&& o) noexcept { *this = std::move(o); }
        /**
         * @brief Performs the `operator=` operation.
         * @param o Value passed to the method.
         * @return The requested operator =.
         */
        Subscription& operator=(Subscription&& o) noexcept {
            if (this == &o) return *this;
            stop();
            pollerId_ = o.pollerId_;
            o.pollerId_ = 0;
#ifdef _WIN32
            watchToken_ = o.watchToken_;
            o.watchToken_ = 0;
            app_ = o.app_;
            o.app_ = nullptr;
#endif
            domain_ = std::move(o.domain_);
            object_ = std::move(o.object_);
            signal_ = std::move(o.signal_);
            subPid_ = o.subPid_;
            o.subPid_ = 0;
            subscriberObject_ = std::move(o.subscriberObject_);
            registered_ = o.registered_;
            o.registered_ = false;
            return *this;
        }

        /**
         * @brief Destroys the `Subscription` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~Subscription() { stop(); }

        /**
         * @brief Stops the underlying activity managed by the object.
         *
         * @details The call affects the runtime state associated with the underlying resource or service.
         */
        void stop() {
            if (!pollerId_
#ifdef _WIN32
                && !watchToken_
#endif
            ) {
                return;
            }

            if (registered_) {
                detail::SubscribersRegistryTable<>::unregisterSubscription(domain_, object_, signal_, subPid_, subscriberObject_);
                registered_ = false;
            }

#ifdef _WIN32
            if (watchToken_ && app_) {
                app_->ioDispatcher().remove(watchToken_);
                watchToken_ = 0;
            }
#endif

            if (pollerId_) {
                detail::LoopPoller::instance().remove(pollerId_);
                pollerId_ = 0;
            }
        }

    private:
        size_t pollerId_{0};
#ifdef _WIN32
        SwIoDispatcher::Token watchToken_{0};
        SwCoreApplication* app_{nullptr};
#endif
        SwString domain_;
        SwString object_;
        SwString signal_;
        uint32_t subPid_{0};
        SwString subscriberObject_;
        bool registered_{false};
    };

    /**
     * @brief Constructs a `RingQueue` instance.
     * @param reg Value passed to the method.
     * @param signalName Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    RingQueue(Registry& reg, const SwString& signalName)
        : reg_(reg), signalName_(signalName) {
        shmName_ = detail::make_shm_name(reg_.domain(), reg_.object(), signalName_);
        map_ = Mapping::openOrCreate(shmName_, detail::type_id<Args...>());
        detail::RegistryTable<>::registerSignal(reg_.domain(), reg_.object(), signalName_, shmName_,
                                               detail::type_id<Args...>(), detail::type_name<Args...>());
#ifdef _WIN32
        const std::string mtxName = (shmName_ + "_mtx").toStdString();
        mutex_ = ::CreateMutexA(NULL, FALSE, mtxName.c_str());
        if (!mutex_) {
            throw std::runtime_error("CreateMutex failed");
        }

        const std::string semName = (shmName_ + "_sem").toStdString();
        semaphore_ = ::CreateSemaphoreA(NULL, 0, 0x7fffffff, semName.c_str());
        if (!semaphore_) {
            ::CloseHandle(mutex_);
            mutex_ = NULL;
            throw std::runtime_error("CreateSemaphore failed");
        }

        const std::string evtName = (shmName_ + "_evt").toStdString();
        event_ = ::CreateEventA(NULL, FALSE, FALSE, evtName.c_str());
        if (!event_) {
            ::CloseHandle(semaphore_);
            semaphore_ = NULL;
            ::CloseHandle(mutex_);
            mutex_ = NULL;
            throw std::runtime_error("CreateEvent failed");
        }
#endif
    }

    /**
     * @brief Destroys the `RingQueue` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~RingQueue() {
#ifdef _WIN32
        if (mutex_) {
            ::CloseHandle(mutex_);
            mutex_ = NULL;
        }
        if (semaphore_) {
            ::CloseHandle(semaphore_);
            semaphore_ = NULL;
        }
        if (event_) {
            ::CloseHandle(event_);
            event_ = NULL;
        }
#endif
    }

    /**
     * @brief Returns the current shm Name.
     * @return The current shm Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& shmName() const { return shmName_; }

#ifdef _WIN32
    HANDLE wakeEvent() const { return event_; }
#endif

    /**
     * @brief Performs the `push` operation.
     * @param args Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool push(const Args&... args) {
        std::array<uint8_t, kMaxPayload> tmp;
        detail::Encoder enc(tmp.data(), tmp.size());
        if (!detail::writeAll(enc, args...)) return false;

        Layout* L = map_->layout();
        bool ok = false;

#ifdef _WIN32
        ::WaitForSingleObject(mutex_, INFINITE);
        const uint64_t inFlight = (L->seq >= L->readSeq) ? (L->seq - L->readSeq) : 0;
        if (inFlight < Capacity) {
            const uint64_t next = L->seq + 1;
            typename Layout::Slot& slot = L->entries[next % Capacity];
            slot.seq = next;
            slot.size = static_cast<uint32_t>(enc.size());
            if (slot.size <= kMaxPayload) {
                if (slot.size != 0) std::memcpy(slot.data, tmp.data(), slot.size);
                L->seq = next;
                ok = true;
            }
        }
        ::ReleaseMutex(mutex_);

        if (ok) {
            if (event_) ::SetEvent(event_);
            if (detail::win_wake_by_address_all_fn()) {
                detail::win_wake_by_address_all(&L->seq);
            } else {
                const LONG w = static_cast<LONG>(L->winWaiters);
                if (w > 0) {
                    ::ReleaseSemaphore(semaphore_, w, NULL);
                }
            }
        }
#else
        pthread_mutex_lock(&L->mtx);
        const uint64_t inFlight = (L->seq >= L->readSeq) ? (L->seq - L->readSeq) : 0;
        if (inFlight < Capacity) {
            const uint64_t next = L->seq + 1;
            typename Layout::Slot& slot = L->entries[next % Capacity];
            slot.seq = next;
            slot.size = static_cast<uint32_t>(enc.size());
            if (slot.size <= kMaxPayload) {
                if (slot.size != 0) std::memcpy(slot.data, tmp.data(), slot.size);
                L->seq = next;
                ok = true;
            }
        }
        if (ok) pthread_cond_broadcast(&L->cv);
        pthread_mutex_unlock(&L->mtx);
#endif

        if (!ok) return false;

#ifndef _WIN32
        // Event-driven wakeup for loop-based subscriptions (no polling).
        std::vector<uint32_t> pids;
        detail::SubscribersRegistryTable<>::listSubscriberPids(reg_.domain(), reg_.object(), signalName_, pids);
        if (!pids.empty()) {
            std::sort(pids.begin(), pids.end());
            pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
            for (size_t i = 0; i < pids.size(); ++i) {
                detail::LoopPoller::notifyProcess(pids[i]);
            }
        }
#endif
        return true;
    }

    /**
     * @brief Performs the `operator` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator()(const Args&... args) { return push(args...); }

    template <typename Fn>
    /**
     * @brief Performs the `connect` operation.
     * @param cb Value passed to the method.
     * @param fireInitial Value passed to the method.
     * @param timeoutMs Timeout expressed in milliseconds.
     * @return The requested connect.
     */
    Subscription connect(Fn cb, bool fireInitial = true, int timeoutMs = 0) {
        typedef typename std::decay<Fn>::type Callback;
        Callback cbCopy(std::move(cb));

        if (timeoutMs < 0) timeoutMs = 0;

#ifdef _WIN32
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (app) {
            struct State {
                std::shared_ptr<Mapping> map;
                Callback cb;
                bool fireInitial;
                bool firedInitial;
                std::atomic_bool inCallback;
                HANDLE mtx;
                HANDLE evt;

                /**
                 * @brief Constructs a `State` instance.
                 * @param m Value passed to the method.
                 * @param c Value passed to the method.
                 * @param NULL Value passed to the method.
                 *
                 * @details The instance is initialized and prepared for immediate use.
                 */
                State(const std::shared_ptr<Mapping>& m, Callback c, bool fi)
                    : map(m),
                      cb(std::move(c)),
                      fireInitial(fi),
                      firedInitial(false),
                      inCallback(false),
                      mtx(NULL),
                      evt(NULL) {}

                /**
                 * @brief Destroys the `State` instance.
                 *
                 * @details Use this hook to release any resources that remain associated with the instance.
                 */
                ~State() {
                    if (mtx) ::CloseHandle(mtx);
                    if (evt) ::CloseHandle(evt);
                }
            };

            std::shared_ptr<State> st(new State(map_, cbCopy, fireInitial));

            const std::string mtxName = (shmName_ + "_mtx").toStdString();
            st->mtx = ::CreateMutexA(NULL, FALSE, mtxName.c_str());
            if (!st->mtx) {
                throw std::runtime_error("CreateMutex (sub) failed");
            }

            const std::string evtName = (shmName_ + "_evt").toStdString();
            st->evt = ::CreateEventA(NULL, FALSE, FALSE, evtName.c_str());
            if (!st->evt) {
                throw std::runtime_error("CreateEvent (sub) failed");
            }

            const SwString domCopy = reg_.domain();
            const SwString objCopy = reg_.object();
            const SwString sigCopy = signalName_;
            const uint32_t subPid = detail::currentPid();
            const SwString subObjCopy = detail::currentSubscriberObject_();

            auto drain = [st]() {
                if (!st) return;
                if (st->inCallback.exchange(true, std::memory_order_acq_rel)) return;

                Layout* L = st->map ? st->map->layout() : nullptr;
                if (!L) {
                    st->inCallback.store(false, std::memory_order_release);
                    return;
                }

                // Quick check: no backlog and no fireInitial pending.
                if (!(st->fireInitial && !st->firedInitial)) {
                    const uint64_t seqFast = detail::atomic_load_u64(&L->seq);
                    const uint64_t readFast = detail::atomic_load_u64(&L->readSeq);
                    if (seqFast == readFast) {
                        st->inCallback.store(false, std::memory_order_release);
                        return;
                    }
                }

                struct Msg {
                    uint32_t sz;
                    std::array<uint8_t, kMaxPayload> data;
                };
                std::vector<Msg> msgs;

                if (!st->mtx) {
                    st->inCallback.store(false, std::memory_order_release);
                    return;
                }
                DWORD wr = ::WaitForSingleObject(st->mtx, 0);
                if (wr != WAIT_OBJECT_0 && wr != WAIT_ABANDONED) {
                    st->inCallback.store(false, std::memory_order_release);
                    return;
                }

                // Drain backlog (bounded by Capacity).
                while (L->readSeq < L->seq) {
                    const uint64_t next = L->readSeq + 1;
                    typename Layout::Slot& slot = L->entries[next % Capacity];
                    const uint32_t sz = slot.size;
                    if (slot.seq == next && sz <= kMaxPayload) {
                        Msg m;
                        m.sz = sz;
                        if (sz != 0) std::memcpy(m.data.data(), slot.data, sz);
                        msgs.push_back(m);
                    }
                    L->readSeq = next;
                }

                if (st->fireInitial && !st->firedInitial) st->firedInitial = true;

                ::ReleaseMutex(st->mtx);

                // Decode + invoke outside the lock.
                for (size_t i = 0; i < msgs.size(); ++i) {
                    Msg& m = msgs[i];
                    typedef std::tuple<typename std::decay<Args>::type...> Tuple;
                    Tuple out;
                    detail::Decoder dec(m.data.data(), m.sz);
                    if (detail::readTuple(dec, out)) {
                        detail::invokeWithTuple(st->cb, out);
                    }
                }

                st->inCallback.store(false, std::memory_order_release);
            };

            const auto controlPoster = [app](std::function<void()> task) {
                if (app) {
                    app->postEventOnLane(std::move(task), SwFiberLane::Control);
                    return;
                }
                task();
            };
            const SwIoDispatcher::Token watchToken =
                app->ioDispatcher().watchHandle(st->evt, controlPoster, drain);
            detail::SubscribersRegistryTable<>::registerSubscription(domCopy, objCopy, sigCopy, subPid, subObjCopy);

            if (fireInitial) {
                app->postEventPriority(drain);
            }

            if (timeoutMs > 0) {
                SwTimer::singleShot(timeoutMs, [app, watchToken, domCopy, objCopy, sigCopy, subPid, subObjCopy]() {
                    detail::SubscribersRegistryTable<>::unregisterSubscription(domCopy, objCopy, sigCopy, subPid, subObjCopy);
                    if (app) app->ioDispatcher().remove(watchToken);
                });
            }

            return Subscription(app, watchToken, domCopy, objCopy, sigCopy, subPid, subObjCopy);
        }
#endif

        struct State {
            std::shared_ptr<Mapping> map;
            Callback cb;
            bool fireInitial;
            bool firedInitial;
            std::atomic_bool inCallback;

#ifdef _WIN32
            HANDLE mtx;
#endif

            /**
             * @brief Constructs a `State` instance.
             * @param m Value passed to the method.
             * @param c Value passed to the method.
             * @param _WIN32 Value passed to the method.
             * @param NULL Value passed to the method.
             *
             * @details The instance is initialized and prepared for immediate use.
             */
            State(const std::shared_ptr<Mapping>& m, Callback c, bool fi)
                : map(m),
                  cb(std::move(c)),
                  fireInitial(fi),
                  firedInitial(false),
                  inCallback(false)
#ifdef _WIN32
                  ,
                  mtx(NULL)
#endif
            {
            }
#ifdef _WIN32
            /**
             * @brief Destroys the `State` instance.
             * @param mtx Value passed to the method.
             *
             * @details Use this hook to release any resources that remain associated with the instance.
             */
            ~State() { if (mtx) ::CloseHandle(mtx); }
#endif
        };

        std::shared_ptr<State> st(new State(map_, cbCopy, fireInitial));

#ifdef _WIN32
        const std::string mtxName = (shmName_ + "_mtx").toStdString();
        st->mtx = ::CreateMutexA(NULL, FALSE, mtxName.c_str());
        if (!st->mtx) {
            throw std::runtime_error("CreateMutex (loop sub) failed");
        }
#endif

        const SwString domCopy = reg_.domain();
        const SwString objCopy = reg_.object();
        const SwString sigCopy = signalName_;
        const uint32_t subPid = detail::currentPid();
        SwString subObjCopy = detail::currentSubscriberObject_();
        if (subObjCopy.isEmpty()) {
            static std::atomic<uint64_t> s_autoSubSeq(0ull);
            const uint64_t n = s_autoSubSeq.fetch_add(1ull, std::memory_order_relaxed) + 1ull;
            subObjCopy = SwString("__rqsub__") +
                         SwString::number(static_cast<unsigned long long>(subPid)) +
                         SwString("|") +
                         SwString::number(static_cast<unsigned long long>(n));
        }

        const size_t id = detail::LoopPoller::instance().add([st]() {
            if (!st) return;
            if (st->inCallback.exchange(true, std::memory_order_acq_rel)) return;

            Layout* L = st->map ? st->map->layout() : nullptr;
            if (!L) {
                st->inCallback.store(false, std::memory_order_release);
                return;
            }

            // Quick check: no backlog and no fireInitial pending.
            if (!(st->fireInitial && !st->firedInitial)) {
                const uint64_t seqFast = detail::atomic_load_u64(&L->seq);
                const uint64_t readFast = detail::atomic_load_u64(&L->readSeq);
                if (seqFast == readFast) {
                    st->inCallback.store(false, std::memory_order_release);
                    return;
                }
            }

            struct Msg {
                uint32_t sz;
                std::array<uint8_t, kMaxPayload> data;
            };
            std::vector<Msg> msgs;

#ifdef _WIN32
            if (!st->mtx) { st->inCallback.store(false, std::memory_order_release); return; }
            DWORD wr = ::WaitForSingleObject(st->mtx, 0);
            if (wr != WAIT_OBJECT_0 && wr != WAIT_ABANDONED) {
                st->inCallback.store(false, std::memory_order_release);
                return;
            }
#else
            if (pthread_mutex_trylock(&L->mtx) != 0) {
                st->inCallback.store(false, std::memory_order_release);
                return;
            }
#endif

            // Drain backlog (bounded by Capacity).
            while (L->readSeq < L->seq) {
                const uint64_t next = L->readSeq + 1;
                typename Layout::Slot& slot = L->entries[next % Capacity];
                const uint32_t sz = slot.size;
                if (slot.seq == next && sz <= kMaxPayload) {
                    Msg m;
                    m.sz = sz;
                    if (sz != 0) std::memcpy(m.data.data(), slot.data, sz);
                    msgs.push_back(m);
                }
                L->readSeq = next;
            }

            if (st->fireInitial && !st->firedInitial) st->firedInitial = true;

#ifdef _WIN32
            ::ReleaseMutex(st->mtx);
#else
            pthread_mutex_unlock(&L->mtx);
#endif

            // Decode + invoke outside the lock.
            for (size_t i = 0; i < msgs.size(); ++i) {
                Msg& m = msgs[i];
                typedef std::tuple<typename std::decay<Args>::type...> Tuple;
                Tuple out;
                detail::Decoder dec(m.data.data(), m.sz);
                if (detail::readTuple(dec, out)) {
                    detail::invokeWithTuple(st->cb, out);
                }
            }

            st->inCallback.store(false, std::memory_order_release);
        });

        detail::SubscribersRegistryTable<>::registerSubscription(domCopy, objCopy, sigCopy, subPid, subObjCopy);

        if (timeoutMs > 0) {
            SwTimer::singleShot(timeoutMs, [id, domCopy, objCopy, sigCopy, subPid, subObjCopy]() {
                detail::SubscribersRegistryTable<>::unregisterSubscription(domCopy, objCopy, sigCopy, subPid, subObjCopy);
                detail::LoopPoller::instance().remove(id);
            });
        }

        return Subscription(id, domCopy, objCopy, sigCopy, subPid, subObjCopy);
    }

private:
    Registry& reg_;
    SwString signalName_;
    SwString shmName_;
    std::shared_ptr<Mapping> map_;
#ifdef _WIN32
    HANDLE mutex_{NULL};
    HANDLE semaphore_{NULL};
    HANDLE event_{NULL};
#endif
};

#include "ipc/SwIpcRing.inl"

// =================================================================================================
//  SwIpcSignal — couche "signal/slot a la Qt" par-dessus l'IPC partage.
//
//  But : declarer un canal IPC aussi simplement qu'un signal Qt natif, en cachant completement
//  la SHM / le ring / le registry, avec :
//    - dimensionnement AUTOMATIQUE de la place reservee par message a partir des TYPES du signal
//      (IpcWireBound), avec override explicite maxBytes pour les types a taille variable
//      (SwString, SwByteArray, SwList, SwMap) ;
//    - choix de la semantique de livraison (DeliveryMode::Replay / LatestOnly) en un parametre ;
//    - lifetime sur (la Subscription se desabonne dans son destructeur).
//
//  Requires C++17, like SwAny and the native runtime.
// =================================================================================================

namespace size {

// --- IpcIsBounded<T> : le type a-t-il une taille serialisee MAXIMALE connue a la compilation ? ---
// Only native scalars have a fixed default wire size; records require an explicit bound.
// On specialise a false pour les types a payload variable.
template <typename T>
struct IpcIsBounded {
    // Registered record serializers may be variable-size, even for a POD record.
    static const bool value = swAnyDetail::nativeScalar<typename std::decay<T>::type>;
};

template <> struct IpcIsBounded<SwString>    { static const bool value = false; };
template <> struct IpcIsBounded<SwByteArray> { static const bool value = false; };
// Conteneurs a taille variable : specialisations partielles (forward-declaration suffit,
// la definition complete des templates n'est pas requise pour specialiser un trait).
template <typename U>             struct IpcIsBounded< ::SwList<U> >   { static const bool value = false; };
template <typename K, typename V> struct IpcIsBounded< ::SwMap<K, V> > { static const bool value = false; };

// --- IpcWireSize<T> : borne serialisee d'UN type (n'a de sens que si IpcIsBounded<T>::value). ---
// POD : sizeof(T) (writePOD ecrit l'objet brut, sans tag ni longueur).
template <typename T>
struct IpcWireSize { static const size_t value = sizeof(T); };

template <> struct IpcWireSize<SwString>    { static const size_t value = 0; };
template <> struct IpcWireSize<SwByteArray> { static const size_t value = 0; };
template <typename U>             struct IpcWireSize< ::SwList<U> >   { static const size_t value = 0; };
template <typename K, typename V> struct IpcWireSize< ::SwMap<K, V> > { static const size_t value = 0; };

// --- AllBounded<Args...> : ET logique recursif (pas de fold expression en C++11). ---
template <typename... Args> struct AllBounded;
template <> struct AllBounded<> { static const bool value = true; };
template <typename T, typename... Rest>
struct AllBounded<T, Rest...> {
    static const bool value = IpcIsBounded<T>::value && AllBounded<Rest...>::value;
};

// --- SumWireSize<Args...> : somme recursive des bornes. ---
template <typename... Args> struct SumWireSize;
template <> struct SumWireSize<> { static const size_t value = 0; };
template <typename T, typename... Rest>
struct SumWireSize<T, Rest...> {
    static const size_t value = IpcWireSize<T>::value + SumWireSize<Rest...>::value;
};

// --- IpcWireBound<Args...> : facade publique. ---
//   ::bounded -> tous les types sont-ils bornes ?
//   ::value   -> somme des bornes (valide seulement si bounded == true)
//   ::capacityBytes -> taille a reserver par message (avec un petit coussin de securite)
template <typename... Args>
struct IpcWireBound {
    static const bool   bounded = AllBounded<Args...>::value;
    static const size_t value   = SumWireSize<Args...>::value;
    static const size_t kOverhead = 16; // marge d'alignement / robustesse, pas du wire reel
    static const size_t capacityBytes = value + kOverhead;
};

} // namespace size

// Petit utilitaire : maxBytes auto (depuis les types) sinon override explicite.
template <class... Args>
struct IpcAutoMaxBytes_ {
    static uint32_t resolve(uint32_t override_) {
        if (override_ != 0u) return override_;
        // En mode auto, on ne doit arriver ici que si tous les types sont bornes
        // (garanti par le static_assert au site macro SW_IPC_SIGNAL / SW_IPC_LATCH).
        return static_cast<uint32_t>(size::IpcWireBound<Args...>::capacityBytes);
    }
};

#include "ipc/NativeRegistry.inl"
#include "ipc/SwIpcSignal.inl"

namespace detail {

inline void notifyRegistryChangedBestEffort_(const SwString& domain) {
    if (domain.isEmpty()) return;

    try {
        struct Notifier {
            Registry reg;
            SwIpcSignal<uint64_t> sig;
            /**
             * @brief Constructs a `Notifier` instance.
             *
             * @details The instance is initialized and prepared for immediate use.
             */
            explicit Notifier(const SwString& dom)
                : reg(dom, registryEventsObjectName_()),
                  sig(reg, registryEventsSignalName_(), 16u, 0u, DeliveryMode::LatestOnly) {}
        };

        static std::atomic_flag lock = ATOMIC_FLAG_INIT;
        static std::map<std::string, std::shared_ptr<Notifier>>* byDomain =
            new std::map<std::string, std::shared_ptr<Notifier>>();

        const std::string key = domain.toStdString();
        std::shared_ptr<Notifier> n;
        {
            ProcessHooksSpinGuard_ lk(lock);
            auto it = byDomain->find(key);
            if (it == byDomain->end()) {
                n = std::make_shared<Notifier>(domain);
                (*byDomain)[key] = n;
            } else {
                n = it->second;
            }
        }

        if (n) {
            n->sig.publish(nowMs());
        }
    } catch (...) {
    }
}

} // namespace detail


// =================================================================================================
//  SwIpcProperty — property distante "a la Qt" (equivalent Q_PROPERTY partage entre process).
//
//  Modele : ETAT RUNTIME VOLATILE (pas de disque) porte par un latch SHM ; ECRITURE
//  BIDIRECTIONNELLE (n'importe quel process ecrit, last-writer-wins) ; un late-joiner recoit
//  la valeur courante au connect (LatestOnly). Le payload embarque le publisherId de l'auteur
//  pour FILTRER L'ECHO (un writer ignore sa propre publication, sinon boucle infinie).
//
//  La classe est volontairement decouplee de SwRemoteObject/ThreadHandle : la macro
//  SW_IPC_PROPERTY (cote SwRemoteObject.h) fournit le publisherId, le flag alive, et une
//  NotifyFn qui se charge elle-meme du re-post sur le thread d'affinite + de l'emit local.
//
//  IMPORTANT : on ne PUBLIE JAMAIS la valeur par defaut. La SHM ne contient une valeur que
//  si quelqu'un a fait un set() explicite ; sinon chaque instance garde son default local.
//  Cela rend l'ordre de demarrage des process sans effet sur l'etat partage.
// =================================================================================================
template <class T>
class SwIpcProperty {
public:
    typedef std::function<void(const T&)> NotifyFn;

    // reg              : registry de l'objet proprietaire (isole la SHM par domain|object).
    // name             : nom logique de la property (canal SHM = hash(domain|object|name)).
    // defaultValue     : valeur locale initiale (NON publiee).
    // ownerPublisherId : id unique de l'instance proprietaire (filtrage d'echo).
    // ownerAlive       : flag de vie de l'owner (garde lifetime des callbacks).
    // notify           : appele a chaque changement effectif (set local OU reception distante) ;
    //                    typiquement "emit nameChanged(v)" avec re-post sur le thread d'affinite.
    // maxBytes         : 0 => sizing auto (POD) ; >0 requis pour un type variable (SwString...).
    SwIpcProperty(Registry& reg,
                  const SwString& name,
                  const T& defaultValue,
                  uint64_t ownerPublisherId,
                  std::shared_ptr<std::atomic_bool> ownerAlive,
                  NotifyFn notify,
                  uint32_t maxBytes = 0u)
        : name_(name),
          // Owner and proxies share a retained, bidirectional state channel.
          latch_(reg, name, /*capacity*/ 16u, maxBytes, DeliveryMode::LatestOnly),
          cached_(defaultValue),
          self_(ownerPublisherId),
          alive_(std::move(ownerAlive)),
          notify_(std::move(notify)) {
        std::shared_ptr<std::atomic_bool> alive = alive_;
        SwIpcProperty<T>* selfPtr = this;
        // Keep owner and proxy subscriptions identifiable in registry diagnostics.
        // The ring itself allocates an independent cursor for every connection.
        const SwString uniqueSub = reg.object() + SwString("#prop#") + detail::hex64(self_);
        detail::ScopedSubscriberObject subScope(uniqueSub);
        // fireInitial=true + LatestOnly : si la SHM porte deja une valeur (set d'un autre
        // process), on la recoit ici et cached_ se synchronise ; sinon cached_ reste le default.
        sub_ = latch_.connect(
            [selfPtr, alive](uint64_t pubId, T value) {
                if (!alive || !alive->load(std::memory_order_relaxed)) return;
                selfPtr->onLatch_(pubId, value);
            },
            DeliveryMode::LatestOnly, /*fireInitial=*/true, /*timeoutMs=*/0);
    }

    SwIpcProperty(const SwIpcProperty&) = delete;
    SwIpcProperty& operator=(const SwIpcProperty&) = delete;

    ~SwIpcProperty() { sub_.stop(); } // arrete le poller AVANT de detruire cache/mutex.

    // Lecture synchrone de la derniere valeur connue (cache local).
    T get() const {
        SwMutexLocker lk(mutex_);
        return cached_;
    }

    // Ecriture : met a jour le cache, publie (self_, v) sur le latch, notifie localement.
    bool set(const T& v) {
        const T next = v;
        {
            SwMutexLocker lk(mutex_);
            if (published_ && cached_ == next) return true;
        }
        uint64_t committedVersion = 0;
        // Commit in transport order, before direct mirrors run. The property
        // mutex never spans delivery: a mirror may synchronously read or set us.
        const bool accepted = latch_.publishWithCommit([this, &next, &committedVersion]() {
            SwMutexLocker lk(mutex_);
            cached_ = next;
            published_ = true;
            committedVersion = ++version_;
        }, self_, next);
        if (!committedVersion) return false;
        {
            SwMutexLocker lk(mutex_);
            if (version_ != committedVersion) return accepted;
        }
        // A nested newer update already notified observers; do not follow it
        // with an obsolete notification for this outer set().
        if (notify_) notify_(next);
        return accepted;
    }

    const SwString& name() const { return name_; }
    uint32_t maxBytes() const { return latch_.maxBytes(); }

private:
    void onLatch_(uint64_t pubId, const T& value) {
        if (!alive_ || !alive_->load(std::memory_order_relaxed)) return;
        if (pubId == self_) return; // ma propre publication -> ignorer (anti-echo)
        {
            SwMutexLocker lk(mutex_);
            if (cached_ == value) return; // deja a jour -> pas de double notify
            cached_ = value;
            ++version_;
        }
        if (notify_) notify_(value);
    }

    SwString name_;
    SwIpcSignal<uint64_t, T> latch_;
    mutable SwMutex mutex_;
    T cached_;
    bool published_{false};
    uint64_t version_{0};
    uint64_t self_;
    std::shared_ptr<std::atomic_bool> alive_;
    NotifyFn notify_;
    typename SwIpcSignal<uint64_t, T>::Subscription sub_;
};

// --- Macros de confort (requierent un membre `ipcRegistry_` dans la classe). ---

// static_assert clair : un type non borne sans maxBytes => erreur a la compilation.
// IMPORTANT (C++11) : l'assert vit au SITE de la macro SW_IPC_SIGNAL, jamais dans le corps
// partage de SwIpcSignal (sans if constexpr, il casserait SW_IPC_SIGNAL_SIZED).
#define SW_IPC_STATIC_ASSERT_BOUNDED_(name, ...)                                                   \
    static_assert(::sw::ipc::size::IpcWireBound<__VA_ARGS__>::bounded,                             \
        "SW_IPC_SIGNAL(" #name "): un des types n'a pas de taille bornee "                         \
        "(texte/octets/conteneurs/types enregistres). Utilisez "                                           \
        "SW_IPC_SIGNAL_SIZED(" #name ", maxBytes, ...) pour reserver la place.")

// Signal IPC dimensionne AUTOMATIQUEMENT depuis les types (POD uniquement).
#define SW_IPC_SIGNAL(name, ...)                                                                   \
    ::sw::ipc::SwIpcSignal<__VA_ARGS__> name{ipcRegistry_, SwString(#name), 16u, 0u,               \
        ::sw::ipc::DeliveryMode::Replay};                                                          \
    SW_IPC_STATIC_ASSERT_BOUNDED_(name, __VA_ARGS__)

// Signal IPC avec taille explicite par message (obligatoire des qu'un type est variable).
#define SW_IPC_SIGNAL_SIZED(name, maxBytes, ...)                                                   \
    ::sw::ipc::SwIpcSignal<__VA_ARGS__> name{ipcRegistry_, SwString(#name), 16u,                   \
        static_cast<uint32_t>(maxBytes), ::sw::ipc::DeliveryMode::Replay}

// Latch / etat : un seul slot, livraison LatestOnly au connect (dernière valeur connue).
#define SW_IPC_LATCH(name, ...)                                                                    \
    ::sw::ipc::SwIpcSignal<__VA_ARGS__> name{ipcRegistry_, SwString(#name), 1u, 0u,                \
        ::sw::ipc::DeliveryMode::LatestOnly};                                                      \
    SW_IPC_STATIC_ASSERT_BOUNDED_(name, __VA_ARGS__)

// Latch / etat avec taille explicite (pour un latch portant un SwString/SwByteArray).
#define SW_IPC_LATCH_SIZED(name, maxBytes, ...)                                                    \
    ::sw::ipc::SwIpcSignal<__VA_ARGS__> name{ipcRegistry_, SwString(#name), 1u,                    \
        static_cast<uint32_t>(maxBytes), ::sw::ipc::DeliveryMode::LatestOnly}

} // namespace ipc
} // namespace sw

#ifdef SW_SHARED_MEMORY_SIGNAL_ANDROID_SHM_STUBS
#  undef shm_open
#  undef shm_unlink
#  undef SW_SHARED_MEMORY_SIGNAL_ANDROID_SHM_STUBS
#endif
