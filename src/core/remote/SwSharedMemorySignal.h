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

#include "SwByteArray.h"
#include "SwJsonArray.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
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
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <cstdlib>

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
#  include <sys/un.h>
#  include <pthread.h>
#  include <signal.h>
#  include <time.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace sw {
namespace ipc {

namespace detail {

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

// Forward-declared here because it uses Signal<> (defined later in the file).
inline void notifyRegistryChangedBestEffort_(const SwString& domain);

// -------------------------------------------------------------------------
// Shared registries (best-effort) to allow IPC introspection:
// - Apps registry (global): "sw_ipc_registry"
//     - lists which "soft" (domain) is alive, with pid list + lastSeen
// - Signals registry (per-domain): "sw_ipc_registry_<domain>"
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
    static const uint32_t kVersion = 2;
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
    static const uint32_t kVersion = 2;
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
    static const uint32_t kVersion = 1;
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

    // Keep the name reasonably short for platform limits.
    const size_t kMax = 32;
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
    return SwString("sw_ipc_registry_") + suffix;
#else
    return SwString("/sw_ipc_registry_") + suffix;
#endif
}

inline SwString signalsRegistryMutexNameForDomain_(const SwString& domain) {
#ifdef _WIN32
    return SwString("sw_ipc_registry_") + sanitizeRegistrySuffix_(domain) + "_mtx";
#else
    return SwString();
#endif
}

inline SwString subscribersRegistryNameForDomain_(const SwString& domain) {
    const SwString suffix = sanitizeRegistrySuffix_(domain);
#ifdef _WIN32
    return SwString("sw_ipc_subs_") + suffix;
#else
    return SwString("/sw_ipc_subs_") + suffix;
#endif
}

inline SwString subscribersRegistryMutexNameForDomain_(const SwString& domain) {
#ifdef _WIN32
    return SwString("sw_ipc_subs_") + sanitizeRegistrySuffix_(domain) + "_mtx";
#else
    return SwString();
#endif
}

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
                o["domain"] = SwJsonValue(std::string(e.domain));
                o["domainHash"] = SwJsonValue(hex64(e.domainHash).toStdString());
                o["lastSeenMs"] = SwJsonValue(static_cast<double>(e.lastSeenMs));
                o["clientCount"] = SwJsonValue(static_cast<int>(e.pidCount));
                o["signalsRegistryName"] = SwJsonValue(std::string(e.signalsRegistryName));
                SwJsonArray pids;
                for (uint32_t k = 0; k < e.pidCount && k < 16; ++k) {
                    SwJsonObject p;
                    p["pid"] = SwJsonValue(static_cast<int>(e.pids[k]));
                    p["lastSeenMs"] = SwJsonValue(static_cast<double>(e.pidLastSeenMs[k]));
                    pids.append(SwJsonValue(p));
                }
                o["pids"] = SwJsonValue(pids);
                arr.append(SwJsonValue(std::make_shared<SwJsonObject>(o)));
            }
            unlock_(map);
        } catch (...) {
        }
        return arr;
    }

private:
    static SwString appsRegistryName_() {
#ifdef _WIN32
        return SwString("sw_ipc_registry");
#else
        return SwString("/sw_ipc_registry");
#endif
    }

    static SwString appsRegistryMutexName_() {
#ifdef _WIN32
        return SwString("sw_ipc_registry_mtx");
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
        static std::mutex* cachedMtx = new std::mutex();
        static std::shared_ptr<Mapping>* cached = new std::shared_ptr<Mapping>();

        {
            std::lock_guard<std::mutex> lk(*cachedMtx);
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
            std::lock_guard<std::mutex> lk(*cachedMtx);
            *cached = map;
        }
        return map;
#else
        const std::string nameA = appsRegistryName_().toStdString();
        int fd = ::shm_open(nameA.c_str(), O_RDWR | O_CREAT, 0666);
        if (fd < 0) return std::shared_ptr<Mapping>();
        ensureSharedMemoryPermissions_(fd);
        if (::ftruncate(fd, sizeof(Layout)) != 0) {
            ::close(fd);
            return std::shared_ptr<Mapping>();
        }
        void* mem = ::mmap(NULL, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (mem == MAP_FAILED) return std::shared_ptr<Mapping>();
        Layout* L = static_cast<Layout*>(mem);

        if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->count = 0;
            pthread_mutexattr_t ma;
            pthread_mutexattr_init(&ma);
            pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&L->mtx, &ma);
            pthread_mutexattr_destroy(&ma);
        }
        if (L->reserved != Layout::kTimeBaseTag) {
            L->count = 0;
            std::memset(L->apps, 0, sizeof(L->apps));
            L->reserved = Layout::kTimeBaseTag;
        }

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        {
            std::lock_guard<std::mutex> lk(*cachedMtx);
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
        pthread_mutex_lock(&map->layout()->mtx);
#endif
    }

    static bool tryLock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return false;
#ifdef _WIN32
        if (!map->hMtx_) return false;
        const DWORD wr = ::WaitForSingleObject(map->hMtx_, 0);
        return (wr == WAIT_OBJECT_0 || wr == WAIT_ABANDONED);
