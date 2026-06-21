/***************************************************************************************************
 * SwHostResolver — process-wide, asynchronous, cached DNS resolver.
 *
 * Purpose: keep blocking ::getaddrinfo OFF event-loop threads. Socket layers
 * (SwTcpSocket/SwUdpSocket and everything built on them: SwSslSocket, SwWebSocket,
 * SwHttpClient) consult this cache before resolving, so a host is resolved AT MOST ONCE
 * per TTL across the whole process, and a connect to an already-known host never runs a
 * synchronous getaddrinfo (it substitutes the cached IP literal, which getaddrinfo then
 * parses with no network I/O).
 *
 * Design:
 *  - cachedIp(host): synchronous, lock-guarded cache read. Empty on miss/expiry. Never resolves.
 *  - store(host, ip): synchronous cache write with TTL (positive or negative).
 *  - prewarm(host): fire-and-forget; resolves the host on a DEDICATED resolver thread and
 *    stores the result. Deduplicated (one in-flight resolve per host) and skipped when the
 *    cache is already valid. NO callbacks into callers -> no lifetime/UAF coupling.
 *
 * Callers that need the result delivered (vs. a connect that can substitute the cached IP)
 * combine cachedIp() (sync fast path) with their own off-thread resolve + store(); the cache
 * makes that resolve happen at most once. The resolver thread is intentionally leaked (the
 * singleton is never destroyed) so there is no thread-join-at-exit hang and no static
 * destruction-order hazard.
 **************************************************************************************************/
#pragma once

#include "SwString.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

class SwHostResolver {
public:
    static SwHostResolver& instance() {
        // Intentionally leaked: lives until process exit. Avoids joining a thread that
        // may be parked in getaddrinfo, and avoids static-destruction-order UAF.
        static SwHostResolver* inst = new SwHostResolver();
        return *inst;
    }

    // True for a numeric IPv4/IPv6 literal (no DNS needed). Safe on any thread.
    static bool isNumericHost(const SwString& host) {
        const std::string h = host.trimmed().toStdString();
        if (h.empty()) {
            return false;
        }
        struct in_addr v4;
        struct in6_addr v6;
        return ::inet_pton(AF_INET, h.c_str(), &v4) == 1 ||
               ::inet_pton(AF_INET6, h.c_str(), &v6) == 1;
    }

    // Synchronous cache lookup. Returns a cached IP literal, or empty on miss / expiry /
    // negative entry. Never performs DNS I/O.
    SwString cachedIp(const SwString& host) {
        const std::string key = host.trimmed().toStdString();
        if (key.empty()) {
            return SwString();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const std::unordered_map<std::string, Entry>::iterator it = cache_.find(key);
        if (it == cache_.end() || nowMs_() >= it->second.expiresAtMs || !it->second.ok) {
            return SwString();
        }
        return SwString(it->second.ip);
    }

    // Synchronous cache write. Empty ip => negative entry (short TTL).
    void store(const SwString& host, const SwString& ip) {
        const std::string key = host.trimmed().toStdString();
        if (key.empty()) {
            return;
        }
        const std::string ipStr = ip.trimmed().toStdString();
        std::lock_guard<std::mutex> lock(mutex_);
        Entry& entry = cache_[key];
        entry.ip = ipStr;
        entry.ok = !ipStr.empty();
        entry.expiresAtMs = nowMs_() + (entry.ok ? kPositiveTtlMs_ : kNegativeTtlMs_);
    }

    // Asynchronous, fire-and-forget warm-up. No-op if the cache is already valid or a
    // resolve for this host is already in flight. Resolution runs on the resolver thread.
    void prewarm(const SwString& host) {
        const std::string key = host.trimmed().toStdString();
        if (key.empty() || isNumericHost(host)) {
            return;
        }
        ensureThread_();
        std::lock_guard<std::mutex> lock(mutex_);
        const std::unordered_map<std::string, Entry>::iterator it = cache_.find(key);
        const bool cachedValid = it != cache_.end() && nowMs_() < it->second.expiresAtMs && it->second.ok;
        if (cachedValid) {
            return;
        }
        queueResolveLocked_(key);
    }

    // Resolve a host to an IPv4 literal (IPv6 fallback), serving from cache when possible.
    // SERVE-STALE: a fresh hit returns instantly; an expired-but-known entry returns the
    // last-known IP instantly AND kicks off an async refresh — so this BLOCKS only on the
    // very first resolve of a never-seen host (cold cache). After that, the caller thread
    // (incl. an event loop, via connectToHost) never blocks on DNS for this host. A stale
    // IP that has since changed simply fails the connect; the async refresh then heals it.
    SwString resolveBlockingAndCache(const SwString& host) {
        if (isNumericHost(host)) {
            return host.trimmed();
        }
        const std::string key = host.trimmed().toStdString();
        if (key.empty()) {
            return SwString();
        }
        ensureThread_();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::unordered_map<std::string, Entry>::iterator it = cache_.find(key);
            if (it != cache_.end() && it->second.ok && !it->second.ip.empty()) {
                const SwString ip(it->second.ip);
                if (nowMs_() >= it->second.expiresAtMs) {
                    // Stale: serve it now, refresh off-thread (don't block the caller).
                    queueResolveLocked_(key);
                }
                return ip;
            }
        }
        // Cold cache: resolve once on this thread (callers ensure this runs off the event
        // loop, e.g. a worker; the very first connect on the event loop may block once).
        const SwString resolved = resolveBlocking_(key);
        store(host, resolved);
        return resolved;
    }

private:
    struct Entry {
        std::string ip;
        bool ok = false;
        long long expiresAtMs = 0;
    };