#else
        return pthread_mutex_trylock(&map->layout()->mtx) == 0;
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
            std::lock_guard<std::mutex> lk(mtx_());
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
            std::lock_guard<std::mutex> lk(mtx_());
            if (heartbeats_().count(key)) return;

            const SwString domCopy = domain;
            SwEventLoop::RuntimeHandle h = SwEventLoop::installSlowRuntime(1000, [domCopy]() {
                AppsRegistryTable::touchDomainBestEffort_(domCopy);
            });
            heartbeats_()[key] = h;
        }

    private:
        static std::mutex& mtx_() {
            static std::mutex* m = new std::mutex();
            return *m;
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
                std::lock_guard<std::mutex> lk(mtx_());
                for (const auto& d : domains_()) doms.push_back(d);
            }

            for (size_t i = 0; i < doms.size(); ++i) {
                if (bestEffort) unregisterCurrentPidBestEffortStd_(doms[i]);
                else unregisterCurrentPid(SwString(doms[i]));
            }

            // stop heartbeats (best-effort)
            {
                std::lock_guard<std::mutex> lk(mtx_());
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
            bool changed = false;

            for (uint32_t i = 0; i < L->count && i < MaxEntries; ++i) {
                RegistryEntry& e = L->entries[i];
                if (e.hash == h) {
                    e.typeId = typeId;
                    // Don't steal ownership when another process merely opens the signal.
                    // Ownership is only refreshed by the owning PID's heartbeat, or taken over if stale/dead.
                    if (e.pid == 0 || e.pid == pid ||
                        pidStateBestEffort_(e.pid) == PidState::Dead ||
                        (e.lastSeenMs != 0 && t >= e.lastSeenMs && (t - e.lastSeenMs) > kEntryTtlMs)) {
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

            lock_(map);
            Layout* L = map->layout();
            cleanupStale_locked_(L, nowMs());
            const uint32_t n = (L->count < MaxEntries) ? L->count : static_cast<uint32_t>(MaxEntries);
            for (uint32_t i = 0; i < n; ++i) {
                const RegistryEntry& e = L->entries[i];
                SwJsonObject o;
                o["hash"] = SwJsonValue(hex64(e.hash).toStdString());
                o["typeId"] = SwJsonValue(hex64(e.typeId).toStdString());
                o["pid"] = SwJsonValue(static_cast<int>(e.pid));
                o["lastSeenMs"] = SwJsonValue(static_cast<double>(e.lastSeenMs));
                o["shmName"] = SwJsonValue(std::string(e.shmName));
                o["domain"] = SwJsonValue(std::string(e.domain));
                o["object"] = SwJsonValue(std::string(e.object));
                o["signal"] = SwJsonValue(std::string(e.signal));
                o["typeName"] = SwJsonValue(std::string(e.typeName));
                arr.append(SwJsonValue(std::make_shared<SwJsonObject>(o)));
            }
            unlock_(map);
        } catch (...) {
        }
        return arr;
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

    static std::shared_ptr<Mapping> openOrCreate_(const SwString& domain) {
        static std::mutex gMtx;
        static std::map<std::string, std::shared_ptr<Mapping>> cache;

        const std::string key = domain.toStdString();
        {
            std::lock_guard<std::mutex> lk(gMtx);
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
            std::lock_guard<std::mutex> lk(gMtx);
            cache[key] = map;
        }
        return map;
#else
        const std::string nameA = registryName_(domain).toStdString();
        int fd = ::shm_open(nameA.c_str(), O_RDWR | O_CREAT, 0666);
        if (fd < 0) return std::shared_ptr<Mapping>();
        ensureSharedMemoryPermissions_(fd);
        if (::ftruncate(fd, sizeof(Layout)) != 0) {
            ::close(fd);
            return std::shared_ptr<Mapping>();
        }
        void* mem = ::mmap(NULL, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (mem == MAP_FAILED) return std::shared_ptr<Mapping>();
        Layout* L = static_cast<Layout*>(mem);

        if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->count = 0;
            pthread_mutexattr_t ma;
            pthread_mutexattr_init(&ma);
            pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&L->mtx, &ma);
            pthread_mutexattr_destroy(&ma);
        }
        if (L->reserved != Layout::kTimeBaseTag) {
            L->count = 0;
            std::memset(L->entries, 0, sizeof(L->entries));
            L->reserved = Layout::kTimeBaseTag;
        }

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        {
            std::lock_guard<std::mutex> lk(gMtx);
            cache[key] = map;
        }
        return map;
#endif
    }

    static void lock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return;
#ifdef _WIN32
        ::WaitForSingleObject(map->hMtx_, INFINITE);
#else
        pthread_mutex_lock(&map->layout()->mtx);
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
        const bool allowTtl = (L->reserved == Layout::kTimeBaseTag);

        uint32_t i = 0;
        while (i < L->count && i < MaxEntries) {
            RegistryEntry& e = L->entries[i];
            const uint32_t pid = e.pid;
            const uint64_t ls = e.lastSeenMs;

            bool stale = false;
            if (pid == 0) stale = true;
            else if (ls == 0) stale = true;
            else if (allowTtl && nowMs >= ls && (nowMs - ls) > kEntryTtlMs) stale = true;
            else if (pidStateBestEffort_(pid) == PidState::Dead) stale = true;

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
        return pthread_mutex_trylock(&map->layout()->mtx) == 0;
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
            std::lock_guard<std::mutex> lk(mtx_());
            if (heartbeats_().count(key)) return;

            const SwString domCopy = domain;
            SwEventLoop::RuntimeHandle h = SwEventLoop::installSlowRuntime(1000, [domCopy]() {
                RegistryTable::touchCurrentPidBestEffort_(domCopy);
            });
            heartbeats_()[key] = h;
        }

    private:
        static std::mutex& mtx_() {
            static std::mutex* m = new std::mutex();
            return *m;
        }

        static std::map<std::string, SwEventLoop::RuntimeHandle>& heartbeats_() {
            static std::map<std::string, SwEventLoop::RuntimeHandle>* m =
                new std::map<std::string, SwEventLoop::RuntimeHandle>();
            return *m;
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

            lock_(map);
            Layout* L = map->layout();
            cleanupStale_locked_(L, nowMs());
            const uint32_t n = (L->count < MaxEntries) ? L->count : static_cast<uint32_t>(MaxEntries);
            for (uint32_t i = 0; i < n; ++i) {
                const SubscriberEntry& e = L->entries[i];
                SwJsonObject o;
                o["hash"] = SwJsonValue(hex64(e.hash).toStdString());
                o["subPid"] = SwJsonValue(static_cast<int>(e.subPid));
                o["refCount"] = SwJsonValue(static_cast<int>(e.refCount));
                o["lastSeenMs"] = SwJsonValue(static_cast<double>(e.lastSeenMs));
                o["domain"] = SwJsonValue(std::string(e.domain));
                o["object"] = SwJsonValue(std::string(e.object));
                o["signal"] = SwJsonValue(std::string(e.signal));
                {
                    const SwString subObj = unpackCstrAfterNul_(e.signal, sizeof(e.signal));
                    if (!subObj.isEmpty()) {
                        const std::string dom = std::string(e.domain);
                        o["subObject"] = SwJsonValue(subObj.toStdString());
                        o["subTarget"] = SwJsonValue((dom.empty() ? domain.toStdString() : dom) + "/" + subObj.toStdString());
                    }
                }
                arr.append(SwJsonValue(std::make_shared<SwJsonObject>(o)));
            }
            unlock_(map);
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
        static std::mutex gMtx;
        static std::map<std::string, std::shared_ptr<Mapping>> cache;

        const std::string key = domain.toStdString();
        {
            std::lock_guard<std::mutex> lk(gMtx);
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
            std::lock_guard<std::mutex> lk(gMtx);
            cache[key] = map;
        }
        return map;
#else
        const std::string nameA = registryName_(domain).toStdString();
        int fd = ::shm_open(nameA.c_str(), O_RDWR | O_CREAT, 0666);
        if (fd < 0) return std::shared_ptr<Mapping>();
        ensureSharedMemoryPermissions_(fd);
        if (::ftruncate(fd, sizeof(Layout)) != 0) {
            ::close(fd);
            return std::shared_ptr<Mapping>();
        }
        void* mem = ::mmap(NULL, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (mem == MAP_FAILED) return std::shared_ptr<Mapping>();
        Layout* L = static_cast<Layout*>(mem);

        if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->count = 0;
            pthread_mutexattr_t ma;
            pthread_mutexattr_init(&ma);
            pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&L->mtx, &ma);
            pthread_mutexattr_destroy(&ma);
        }
        if (L->reserved != Layout::kTimeBaseTag) {
            L->count = 0;
            std::memset(L->entries, 0, sizeof(L->entries));
            L->reserved = Layout::kTimeBaseTag;
        }

        std::shared_ptr<Mapping> map(new Mapping(L));
        map->mem_ = mem;
        {
            std::lock_guard<std::mutex> lk(gMtx);
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
        pthread_mutex_lock(&map->layout()->mtx);
#endif
    }

    static bool tryLock_(const std::shared_ptr<Mapping>& map) {
        if (!map) return false;
#ifdef _WIN32
        if (!map->hMtx_) return false;
        const DWORD wr = ::WaitForSingleObject(map->hMtx_, 0);
        return (wr == WAIT_OBJECT_0 || wr == WAIT_ABANDONED);
#else
        return pthread_mutex_trylock(&map->layout()->mtx) == 0;
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
        const bool allowTtl = (L->reserved == Layout::kTimeBaseTag);

        uint32_t i = 0;
        while (i < L->count && i < MaxEntries) {
            SubscriberEntry& e = L->entries[i];
            const uint32_t pid = e.subPid;
            const uint64_t ls = e.lastSeenMs;

            bool stale = false;
            if (pid == 0) stale = true;
            else if (ls == 0) stale = true;
            else if (e.refCount == 0) stale = true;
            else if (allowTtl && nowMs >= ls && (nowMs - ls) > kEntryTtlMs) stale = true;
            else if (pidStateBestEffort_(pid) == PidState::Dead) stale = true;

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
            std::lock_guard<std::mutex> lk(mtx_());
            if (heartbeats_().count(key)) return;

            const SwString domCopy = domain;
            SwEventLoop::RuntimeHandle h = SwEventLoop::installSlowRuntime(1000, [domCopy]() {
                SubscribersRegistryTable::touchCurrentPidBestEffort_(domCopy);
            });
            heartbeats_()[key] = h;
        }

    private:
        static std::mutex& mtx_() {
            static std::mutex* m = new std::mutex();
            return *m;
        }

        static std::map<std::string, SwEventLoop::RuntimeHandle>& heartbeats_() {
            static std::map<std::string, SwEventLoop::RuntimeHandle>* m =
                new std::map<std::string, SwEventLoop::RuntimeHandle>();
            return *m;
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
#if defined(__clang__) || defined(__GNUC__)
    return SwString(__PRETTY_FUNCTION__);
#elif defined(_MSC_VER)
    return SwString(__FUNCSIG__);
#else
    return SwString("sw::ipc::detail::type_name<unknown>");
#endif
}

struct Encoder {
    uint8_t* p;
    size_t cap;
    size_t pos;

    /**
     * @brief Constructs a `Encoder` instance.
     * @param data Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    Encoder(uint8_t* data, size_t capacity) : p(data), cap(capacity), pos(0) {}

    /**
     * @brief Performs the `writeBytes` operation on the associated resource.
     * @param data Value passed to the method.
     * @param len Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool writeBytes(const void* data, size_t len) {
        if (pos + len > cap) return false;
        std::memcpy(p + pos, data, len);
        pos += len;
        return true;
    }

    template <class T>
    /**
     * @brief Performs the `writePOD` operation on the associated resource.
     * @param v Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool writePOD(const T& v) {
        if (pos + sizeof(T) > cap) return false;
        std::memcpy(p + pos, &v, sizeof(T));
        pos += sizeof(T);
        return true;
    }

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t size() const { return pos; }
};

struct Decoder {
    const uint8_t* p;
    size_t cap;
    size_t pos;

    /**
     * @brief Constructs a `Decoder` instance.
     * @param data Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    Decoder(const uint8_t* data, size_t capacity) : p(data), cap(capacity), pos(0) {}

    /**
     * @brief Performs the `readBytes` operation on the associated resource.
     * @param out Value passed to the method.
     * @param len Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool readBytes(void* out, size_t len) {
        if (pos + len > cap) return false;
        std::memcpy(out, p + pos, len);
        pos += len;
        return true;
    }

    template <class T>
    /**
     * @brief Performs the `readPOD` operation on the associated resource.
     * @param out Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool readPOD(T& out) {
        if (pos + sizeof(T) > cap) return false;
        std::memcpy(&out, p + pos, sizeof(T));
        pos += sizeof(T);
        return true;
    }
};

template <typename T, typename Enable = void>
struct Codec {
    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param enc Value passed to the method.
     * @param v Value passed to the method.
     * @return The requested write.
     */
    static bool write(Encoder& enc, const T& v) {
        return enc.writePOD(v);
    }
    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param dec Value passed to the method.
     * @param out Value passed to the method.
     * @return The resulting read.
     */
    static bool read(Decoder& dec, T& out) {
        return dec.readPOD(out);
    }
};

template <typename T>
struct Codec<T, typename std::enable_if<std::is_same<T, SwString>::value>::type> {
    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param enc Value passed to the method.
     * @param v Value passed to the method.
     * @return The requested write.
     */
    static bool write(Encoder& enc, const SwString& v) {
        std::string s = v.toStdString();
        const uint32_t len = static_cast<uint32_t>(s.size());
        if (!enc.writePOD(len)) return false;
        return enc.writeBytes(s.data(), len);
    }
    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param dec Value passed to the method.
     * @param out Value passed to the method.
     * @return The resulting read.
     */
    static bool read(Decoder& dec, SwString& out) {
        uint32_t len = 0;
        if (!dec.readPOD(len)) return false;
        std::string s;
        s.resize(len);
        if (len != 0 && !dec.readBytes(&s[0], len)) return false;
        out = SwString(s);
        return true;
    }
};

template <typename T>
struct Codec<T, typename std::enable_if<std::is_same<T, SwByteArray>::value>::type> {
    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param enc Value passed to the method.
     * @param v Value passed to the method.
     * @return The requested write.
     */
    static bool write(Encoder& enc, const SwByteArray& v) {
        const uint32_t len = static_cast<uint32_t>(v.size());
        if (!enc.writePOD(len)) return false;
        if (len == 0) return true;
        return enc.writeBytes(v.constData(), len);
    }
    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param dec Value passed to the method.
     * @param out Value passed to the method.
     * @return The resulting read.
     */
    static bool read(Decoder& dec, SwByteArray& out) {
        uint32_t len = 0;
        if (!dec.readPOD(len)) return false;
        if (len == 0) {
            out.clear();
            return true;
        }
        SwByteArray tmp(static_cast<size_t>(len), '\0');
        if (!dec.readBytes(tmp.data(), len)) return false;
        out = std::move(tmp);
        return true;
    }
};

inline bool writeAll(Encoder&) { return true; }
template <typename T, typename... Rest>
inline bool writeAll(Encoder& enc, const T& v, const Rest&... rest) {
    return Codec<T>::write(enc, v) && writeAll(enc, rest...);
}

inline bool readAll(Decoder&) { return true; }
template <typename T, typename... Rest>
inline bool readAll(Decoder& dec, T& out, Rest&... rest) {
    return Codec<T>::read(dec, out) && readAll(dec, rest...);
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
        static int s_fd = -1;
        if (s_fd < 0) {
            s_fd = ::shm_open(shmName.c_str(), O_RDWR | O_CREAT, 0666);
        }
        if (s_fd < 0) return nullptr;
        ensureSharedMemoryPermissions_(s_fd);
        if (::ftruncate(s_fd, static_cast<off_t>(sizeof(Table))) != 0) return nullptr;
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
            std::lock_guard<std::mutex> lk(mtx_);
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
            std::lock_guard<std::mutex> lk(mtx_);
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
        static LoopPoller p;
        return p;
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
            static IpcWakeup n;
            return n;
        }

        /**
         * @brief Performs the `ensureAttached` operation.
         */
        void ensureAttached() {
            if (attached_.load(std::memory_order_acquire)) return;
            std::lock_guard<std::mutex> lk(mtx_);
            if (attached_.load(std::memory_order_relaxed)) return;
            attachLocked_();
        }

        /**
         * @brief Performs the `detach` operation.
         */
        void detach() {
            if (!attached_.load(std::memory_order_acquire)) return;
            std::lock_guard<std::mutex> lk(mtx_);
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
            static int s = -1;
            static std::mutex sm;
            std::lock_guard<std::mutex> lk(sm);
            if (s >= 0) return s;
            s = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            return s;
        }

        static void fillAddr_(uint32_t pid, sockaddr_un& out, socklen_t& lenOut) {
            std::memset(&out, 0, sizeof(out));
            out.sun_family = AF_UNIX;
            const std::string name = std::string("sw_ipc_notify_") + std::to_string(pid);
            const size_t maxLen = sizeof(out.sun_path) - 2;
            const size_t n = (name.size() > maxLen) ? maxLen : name.size();
            out.sun_path[0] = '\0'; // abstract namespace
            std::memcpy(out.sun_path + 1, name.data(), n);
            lenOut = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + n);
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

        std::mutex mtx_;
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
            std::lock_guard<std::mutex> lk(mtx_);
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

    std::mutex mtx_;
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
    if (!Codec<T>::read(dec, elem)) return false;
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
// Segment name: "sw_ipc_registry_<domain>".
inline SwJsonArray shmRegistrySnapshot(const SwString& domain) {
    return detail::RegistryTable<>::snapshot(domain);
}

// Best-effort snapshot of a domain subscribers registry (who is connected to what).
// Segment name: "sw_ipc_subs_<domain>".
inline SwJsonArray shmSubscribersSnapshot(const SwString& domain) {
    return detail::SubscribersRegistryTable<>::snapshot(domain);
}

template <size_t MaxPayload>
struct ShmLayout {
    uint32_t magic;
    uint32_t version;
    uint64_t typeId;

#ifndef _WIN32
    pthread_mutex_t mtx;
    pthread_cond_t cv;
#endif

    uint64_t seq;
    uint32_t size;
    uint8_t data[MaxPayload];

    // Windows fallback when WaitOnAddress is not available:
    // number of subscribers currently waiting on the semaphore.
    uint32_t winWaiters;

    uint8_t reserved[64];

    static const uint32_t kMagic = 0x53494731u; // 'SIG1'
    static const uint32_t kVersion = 2;
};

template <size_t MaxPayload>
class ShmMapping {
public:
    typedef ShmLayout<MaxPayload> Layout;

    /**
     * @brief Opens the or Create handled by the object.
     * @param shmName Value passed to the method.
     * @param expectedTypeId Value passed to the method.
     * @return The requested or Create.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    static std::shared_ptr<ShmMapping> openOrCreate(const SwString& shmName, uint64_t expectedTypeId) {
        bool created = false;

#ifdef _WIN32
        const std::string nameA = shmName.toStdString();
        HANDLE hMap = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                           static_cast<DWORD>(sizeof(Layout)), nameA.c_str());
        const DWORD lastErr = ::GetLastError();
        if (!hMap) {
            throw std::runtime_error("CreateFileMapping failed");
        }
        created = (lastErr != ERROR_ALREADY_EXISTS);

        void* mem = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Layout));
        if (!mem) {
            ::CloseHandle(hMap);
            throw std::runtime_error("MapViewOfFile failed");
        }

        std::shared_ptr<ShmMapping> mapping(new ShmMapping(shmName, mem, hMap));
#else
        const std::string nameA = shmName.toStdString();
        int fd = ::shm_open(nameA.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            created = true;
            detail::ensureSharedMemoryPermissions_(fd);
            if (::ftruncate(fd, sizeof(Layout)) != 0) {
                ::close(fd);
                ::shm_unlink(nameA.c_str());
                throw std::runtime_error("ftruncate(shm) failed");
            }
        } else if (errno == EEXIST) {
            fd = ::shm_open(nameA.c_str(), O_RDWR, 0666);
            if (fd < 0) throw std::runtime_error("shm_open(existing) failed");
            detail::ensureSharedMemoryPermissions_(fd);
        } else {
            throw std::runtime_error("shm_open failed");
        }

        void* mem = ::mmap(NULL, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (mem == MAP_FAILED) throw std::runtime_error("mmap failed");

        std::shared_ptr<ShmMapping> mapping(new ShmMapping(shmName, mem));
#endif

        Layout* L = mapping->layout();

#ifdef _WIN32
        // Serialize initialization/validation to avoid races between concurrent creators/openers.
        const std::string initName = shmName.toStdString() + "_init";
        HANDLE initMtx = ::CreateMutexA(NULL, FALSE, initName.c_str());
        if (!initMtx) {
            throw std::runtime_error("CreateMutex(init) failed");
        }
        ::WaitForSingleObject(initMtx, INFINITE);
#endif

        if (created || (L->magic == 0 && L->version == 0)) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->typeId = expectedTypeId;

#ifndef _WIN32
            pthread_mutexattr_t ma;
            pthread_condattr_t ca;
            pthread_mutexattr_init(&ma);
            pthread_condattr_init(&ca);
            pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
            pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&L->mtx, &ma);
            pthread_cond_init(&L->cv, &ca);
            pthread_mutexattr_destroy(&ma);
            pthread_condattr_destroy(&ca);
#endif

            L->seq = 1;
            L->size = 0;
        } else {
            if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
#ifdef _WIN32
                ::ReleaseMutex(initMtx);
                ::CloseHandle(initMtx);
#endif
                throw std::runtime_error("SHM layout mismatch (magic/version)");
            }
            if (L->typeId != expectedTypeId) {
#ifdef _WIN32
                ::ReleaseMutex(initMtx);
                ::CloseHandle(initMtx);
#endif
                throw std::runtime_error("SHM type mismatch (subscriber/publisher types differ)");
            }
        }

#ifdef _WIN32
        ::ReleaseMutex(initMtx);
        ::CloseHandle(initMtx);
#endif

        return mapping;
    }

#ifndef _WIN32
    /**
     * @brief Destroys the specified destroy.
     * @param shmName Value passed to the method.
     * @return The requested destroy.
     */
    static void destroy(const SwString& shmName) {
        ::shm_unlink(shmName.toStdString().c_str());
    }
#endif

    /**
     * @brief Destroys the `ShmMapping` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~ShmMapping() {
#ifdef _WIN32
        if (mem_) {
            ::UnmapViewOfFile(mem_);
        }
        if (hMap_) {
            ::CloseHandle(hMap_);
        }
#else
        if (mem_ && mem_ != MAP_FAILED) {
            ::munmap(mem_, sizeof(Layout));
        }
#endif
    }

    /**
     * @brief Performs the `layout` operation.
     * @param mem_ Value passed to the method.
     * @return The requested layout.
     */
    Layout* layout() const { return static_cast<Layout*>(mem_); }
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& name() const { return name_; }

private:
#ifdef _WIN32
    ShmMapping(const SwString& name, void* mem, HANDLE hMap)
        : name_(name), mem_(mem), hMap_(hMap) {}
#else
    ShmMapping(const SwString& name, void* mem)
        : name_(name), mem_(mem) {}
#endif

    SwString name_;
    void* mem_{nullptr};
#ifdef _WIN32
    HANDLE hMap_{NULL};
#endif
};

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

template <typename Layout>
class ShmMappingT {
public:
    /**
     * @brief Opens the or Create handled by the object.
     * @param shmName Value passed to the method.
     * @param expectedTypeId Value passed to the method.
     * @return The requested or Create.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    static std::shared_ptr<ShmMappingT> openOrCreate(const SwString& shmName, uint64_t expectedTypeId) {
        bool created = false;

#ifdef _WIN32
        const std::string nameA = shmName.toStdString();
        HANDLE hMap = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                           static_cast<DWORD>(sizeof(Layout)), nameA.c_str());
        const DWORD lastErr = ::GetLastError();
        if (!hMap) {
            throw std::runtime_error("CreateFileMapping failed");
        }
        created = (lastErr != ERROR_ALREADY_EXISTS);

        void* mem = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Layout));
        if (!mem) {
            ::CloseHandle(hMap);
            throw std::runtime_error("MapViewOfFile failed");
        }

        std::shared_ptr<ShmMappingT> mapping(new ShmMappingT(shmName, mem, hMap));
#else
        const std::string nameA = shmName.toStdString();
        int fd = ::shm_open(nameA.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            created = true;
            detail::ensureSharedMemoryPermissions_(fd);
            if (::ftruncate(fd, sizeof(Layout)) != 0) {
                ::close(fd);
                ::shm_unlink(nameA.c_str());
                throw std::runtime_error("ftruncate(shm) failed");
            }
        } else if (errno == EEXIST) {
            fd = ::shm_open(nameA.c_str(), O_RDWR, 0666);
            if (fd < 0) throw std::runtime_error("shm_open(existing) failed");
            detail::ensureSharedMemoryPermissions_(fd);
        } else {
            throw std::runtime_error("shm_open failed");
        }

        void* mem = ::mmap(NULL, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (mem == MAP_FAILED) throw std::runtime_error("mmap failed");

        std::shared_ptr<ShmMappingT> mapping(new ShmMappingT(shmName, mem));
#endif

        Layout* L = mapping->layout();

#ifdef _WIN32
        // Serialize initialization/validation to avoid races between concurrent creators/openers.
        const std::string initName = shmName.toStdString() + "_init";
        HANDLE initMtx = ::CreateMutexA(NULL, FALSE, initName.c_str());
        if (!initMtx) {
            throw std::runtime_error("CreateMutex(init) failed");
        }
        ::WaitForSingleObject(initMtx, INFINITE);
#endif

        if (created || (L->magic == 0 && L->version == 0)) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->typeId = expectedTypeId;

#ifndef _WIN32
            pthread_mutexattr_t ma;
            pthread_condattr_t ca;
            pthread_mutexattr_init(&ma);
            pthread_condattr_init(&ca);
            pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
            pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&L->mtx, &ma);
            pthread_cond_init(&L->cv, &ca);
            pthread_mutexattr_destroy(&ma);
            pthread_condattr_destroy(&ca);
#endif

            Layout::initLayout(L);
        } else {
            if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
#ifdef _WIN32
                ::ReleaseMutex(initMtx);
                ::CloseHandle(initMtx);
#endif
                throw std::runtime_error("SHM layout mismatch (magic/version)");
            }
            if (L->typeId != expectedTypeId) {
#ifdef _WIN32
                ::ReleaseMutex(initMtx);
                ::CloseHandle(initMtx);
#endif
                throw std::runtime_error("SHM type mismatch (subscriber/publisher types differ)");
            }
        }

#ifdef _WIN32
        ::ReleaseMutex(initMtx);
        ::CloseHandle(initMtx);
#endif

        return mapping;
    }

#ifndef _WIN32
    /**
     * @brief Destroys the specified destroy.
     * @param shmName Value passed to the method.
     * @return The requested destroy.
     */
    static void destroy(const SwString& shmName) {
        ::shm_unlink(shmName.toStdString().c_str());
    }
#endif

    /**
     * @brief Destroys the `ShmMappingT` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~ShmMappingT() {
#ifdef _WIN32
        if (mem_) {
            ::UnmapViewOfFile(mem_);
        }
        if (hMap_) {
            ::CloseHandle(hMap_);
        }
#else
        if (mem_ && mem_ != MAP_FAILED) {
            ::munmap(mem_, sizeof(Layout));
        }
#endif
    }

    /**
     * @brief Performs the `layout` operation.
     * @param mem_ Value passed to the method.
     * @return The requested layout.
     */
    Layout* layout() const { return static_cast<Layout*>(mem_); }
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& name() const { return name_; }

private:
#ifdef _WIN32
    ShmMappingT(const SwString& name, void* mem, HANDLE hMap)
        : name_(name), mem_(mem), hMap_(hMap) {}
#else
    ShmMappingT(const SwString& name, void* mem)
        : name_(name), mem_(mem) {}
#endif

    SwString name_;
    void* mem_{nullptr};
#ifdef _WIN32
    HANDLE hMap_{NULL};
#endif
};

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

template <class... Args>
class RingQueueDynamic {
public:
    static const uint32_t kDefaultMaxPayload = 4096u;
    static const uint32_t kMaxSubscriberCursors = 64u;
private:
    class DynamicMapping;
public:

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
         * @param subscriberObject Value passed to the method.
         * @param map Value passed to the method.
         * @param true Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Subscription(size_t pollerId,
                     const SwString& domain,
                     const SwString& object,
                     const SwString& signal,
                     uint32_t subPid,
                     const SwString& subscriberObject,
                     const std::shared_ptr<DynamicMapping>& map,
                     const SwString& shmName)
            : pollerId_(pollerId),
              domain_(domain),
              object_(object),
              signal_(signal),
              subPid_(subPid),
              subscriberObject_(subscriberObject),
              map_(map),
              shmName_(shmName),
              registered_(true) {}

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
            domain_ = std::move(o.domain_);
            object_ = std::move(o.object_);
            signal_ = std::move(o.signal_);
            subPid_ = o.subPid_;
            o.subPid_ = 0;
            subscriberObject_ = std::move(o.subscriberObject_);
            map_ = std::move(o.map_);
            shmName_ = std::move(o.shmName_);
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
            if (!pollerId_ && !registered_) return;

            if (registered_) {
                detail::SubscribersRegistryTable<>::unregisterSubscription(domain_, object_, signal_, subPid_, subscriberObject_);
                registered_ = false;
            }

            if (map_) {
                clearSubscriberCursor_(map_, shmName_, subPid_, subscriberObject_);
            }

            detail::LoopPoller::instance().remove(pollerId_);
            pollerId_ = 0;
        }

    private:
        size_t pollerId_{0};
        SwString domain_;
        SwString object_;
        SwString signal_;
        uint32_t subPid_{0};
        SwString subscriberObject_;
        std::shared_ptr<DynamicMapping> map_;
        SwString shmName_;
        bool registered_{false};
    };

    /**
     * @brief Constructs a `RingQueueDynamic` instance.
     * @param reg Value passed to the method.
     * @param signalName Value passed to the method.
     * @param capacity Value passed to the method.
     * @param maxPayload Value passed to the method.
     * @param signalName Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    RingQueueDynamic(Registry& reg,
                     const SwString& signalName,
                     uint32_t capacity,
                     uint32_t maxPayload = kDefaultMaxPayload)
        : reg_(reg), signalName_(signalName) {
        if (capacity == 0u) throw std::runtime_error("RingQueueDynamic: capacity must be > 0");
        if (maxPayload == 0u) throw std::runtime_error("RingQueueDynamic: maxPayload must be > 0");

        shmName_ = detail::make_shm_name(reg_.domain(), reg_.object(), signalName_);
        map_ = openOrCreateMapping_(shmName_, detail::type_id<Args...>(), capacity, maxPayload);
        detail::RegistryTable<>::registerSignal(reg_.domain(), reg_.object(), signalName_, shmName_,
                                               detail::type_id<Args...>(), detail::type_name<Args...>());

#ifdef _WIN32
        const std::string mtxName = (shmName_ + "_mtx").toStdString();
        mutex_ = ::CreateMutexA(NULL, FALSE, mtxName.c_str());
        if (!mutex_) {
            throw std::runtime_error("CreateMutex failed");
        }
#endif
    }

    /**
     * @brief Destroys the `RingQueueDynamic` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~RingQueueDynamic() {
#ifdef _WIN32
        if (mutex_) {
            ::CloseHandle(mutex_);
            mutex_ = NULL;
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
    /**
     * @brief Performs the `capacity` operation.
     * @return The requested capacity.
     */
    uint32_t capacity() const { return map_ ? map_->header()->capacity : 0u; }
    /**
     * @brief Performs the `maxPayload` operation.
     * @return The requested max Payload.
     */
    uint32_t maxPayload() const { return map_ ? map_->header()->maxPayload : 0u; }

    /**
     * @brief Performs the `push` operation.
     * @param args Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool push(const Args&... args) {
        Header* L = map_ ? map_->header() : nullptr;
        if (!L) return false;

        const uint32_t cap = L->capacity;
        const uint32_t maxPayloadBytes = L->maxPayload;
        if (cap == 0u || maxPayloadBytes == 0u) return false;

        std::vector<uint8_t> tmp(static_cast<size_t>(maxPayloadBytes), 0);
        detail::Encoder enc(tmp.data(), tmp.size());
        if (!detail::writeAll(enc, args...)) return false;

        bool ok = false;

#ifdef _WIN32
        ::WaitForSingleObject(mutex_, INFINITE);
        const uint64_t minRead = minReadSeqLocked_(L);
        L->readSeq = minRead;
        const uint64_t inFlight = (L->seq >= minRead) ? (L->seq - minRead) : 0ull;
        if (inFlight < static_cast<uint64_t>(cap)) {
            const uint64_t next = L->seq + 1;
            DynamicSlot* slot = map_->slotAt(static_cast<size_t>(next % cap));
            slot->seq = next;
            slot->size = static_cast<uint32_t>(enc.size());
            slot->reserved = 0;
            if (slot->size <= maxPayloadBytes) {
                if (slot->size != 0) std::memcpy(map_->slotData(slot), tmp.data(), slot->size);
                L->seq = next;
                ok = true;
            }
        }
        ::ReleaseMutex(mutex_);
#else
        pthread_mutex_lock(&L->mtx);
        const uint64_t minRead = minReadSeqLocked_(L);
        L->readSeq = minRead;
        const uint64_t inFlight = (L->seq >= minRead) ? (L->seq - minRead) : 0ull;
        if (inFlight < static_cast<uint64_t>(cap)) {
            const uint64_t next = L->seq + 1;
            DynamicSlot* slot = map_->slotAt(static_cast<size_t>(next % cap));
            slot->seq = next;
            slot->size = static_cast<uint32_t>(enc.size());
            slot->reserved = 0;
            if (slot->size <= maxPayloadBytes) {
                if (slot->size != 0) std::memcpy(map_->slotData(slot), tmp.data(), slot->size);
                L->seq = next;
                ok = true;
            }
        }
        if (ok) pthread_cond_broadcast(&L->cv);
        pthread_mutex_unlock(&L->mtx);
#endif

        if (!ok) return false;

        // Notify all subscribers so LoopPoller dispatches callbacks promptly.
        std::vector<uint32_t> pids;
        detail::SubscribersRegistryTable<>::listSubscriberPids(reg_.domain(), reg_.object(), signalName_, pids);
        if (!pids.empty()) {
            std::sort(pids.begin(), pids.end());
            pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
            for (size_t i = 0; i < pids.size(); ++i) {
                detail::LoopPoller::notifyProcess(pids[i]);
            }
        }

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

        struct State {
            std::shared_ptr<DynamicMapping> map;
            Callback cb;
            bool fireInitial;
            std::atomic_bool inCallback;
            uint32_t subPid;
            SwString subscriberObject;

#ifdef _WIN32
            HANDLE mtx;
#endif

            /**
             * @brief Constructs a `State` instance.
             * @param m Value passed to the method.
             * @param c Value passed to the method.
             * @param fi Value passed to the method.
             * @param pid Value passed to the method.
             * @param _WIN32 Value passed to the method.
             * @param NULL Value passed to the method.
             *
             * @details The instance is initialized and prepared for immediate use.
             */
            State(const std::shared_ptr<DynamicMapping>& m,
                  Callback c,
                  bool fi,
                  uint32_t pid,
                  const SwString& subObj)
                : map(m),
                  cb(std::move(c)),
                  fireInitial(fi),
                  inCallback(false),
                  subPid(pid),
                  subscriberObject(subObj)
#ifdef _WIN32
                  ,
                  mtx(NULL)
#endif
            {
            }

#ifdef _WIN32
            /**
             * @brief Destroys the `State` instance.
             *
             * @details Use this hook to release any resources that remain associated with the instance.
             */
            ~State() {
                if (mtx) ::CloseHandle(mtx);
            }
#endif
        };

        const SwString domCopy = reg_.domain();
        const SwString objCopy = reg_.object();
        const SwString sigCopy = signalName_;
        const uint32_t subPid = detail::currentPid();
        const SwString subObjCopy = detail::currentSubscriberObject_();

        std::shared_ptr<State> st(new State(map_, cbCopy, fireInitial, subPid, subObjCopy));

#ifdef _WIN32
        const std::string mtxName = (shmName_ + "_mtx").toStdString();
        st->mtx = ::CreateMutexA(NULL, FALSE, mtxName.c_str());
        if (!st->mtx) {
            throw std::runtime_error("CreateMutex (loop sub) failed");
        }
#endif

        const size_t id = detail::LoopPoller::instance().add([st]() {
            if (!st) return;
            if (st->inCallback.exchange(true, std::memory_order_acq_rel)) return;

            Header* L = st->map ? st->map->header() : nullptr;
            if (!L) {
                st->inCallback.store(false, std::memory_order_release);
                return;
            }

            struct Msg {
                uint32_t sz;
                std::vector<uint8_t> data;
            };
            std::vector<Msg> msgs;

#ifdef _WIN32
            if (!st->mtx) {
                st->inCallback.store(false, std::memory_order_release);
                return;
            }
            DWORD wr = ::WaitForSingleObject(st->mtx, INFINITE);
            if (wr != WAIT_OBJECT_0 && wr != WAIT_ABANDONED) {
                st->inCallback.store(false, std::memory_order_release);
                return;
            }
#else
            pthread_mutex_lock(&L->mtx);
#endif

            const uint32_t cap = L->capacity;
            const uint32_t maxPayloadBytes = L->maxPayload;
            SubscriberCursor* cursor =
                findOrCreateCursorLocked_(L, st->subPid, st->subscriberObject, st->fireInitial);
            if (!cursor) {
#ifdef _WIN32
                    ::ReleaseMutex(st->mtx);
#else
                    pthread_mutex_unlock(&L->mtx);
#endif
                st->inCallback.store(false, std::memory_order_release);
                return;
            }

            const uint64_t seqNow = L->seq;

            if (cursor->readSeq < seqNow) {
                const uint64_t start = cursor->readSeq + 1ull;
                for (uint64_t seq = start; seq <= seqNow; ++seq) {
                    DynamicSlot* slot = st->map->slotAt(static_cast<size_t>(seq % cap));
                    const uint32_t sz = slot->size;
                    if (slot->seq == seq && sz <= maxPayloadBytes) {
                        Msg m;
                        m.sz = sz;
                        m.data.resize(static_cast<size_t>(sz));
                        if (sz != 0) std::memcpy(m.data.data(), st->map->slotData(slot), sz);
                        msgs.push_back(std::move(m));
                    }
                }
                cursor->readSeq = seqNow;
            }
            cursor->lastSeenMs = detail::nowMs();
            L->readSeq = minReadSeqLocked_(L);

#ifdef _WIN32
            ::ReleaseMutex(st->mtx);
#else
            pthread_mutex_unlock(&L->mtx);
#endif

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
            std::shared_ptr<DynamicMapping> mapCopy = map_;
            const SwString shmCopy = shmName_;
            SwTimer::singleShot(timeoutMs, [id, domCopy, objCopy, sigCopy, subPid, subObjCopy, mapCopy, shmCopy]() {
                detail::SubscribersRegistryTable<>::unregisterSubscription(domCopy, objCopy, sigCopy, subPid, subObjCopy);
                clearSubscriberCursor_(mapCopy, shmCopy, subPid, subObjCopy);
                detail::LoopPoller::instance().remove(id);
            });
        }

        return Subscription(id, domCopy, objCopy, sigCopy, subPid, subObjCopy, map_, shmName_);
    }

private:
    struct SubscriberCursor {
        uint32_t subPid;
        uint32_t active;
        uint64_t readSeq;
        uint64_t lastSeenMs;
        char subscriberObject[64];
    };

    struct Header {
        uint32_t magic;
        uint32_t version;
        uint64_t typeId;
        uint32_t capacity;
        uint32_t maxPayload;
        uint32_t cursorCount;
        uint32_t cursorCapacity;

#ifndef _WIN32
        pthread_mutex_t mtx;
        pthread_cond_t cv;
#endif

        uint64_t seq;
        uint64_t readSeq;
        uint32_t winWaiters;
        uint32_t reserved0;
        SubscriberCursor cursors[kMaxSubscriberCursors];
        uint8_t reserved[64];

        static const uint32_t kMagic = 0x51554431u; // 'QUD1'
        static const uint32_t kVersion = 2;
    };

    struct DynamicSlot {
        uint64_t seq;
        uint32_t size;
        uint32_t reserved;
    };

    class DynamicMapping {
    public:
#ifdef _WIN32
        /**
         * @brief Constructs a `DynamicMapping` instance.
         * @param name Value passed to the method.
         * @param mem Value passed to the method.
         * @param hMap Value passed to the method.
         * @param mappedSize Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        DynamicMapping(const SwString& name, void* mem, HANDLE hMap, size_t mappedSize)
            : name_(name), mem_(mem), hMap_(hMap), mappedSize_(mappedSize) {}
#else
        /**
         * @brief Constructs a `DynamicMapping` instance.
         * @param name Value passed to the method.
         * @param mem Value passed to the method.
         * @param mappedSize Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        DynamicMapping(const SwString& name, void* mem, size_t mappedSize)
            : name_(name), mem_(mem), mappedSize_(mappedSize) {}
#endif

        /**
         * @brief Destroys the `DynamicMapping` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~DynamicMapping() {
#ifdef _WIN32
            if (mem_) ::UnmapViewOfFile(mem_);
            if (hMap_) ::CloseHandle(hMap_);
#else
            if (mem_ && mem_ != MAP_FAILED) ::munmap(mem_, mappedSize_);
#endif
        }

        /**
         * @brief Performs the `header` operation.
         * @param mem_ Value passed to the method.
         * @return The requested header.
         */
        Header* header() const { return static_cast<Header*>(mem_); }
        /**
         * @brief Returns the current mapped Size.
         * @return The current mapped Size.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        size_t mappedSize() const { return mappedSize_; }

        /**
         * @brief Performs the `configure` operation.
         * @param entriesOffset Value passed to the method.
         * @param slotStride Value passed to the method.
         * @param requiredSize Value passed to the method.
         */
        void configure(size_t entriesOffset, size_t slotStride, size_t requiredSize) {
            entriesOffset_ = entriesOffset;
            slotStride_ = slotStride;
            requiredSize_ = requiredSize;
        }

        /**
         * @brief Performs the `slotAt` operation.
         * @param slotIndex Value passed to the method.
         * @return The requested slot At.
         */
        DynamicSlot* slotAt(size_t slotIndex) const {
            uint8_t* base = static_cast<uint8_t*>(mem_) + entriesOffset_ + slotIndex * slotStride_;
            return reinterpret_cast<DynamicSlot*>(base);
        }

        /**
         * @brief Performs the `slotData` operation.
         * @param slot Value passed to the method.
         * @return The requested slot Data.
         */
        uint8_t* slotData(DynamicSlot* slot) const {
            return reinterpret_cast<uint8_t*>(slot) + sizeof(DynamicSlot);
        }

    private:
        SwString name_;
        void* mem_{nullptr};
        size_t mappedSize_{0};
        size_t entriesOffset_{0};
        size_t slotStride_{0};
        size_t requiredSize_{0};
#ifdef _WIN32
        HANDLE hMap_{NULL};
#endif
    };

    static std::string normalizedSubscriberObjectKey_(const SwString& subscriberObject) {
        std::string key = subscriberObject.toStdString();
        const size_t cap = sizeof(SubscriberCursor::subscriberObject);
        if (cap > 0 && key.size() >= cap) key.resize(cap - 1);
        return key;
    }

    static bool cursorMatches_(const SubscriberCursor& c, uint32_t subPid, const std::string& key) {
        if (c.active == 0u) return false;
        if (c.subPid != subPid) return false;
        return std::string(c.subscriberObject) == key;
    }

    static uint32_t recomputeCursorCountLocked_(Header* L) {
        if (!L) return 0u;
        uint32_t n = 0u;
        for (uint32_t i = 0; i < kMaxSubscriberCursors; ++i) {
            if (L->cursors[i].active != 0u) ++n;
        }
        L->cursorCount = n;
        return n;
    }

    static SubscriberCursor* findCursorLocked_(Header* L, uint32_t subPid, const std::string& key) {
        if (!L) return nullptr;
        for (uint32_t i = 0; i < kMaxSubscriberCursors; ++i) {
            if (cursorMatches_(L->cursors[i], subPid, key)) {
                return &L->cursors[i];
            }
        }
        return nullptr;
    }

    static SubscriberCursor* findOrCreateCursorLocked_(Header* L,
                                                       uint32_t subPid,
                                                       const SwString& subscriberObject,
                                                       bool fireInitial) {
        if (!L) return nullptr;
        const std::string key = normalizedSubscriberObjectKey_(subscriberObject);

        SubscriberCursor* c = findCursorLocked_(L, subPid, key);
        if (c) {
            c->lastSeenMs = detail::nowMs();
            return c;
        }

        SubscriberCursor* freeSlot = nullptr;
        for (uint32_t i = 0; i < kMaxSubscriberCursors; ++i) {
            if (L->cursors[i].active == 0u) {
                freeSlot = &L->cursors[i];
                break;
            }
        }

        if (!freeSlot) {
            for (uint32_t i = 0; i < kMaxSubscriberCursors; ++i) {
                SubscriberCursor& c0 = L->cursors[i];
                if (c0.active == 0u) continue;
                if (detail::pidStateBestEffort_(c0.subPid) == detail::PidState::Dead) {
                    std::memset(&c0, 0, sizeof(SubscriberCursor));
                    freeSlot = &c0;
                    break;
                }
            }
        }
        if (!freeSlot) return nullptr;

        std::memset(freeSlot, 0, sizeof(SubscriberCursor));
        freeSlot->subPid = subPid;
        freeSlot->active = 1u;
        freeSlot->lastSeenMs = detail::nowMs();
        detail::copyTrunc(freeSlot->subscriberObject, sizeof(freeSlot->subscriberObject), SwString(key));

        const uint64_t seqNow = L->seq;
        if (fireInitial) {
            const uint64_t cap = static_cast<uint64_t>(L->capacity);
            freeSlot->readSeq = (seqNow > cap) ? (seqNow - cap) : 0ull;
        } else {
            freeSlot->readSeq = seqNow;
        }

        recomputeCursorCountLocked_(L);
        return freeSlot;
    }

    static uint64_t minReadSeqLocked_(Header* L) {
        if (!L) return 0ull;
        uint64_t minRead = L->seq;
        bool found = false;
        for (uint32_t i = 0; i < kMaxSubscriberCursors; ++i) {
            const SubscriberCursor& c = L->cursors[i];
            if (c.active == 0u) continue;
            if (!found || c.readSeq < minRead) {
                minRead = c.readSeq;
                found = true;
            }
        }
        if (!found) minRead = L->seq;
        return minRead;
    }

    static void clearSubscriberCursor_(const std::shared_ptr<DynamicMapping>& map,
                                       const SwString& shmName,
                                       uint32_t subPid,
                                       const SwString& subscriberObject) {
        if (!map) return;
        Header* L = map->header();
        if (!L) return;
        const std::string key = normalizedSubscriberObjectKey_(subscriberObject);

#ifdef _WIN32
        const std::string mtxName = (shmName + "_mtx").toStdString();
        HANDLE mtx = ::CreateMutexA(NULL, FALSE, mtxName.c_str());
        if (!mtx) return;
        DWORD wr = ::WaitForSingleObject(mtx, 1000);
        if (wr != WAIT_OBJECT_0 && wr != WAIT_ABANDONED) {
            ::CloseHandle(mtx);
            return;
        }
#else
        pthread_mutex_lock(&L->mtx);
#endif

        for (uint32_t i = 0; i < kMaxSubscriberCursors; ++i) {
            SubscriberCursor& c = L->cursors[i];
            if (!cursorMatches_(c, subPid, key)) continue;
            std::memset(&c, 0, sizeof(SubscriberCursor));
            break;
        }
        recomputeCursorCountLocked_(L);
        L->readSeq = minReadSeqLocked_(L);

#ifdef _WIN32
        ::ReleaseMutex(mtx);
        ::CloseHandle(mtx);
#else
        pthread_mutex_unlock(&L->mtx);
#endif
    }

    static size_t alignUp_(size_t v, size_t a) {
        if (a == 0u) return v;
        const size_t rem = v % a;
        return (rem == 0u) ? v : (v + (a - rem));
    }

    static bool computeLayout_(uint32_t capacity,
                               uint32_t maxPayload,
                               size_t& entriesOffsetOut,
                               size_t& slotStrideOut,
                               size_t& totalSizeOut) {
        if (capacity == 0u || maxPayload == 0u) return false;

        const size_t align = alignof(DynamicSlot);
        const size_t entriesOffset = alignUp_(sizeof(Header), align);
        const size_t rawStride = sizeof(DynamicSlot) + static_cast<size_t>(maxPayload);
        const size_t slotStride = alignUp_(rawStride, align);

        if (slotStride == 0u) return false;
        if (static_cast<size_t>(capacity) > (std::numeric_limits<size_t>::max() - entriesOffset) / slotStride) {
            return false;
        }

        const size_t total = entriesOffset + static_cast<size_t>(capacity) * slotStride;
        entriesOffsetOut = entriesOffset;
        slotStrideOut = slotStride;
        totalSizeOut = total;
        return true;
    }

    static std::shared_ptr<DynamicMapping> openOrCreateMapping_(const SwString& shmName,
                                                                uint64_t expectedTypeId,
                                                                uint32_t expectedCapacity,
                                                                uint32_t expectedMaxPayload) {
        size_t expectedEntriesOffset = 0;
        size_t expectedSlotStride = 0;
        size_t expectedTotalSize = 0;
        if (!computeLayout_(expectedCapacity, expectedMaxPayload,
                            expectedEntriesOffset, expectedSlotStride, expectedTotalSize)) {
            throw std::runtime_error("RingQueueDynamic: invalid queue layout");
        }

        bool created = false;

#ifdef _WIN32
        const std::string nameA = shmName.toStdString();
        HANDLE hMap = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                           static_cast<DWORD>(expectedTotalSize), nameA.c_str());
        const DWORD lastErr = ::GetLastError();
        if (!hMap) {
            throw std::runtime_error("CreateFileMapping failed");
        }
        created = (lastErr != ERROR_ALREADY_EXISTS);

        void* mem = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!mem) {
            ::CloseHandle(hMap);
            throw std::runtime_error("MapViewOfFile failed");
        }

        std::shared_ptr<DynamicMapping> mapping(new DynamicMapping(shmName, mem, hMap, expectedTotalSize));
#else
        const std::string nameA = shmName.toStdString();
        int fd = ::shm_open(nameA.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            created = true;
            detail::ensureSharedMemoryPermissions_(fd);
            if (::ftruncate(fd, static_cast<off_t>(expectedTotalSize)) != 0) {
                ::close(fd);
                ::shm_unlink(nameA.c_str());
                throw std::runtime_error("ftruncate(shm) failed");
            }
        } else if (errno == EEXIST) {
            fd = ::shm_open(nameA.c_str(), O_RDWR, 0666);
            if (fd < 0) throw std::runtime_error("shm_open(existing) failed");
            detail::ensureSharedMemoryPermissions_(fd);
        } else {
            throw std::runtime_error("shm_open failed");
        }

        struct stat st;
        if (::fstat(fd, &st) != 0) {
            ::close(fd);
            throw std::runtime_error("fstat(shm) failed");
        }
        const size_t mappedSize = static_cast<size_t>(st.st_size);
        if (mappedSize < sizeof(Header)) {
            ::close(fd);
            throw std::runtime_error("shm too small");
        }

        void* mem = ::mmap(NULL, mappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (mem == MAP_FAILED) throw std::runtime_error("mmap failed");

        std::shared_ptr<DynamicMapping> mapping(new DynamicMapping(shmName, mem, mappedSize));
#endif

        Header* L = mapping->header();

#ifdef _WIN32
        const std::string initName = shmName.toStdString() + "_init";
        HANDLE initMtx = ::CreateMutexA(NULL, FALSE, initName.c_str());
        if (!initMtx) {
            throw std::runtime_error("CreateMutex(init) failed");
        }
        ::WaitForSingleObject(initMtx, INFINITE);
        try {
#endif
            if (created || (L->magic == 0 && L->version == 0)) {
                if (mapping->mappedSize() < expectedTotalSize) {
                    throw std::runtime_error("SHM queue mapping too small for requested capacity");
                }

                std::memset(L, 0, expectedTotalSize);
                L->magic = Header::kMagic;
                L->version = Header::kVersion;
                L->typeId = expectedTypeId;
                L->capacity = expectedCapacity;
                L->maxPayload = expectedMaxPayload;
                L->cursorCount = 0;
                L->cursorCapacity = kMaxSubscriberCursors;
                L->seq = 0;
                L->readSeq = 0;
                L->winWaiters = 0;
                L->reserved0 = 0;

#ifndef _WIN32
                pthread_mutexattr_t ma;
                pthread_condattr_t ca;
                pthread_mutexattr_init(&ma);
                pthread_condattr_init(&ca);
                pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
                pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
                pthread_mutex_init(&L->mtx, &ma);
                pthread_cond_init(&L->cv, &ca);
                pthread_mutexattr_destroy(&ma);
                pthread_condattr_destroy(&ca);
#endif

                uint8_t* entriesBase = reinterpret_cast<uint8_t*>(L) + expectedEntriesOffset;
                for (size_t i = 0; i < static_cast<size_t>(expectedCapacity); ++i) {
                    DynamicSlot* slot =
                        reinterpret_cast<DynamicSlot*>(entriesBase + i * expectedSlotStride);
                    slot->seq = 0;
                    slot->size = 0;
                    slot->reserved = 0;
                }
            } else {
                if (L->magic != Header::kMagic || L->version != Header::kVersion) {
                    throw std::runtime_error("SHM queue layout mismatch (magic/version)");
                }
                if (L->typeId != expectedTypeId) {
                    throw std::runtime_error("SHM queue type mismatch");
                }
                if (L->capacity != expectedCapacity || L->maxPayload != expectedMaxPayload) {
                    throw std::runtime_error("SHM queue config mismatch (capacity/maxPayload)");
                }
                if (L->cursorCapacity != kMaxSubscriberCursors) {
                    throw std::runtime_error("SHM queue cursor config mismatch");
                }
            }

            size_t actualEntriesOffset = 0;
            size_t actualSlotStride = 0;
            size_t actualTotalSize = 0;
            if (!computeLayout_(L->capacity, L->maxPayload,
                                actualEntriesOffset, actualSlotStride, actualTotalSize)) {
                throw std::runtime_error("SHM queue layout invalid");
            }
            if (mapping->mappedSize() < actualTotalSize) {
                throw std::runtime_error("SHM queue mapping smaller than layout");
            }

            mapping->configure(actualEntriesOffset, actualSlotStride, actualTotalSize);

#ifdef _WIN32
            ::ReleaseMutex(initMtx);
            ::CloseHandle(initMtx);
        } catch (...) {
            ::ReleaseMutex(initMtx);
            ::CloseHandle(initMtx);
            throw;
        }
#endif

        return mapping;
    }

    Registry& reg_;
    SwString signalName_;
    SwString shmName_;
    std::shared_ptr<DynamicMapping> map_;