    SwHostResolver() = default;

    static long long nowMs_() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void ensureThread_() {
        std::call_once(threadOnce_, [this]() {
            std::thread([this]() { runLoop_(); }).detach();
        });
    }

    // Enqueue an off-thread resolve for key, deduplicated. Caller MUST hold mutex_.
    void queueResolveLocked_(const std::string& key) {
        if (inFlight_.count(key) != 0) {
            return;
        }
        inFlight_.insert(key);
        pending_.push_back(key);
        cv_.notify_one();
    }

    void runLoop_() {
        for (;;) {
            std::string host;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return !pending_.empty(); });
                host = pending_.front();
                pending_.pop_front();
            }
            const SwString ip = resolveBlocking_(host);
            store(SwString(host), ip);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                inFlight_.erase(host);
            }
        }
    }

    static SwString resolveBlocking_(const std::string& host) {
        if (host.empty()) {
            return SwString();
        }
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
            return SwString();
        }
        char buffer[64];
        SwString out;
        // Prefer IPv4, fall back to the first IPv6.
        for (struct addrinfo* ptr = result; ptr && out.isEmpty(); ptr = ptr->ai_next) {
            if (ptr->ai_family == AF_INET) {
                struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(ptr->ai_addr);
                if (::inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer))) {
                    out = SwString(buffer);
                }
            }
        }
        for (struct addrinfo* ptr = result; ptr && out.isEmpty(); ptr = ptr->ai_next) {
            if (ptr->ai_family == AF_INET6) {
                struct sockaddr_in6* addr = reinterpret_cast<struct sockaddr_in6*>(ptr->ai_addr);
                if (::inet_ntop(AF_INET6, &addr->sin6_addr, buffer, sizeof(buffer))) {
                    out = SwString(buffer);
                }
            }
        }
        ::freeaddrinfo(result);
        return out;
    }

    // enum (not static const) so these are pure prvalues — never ODR-used, no out-of-line
    // definition needed in this header-only class.
    enum : long long {
        kPositiveTtlMs_ = 60000,  // 60s — one real resolve per host per minute
        kNegativeTtlMs_ = 5000,   // 5s — don't hammer a failing resolver
    };

    std::mutex mutex_;
    std::condition_variable cv_;
    std::once_flag threadOnce_;
    std::deque<std::string> pending_;
    std::unordered_set<std::string> inFlight_;
    std::unordered_map<std::string, Entry> cache_;
};