#ifdef _WIN32
    HANDLE mutex_{NULL};
#endif
};

template <class... Args>
class Signal {
public:
    static const size_t kMaxPayload = 4096;
    typedef ShmMapping<kMaxPayload> Mapping;
    typedef ShmLayout<kMaxPayload> Layout;

    /**
     * @brief Constructs a `Signal` instance.
     * @param reg Value passed to the method.
     * @param signalName Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    Signal(Registry& reg, const SwString& signalName)
        : reg_(reg), signalName_(signalName) {
        shmName_ = detail::make_shm_name(reg_.domain(), reg_.object(), signalName_);
        map_ = Mapping::openOrCreate(shmName_, detail::type_id<Args...>());
        detail::RegistryTable<>::registerSignal(reg_.domain(), reg_.object(), signalName_, shmName_, detail::type_id<Args...>(), detail::type_name<Args...>());
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
#endif
    }

    /**
     * @brief Destroys the `Signal` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~Signal() {
#ifdef _WIN32
        if (mutex_) {
            ::CloseHandle(mutex_);
            mutex_ = NULL;
        }
        if (semaphore_) {
            ::CloseHandle(semaphore_);
            semaphore_ = NULL;
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

    /**
     * @brief Performs the `publish` operation.
     * @param args Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool publish(const Args&... args) {
        std::array<uint8_t, kMaxPayload> tmp;
        detail::Encoder enc(tmp.data(), tmp.size());
        if (!detail::writeAll(enc, args...)) return false;

        Layout* L = map_->layout();

#ifdef _WIN32
        ::WaitForSingleObject(mutex_, INFINITE);
        std::memcpy(L->data, tmp.data(), enc.size());
        L->size = static_cast<uint32_t>(enc.size());
        ::InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&L->seq));
        ::ReleaseMutex(mutex_);
        if (detail::win_wake_by_address_all_fn()) {
            detail::win_wake_by_address_all(&L->seq);
        } else {
            const LONG w = static_cast<LONG>(L->winWaiters);
            if (w > 0) {
                ::ReleaseSemaphore(semaphore_, w, NULL);
            }
        }
#else
        pthread_mutex_lock(&L->mtx);
        std::memcpy(L->data, tmp.data(), enc.size());
        L->size = static_cast<uint32_t>(enc.size());
        L->seq++;
        pthread_cond_broadcast(&L->cv);
        pthread_mutex_unlock(&L->mtx);
#endif

        // Event-driven wakeup for loop-based subscriptions (no polling).
        // We notify subscriber processes best-effort; they will dispatch in their main loop thread.
        std::vector<uint32_t> pids;
        detail::SubscribersRegistryTable<>::listSubscriberPids(reg_.domain(), reg_.object(), signalName_, pids);
        if (!pids.empty()) {
            std::sort(pids.begin(), pids.end());
            pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
            for (size_t i = 0; i < pids.size(); ++i) {
                detail::LoopPoller::notifyProcess(pids[i]);
            }
        }
        return true;
    }

    /**
     * @brief Performs the `operator` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator()(const Args&... args) { return publish(args...); }

    /**
     * @brief Performs the `readLatest` operation on the associated resource.
     * @param out Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool readLatest(Args&... out) const {
        Layout* L = map_->layout();
        std::array<uint8_t, kMaxPayload> tmp;
        uint32_t sz = 0;

#ifdef _WIN32
        ::WaitForSingleObject(mutex_, INFINITE);
        sz = L->size;
        if (sz != 0 && sz <= kMaxPayload) {
            std::memcpy(tmp.data(), L->data, sz);
        }
        ::ReleaseMutex(mutex_);
#else
        pthread_mutex_lock(&L->mtx);
        sz = L->size;
        if (sz != 0 && sz <= kMaxPayload) {
            std::memcpy(tmp.data(), L->data, sz);
        }
        pthread_mutex_unlock(&L->mtx);
#endif

        if (sz == 0 || sz > kMaxPayload) return false;

        detail::Decoder dec(tmp.data(), sz);
        return detail::readAll(dec, out...);
    }


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
         * @param id Value passed to the method.
         * @param domain Value passed to the method.
         * @param object Value passed to the method.
         * @param signal Value passed to the method.
         * @param subPid Value passed to the method.
         * @param true Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit Subscription(size_t id,
                                   const SwString& domain,
                                   const SwString& object,
                                   const SwString& signal,
                                   uint32_t subPid,
                                   const SwString& subscriberObject)
            : id_(id),
              domain_(domain),
              object_(object),
              signal_(signal),
              subPid_(subPid),
              subscriberObject_(subscriberObject),
              registered_(true) {}
        /**
         * @brief Destroys the `Subscription` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~Subscription() { stop(); }

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
            id_ = o.id_;
            o.id_ = 0;
            domain_ = std::move(o.domain_);
            object_ = std::move(o.object_);
            signal_ = std::move(o.signal_);
            subPid_ = o.subPid_;
            subscriberObject_ = std::move(o.subscriberObject_);
            registered_ = o.registered_;
            o.registered_ = false;
            return *this;
        }

        /**
         * @brief Stops the underlying activity managed by the object.
         *
         * @details The call affects the runtime state associated with the underlying resource or service.
         */
        void stop() {
            if (!id_) return;
            if (registered_) {
                detail::SubscribersRegistryTable<>::unregisterSubscription(domain_, object_, signal_, subPid_, subscriberObject_);
                registered_ = false;
            }
            detail::LoopPoller::instance().remove(id_);
            id_ = 0;
        }

    private:
        size_t id_{0};
        SwString domain_;
        SwString object_;
        SwString signal_;
        uint32_t subPid_{0};
        SwString subscriberObject_;
        bool registered_{false};
    };

    // Subscribe without creating a dedicated OS thread:
    // - Uses a per-process OS wakeup (eventfd / named event) to become event-driven (no polling)
    // - Uses try-lock to avoid blocking the event loop thread
    // - Good for "state signals" (latest value wins)
    // - Optional timeoutMs to auto-stop the runtime
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

        std::shared_ptr<Mapping> map = map_;

        struct State {
            uint64_t lastSeq;
            bool fireInitial;
            bool firedInitial;
            std::atomic_bool inCallback;
#ifdef _WIN32
            HANDLE mtx;
            /**
             * @brief Constructs a `State` instance.
             * @param NULL Value passed to the method.
             *
             * @details The instance is initialized and prepared for immediate use.
             */
            explicit State() : lastSeq(0), fireInitial(true), firedInitial(false), inCallback(false), mtx(NULL) {}
            /**
             * @brief Destroys the `State` instance.
             * @param mtx Value passed to the method.
             *
             * @details Use this hook to release any resources that remain associated with the instance.
             */
            ~State() { if (mtx) ::CloseHandle(mtx); }
#else
            /**
             * @brief Constructs a `State` instance.
             * @param false Value passed to the method.
             *
             * @details The instance is initialized and prepared for immediate use.
             */
            explicit State() : lastSeq(0), fireInitial(true), firedInitial(false), inCallback(false) {}
#endif
        };
        std::shared_ptr<State> st(new State());

#ifdef _WIN32
        const std::string mtxName = (shmName_ + "_mtx").toStdString();
        st->mtx = ::CreateMutexA(NULL, FALSE, mtxName.c_str());
#endif

        // Seed lastSeq at connect-time so we don't miss publishes that happen before the runtime starts.
        st->fireInitial = fireInitial;
        st->firedInitial = false;
        st->lastSeq = detail::atomic_load_u64(&map->layout()->seq);

        const size_t id = detail::LoopPoller::instance().add([map, st, cbCopy]() mutable {
            if (!map || !st) return;
            if (st->inCallback.exchange(true, std::memory_order_acq_rel)) return;

            Layout* L = map->layout();

            // Fast path: avoid locking/copying when nothing changed (except when we still owe fireInitial).
            if (!(st->fireInitial && !st->firedInitial)) {
                const uint64_t seqFast = detail::atomic_load_u64(&L->seq);
                if (seqFast == st->lastSeq) {
                    st->inCallback.store(false, std::memory_order_release);
                    return;
                }
            }

            uint32_t sz = 0;
            uint64_t seq = 0;
            std::array<uint8_t, kMaxPayload> tmp;
            bool hasPayload = false;

#ifdef _WIN32
            if (!st->mtx) { st->inCallback.store(false, std::memory_order_release); return; }
            DWORD wr = ::WaitForSingleObject(st->mtx, 0);
            if (wr != WAIT_OBJECT_0 && wr != WAIT_ABANDONED) { st->inCallback.store(false, std::memory_order_release); return; }
            seq = L->seq;
            if (!(st->fireInitial && !st->firedInitial) && seq == st->lastSeq) {
                ::ReleaseMutex(st->mtx);
                st->inCallback.store(false, std::memory_order_release);
                return;
            }
            sz = L->size;
            if (sz != 0 && sz <= kMaxPayload) {
                std::memcpy(tmp.data(), L->data, sz);
                hasPayload = true;
            }
            ::ReleaseMutex(st->mtx);
#else
            if (pthread_mutex_trylock(&L->mtx) != 0) { st->inCallback.store(false, std::memory_order_release); return; }
            seq = L->seq;
            if (!(st->fireInitial && !st->firedInitial) && seq == st->lastSeq) {
                pthread_mutex_unlock(&L->mtx);
                st->inCallback.store(false, std::memory_order_release);
                return;
            }
            sz = L->size;
            if (sz != 0 && sz <= kMaxPayload) {
                std::memcpy(tmp.data(), L->data, sz);
                hasPayload = true;
            }
            pthread_mutex_unlock(&L->mtx);
#endif

            if (st->fireInitial && !st->firedInitial) {
                st->firedInitial = true;
                st->lastSeq = seq;
                if (!hasPayload) { st->inCallback.store(false, std::memory_order_release); return; }
            } else {
                st->lastSeq = seq;
                if (!hasPayload) { st->inCallback.store(false, std::memory_order_release); return; }
            }

            typedef std::tuple<typename std::decay<Args>::type...> Tuple;
            Tuple out;
            detail::Decoder dec(tmp.data(), sz);
            if (detail::readTuple(dec, out)) {
                detail::invokeWithTuple(cbCopy, out);
            }

            st->inCallback.store(false, std::memory_order_release);
        });

        const uint32_t subPid = detail::currentPid();
        const SwString subObjCopy = detail::currentSubscriberObject_();
        detail::SubscribersRegistryTable<>::registerSubscription(reg_.domain(), reg_.object(), signalName_, subPid, subObjCopy);

        if (timeoutMs > 0) {
            const SwString domCopy = reg_.domain();
            const SwString objCopy = reg_.object();
            const SwString sigCopy = signalName_;
            SwTimer::singleShot(timeoutMs, [id, domCopy, objCopy, sigCopy, subPid, subObjCopy]() {
                detail::SubscribersRegistryTable<>::unregisterSubscription(domCopy, objCopy, sigCopy, subPid, subObjCopy);
                detail::LoopPoller::instance().remove(id);
            });
        }

        return Subscription(id, reg_.domain(), reg_.object(), signalName_, subPid, subObjCopy);
    }

private:
    Registry& reg_;
    SwString signalName_;
    SwString shmName_;
    std::shared_ptr<Mapping> map_;
#ifdef _WIN32
    HANDLE mutex_{NULL};
    HANDLE semaphore_{NULL};
#endif
};

/**
 * @brief Small proxy that makes IPC signals feel like SwObject signals:
 *        - call: `emit ping(1, "x")` (because `emit` macro expands to nothing)
 *        - subscribe: `ping.connect(...)`
 *        - still available: `ping.publish(...)`
 */
template <class... Args>
class SignalProxy {
public:
    /**
     * @brief Constructs a `SignalProxy` instance.
     * @param reg Value passed to the method.
     * @param signalName Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SignalProxy(Registry& reg, const SwString& signalName)
        : sig_(reg, signalName) {}

    /**
     * @brief Performs the `publish` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool publish(const Args&... args) { return sig_.publish(args...); }
    /**
     * @brief Performs the `operator` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator()(const Args&... args) { return publish(args...); }

    /**
     * @brief Performs the `readLatest` operation on the associated resource.
     * @return `true` on success; otherwise `false`.
     */
    bool readLatest(Args&... out) const { return sig_.readLatest(out...); }

    template <typename Fn>
    /**
     * @brief Performs the `connect` operation.
     * @param cb Value passed to the method.
     * @param fireInitial Value passed to the method.
     * @param timeoutMs Timeout expressed in milliseconds.
     * @return The requested connect.
     */
    typename Signal<Args...>::Subscription connect(Fn cb, bool fireInitial = true, int timeoutMs = 0) {
        return sig_.connect(cb, fireInitial, timeoutMs);
    }

    /**
     * @brief Performs the `shmName` operation.
     * @return The requested shm Name.
     */
    const SwString& shmName() const { return sig_.shmName(); }

private:
    Signal<Args...> sig_;
};

namespace detail {

inline void notifyRegistryChangedBestEffort_(const SwString& domain) {
    if (domain.isEmpty()) return;

    try {
        struct Notifier {
            Registry reg;
            Signal<uint64_t> sig;
            /**
             * @brief Constructs a `Notifier` instance.
             *
             * @details The instance is initialized and prepared for immediate use.
             */
            explicit Notifier(const SwString& dom)
                : reg(dom, registryEventsObjectName_()),
                  sig(reg, registryEventsSignalName_()) {}
        };

        static std::mutex* mtx = new std::mutex();
        static std::map<std::string, std::shared_ptr<Notifier>>* byDomain =
            new std::map<std::string, std::shared_ptr<Notifier>>();

        const std::string key = domain.toStdString();
        std::shared_ptr<Notifier> n;
        {
            std::lock_guard<std::mutex> lk(*mtx);
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

// Convenience macro (requires a member named `ipcRegistry_` in the class).
#define SW_REGISTER_SHM_SIGNAL(name, ...) ::sw::ipc::SignalProxy<__VA_ARGS__> name{ipcRegistry_, SwString(#name)}

} // namespace ipc
} // namespace sw
