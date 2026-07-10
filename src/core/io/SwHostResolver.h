/***************************************************************************************************
 * Process-wide DNS cache and resolver.
 *
 * resolveAllAsync() is the primary API: cold getaddrinfo calls run on the resolver worker,
 * concurrent requests for one hostname are coalesced, and every IPv4/IPv6 result is retained
 * for Happy Eyeballs consumers. Positive and negative entries have distinct TTLs. Expired
 * positive entries are served stale while a refresh runs; expired negative entries are resolved
 * again and never masquerade as an ordinary cache miss.
 *
 * cachedIp()/resolveBlockingAndCache() remain compatibility APIs. Event-loop code must use the
 * asynchronous API. The singleton and its worker are intentionally process-lifetime objects so
 * shutdown never waits on an OS resolver call or crosses static-destruction order.
 **************************************************************************************************/
#pragma once

#include "SwString.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    struct ResolvedAddress {
        sockaddr_storage storage{};
        socklen_t length{0};
        int family{AF_UNSPEC};
        SwString address;
    };

    using AddressList = std::vector<ResolvedAddress>;
    using ResolveCallback = std::function<void(const AddressList&)>;

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
        const std::string key = cacheKey_(host);
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

    AddressList cachedAddresses(const SwString& host, bool* negativeHit = nullptr) {
        if (negativeHit) {
            *negativeHit = false;
        }
        const std::string key = cacheKey_(host);
        if (key.empty()) {
            return AddressList();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = cache_.find(key);
        if (it == cache_.end() || nowMs_() >= it->second.expiresAtMs) {
            return AddressList();
        }
        if (!it->second.ok) {
            if (negativeHit) {
                *negativeHit = true;
            }
            return AddressList();
        }
        return it->second.addresses;
    }

    // Synchronous cache write. Empty ip => negative entry (short TTL).
    void store(const SwString& host, const SwString& ip) {
        const AddressList addresses = numericAddress_(ip.trimmed().toStdString());
        storeAll_(cacheKey_(host), addresses);
    }

    void resolveAllAsync(const SwString& host, ResolveCallback callback) {
        if (!callback) {
            return;
        }
        const std::string key = cacheKey_(host);
        if (key.empty()) {
            callback(AddressList());
            return;
        }
        const AddressList numeric = numericAddress_(key);
        if (!numeric.empty()) {
            callback(numeric);
            return;
        }

        ensureThread_();
        AddressList immediate;
        bool deliverImmediate = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = cache_.find(key);
            if (it != cache_.end() && nowMs_() < it->second.expiresAtMs) {
                immediate = it->second.addresses;
                deliverImmediate = true; // includes a valid negative cache hit
            } else if (it != cache_.end() && it->second.ok && !it->second.addresses.empty()) {
                // Serve stale immediately and refresh without coupling caller lifetime to DNS.
                immediate = it->second.addresses;
                deliverImmediate = true;
                queueResolveLocked_(key);
            } else {
                callbacks_[key].push_back(std::move(callback));
                queueResolveLocked_(key);
            }
        }
        if (deliverImmediate) {
            callback(immediate);
        }
    }

private:
    void storeAll_(const std::string& key, const AddressList& addresses) {
        if (key.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        Entry& entry = cache_[key];
        entry.addresses = addresses;
        entry.ip = preferredAddress_(addresses).toStdString();
        entry.ok = !addresses.empty();
        entry.expiresAtMs = nowMs_() + (entry.ok ? kPositiveTtlMs_ : kNegativeTtlMs_);
    }

public:

    // Asynchronous, fire-and-forget warm-up. No-op if the cache is already valid or a
    // resolve for this host is already in flight. Resolution runs on the resolver thread.
    void prewarm(const SwString& host) {
        const std::string key = cacheKey_(host);
        if (key.empty() || isNumericHost(host)) {
            return;
        }
        ensureThread_();
        std::lock_guard<std::mutex> lock(mutex_);
        const std::unordered_map<std::string, Entry>::iterator it = cache_.find(key);
        const bool cachedValid = it != cache_.end() && nowMs_() < it->second.expiresAtMs;
        if (cachedValid) {
            return;
        }
        queueResolveLocked_(key);
    }

    // Compatibility API returning one preferred literal. A cold call blocks and therefore
    // belongs only on a worker; event-loop transports use resolveAllAsync(). Positive stale
    // entries return immediately and refresh in the background.
    SwString resolveBlockingAndCache(const SwString& host) {
        if (isNumericHost(host)) {
            return host.trimmed();
        }
        const std::string key = cacheKey_(host);
        if (key.empty()) {
            return SwString();
        }
        ensureThread_();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::unordered_map<std::string, Entry>::iterator it = cache_.find(key);
            if (it != cache_.end() && nowMs_() < it->second.expiresAtMs) {
                return it->second.ok ? SwString(it->second.ip) : SwString();
            }
            if (it != cache_.end() && it->second.ok && !it->second.ip.empty()) {
                const SwString ip(it->second.ip);
                // Stale positive: serve it now, refresh off-thread (don't block the caller).
                queueResolveLocked_(key);
                return ip;
            }
        }
        // Cold compatibility call: resolve once on this worker/caller thread.
        const AddressList addresses = resolveAllBlocking_(key);
        storeAll_(key, addresses);
        return preferredAddress_(addresses);
    }

private:
    struct Entry {
        std::string ip;
        AddressList addresses;
        bool ok = false;
        long long expiresAtMs = 0;
    };

    SwHostResolver() = default;

    static std::string cacheKey_(const SwString& host) {
        // DNS names are case-insensitive. Sharing one entry avoids duplicate lookups for
        // spelling-only variants while leaving a trailing root dot semantically distinct.
        return host.trimmed().toLower().toStdString();
    }

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
            const AddressList addresses = resolveAllBlocking_(host);
            storeAll_(host, addresses);
            std::vector<ResolveCallback> callbacks;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                inFlight_.erase(host);
                auto callbackIt = callbacks_.find(host);
                if (callbackIt != callbacks_.end()) {
                    callbacks.swap(callbackIt->second);
                    callbacks_.erase(callbackIt);
                }
            }
            for (std::size_t i = 0; i < callbacks.size(); ++i) {
                if (callbacks[i]) {
                    callbacks[i](addresses);
                }
            }
        }
    }

    static SwString preferredAddress_(const AddressList& addresses) {
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            if (addresses[i].family == AF_INET) {
                return addresses[i].address;
            }
        }
        return addresses.empty() ? SwString() : addresses.front().address;
    }

    static AddressList numericAddress_(const std::string& host) {
        AddressList addresses;
        if (host.empty()) {
            return addresses;
        }

        ResolvedAddress resolved;
        sockaddr_in ipv4 {};
        if (::inet_pton(AF_INET, host.c_str(), &ipv4.sin_addr) == 1) {
            ipv4.sin_family = AF_INET;
            std::memcpy(&resolved.storage, &ipv4, sizeof(ipv4));
            resolved.length = static_cast<socklen_t>(sizeof(ipv4));
            resolved.family = AF_INET;
        } else {
            sockaddr_in6 ipv6 {};
            if (::inet_pton(AF_INET6, host.c_str(), &ipv6.sin6_addr) != 1) {
                return addresses;
            }
            ipv6.sin6_family = AF_INET6;
            std::memcpy(&resolved.storage, &ipv6, sizeof(ipv6));
            resolved.length = static_cast<socklen_t>(sizeof(ipv6));
            resolved.family = AF_INET6;
        }

        char buffer[INET6_ADDRSTRLEN] = {};
        const void* rawAddress = resolved.family == AF_INET
                                     ? static_cast<const void*>(
                                           &reinterpret_cast<const sockaddr_in*>(&resolved.storage)->sin_addr)
                                     : static_cast<const void*>(
                                           &reinterpret_cast<const sockaddr_in6*>(&resolved.storage)->sin6_addr);
        if (::inet_ntop(resolved.family, rawAddress, buffer, sizeof(buffer))) {
            resolved.address = SwString(buffer);
            addresses.push_back(resolved);
        }
        return addresses;
    }

    static AddressList resolveAllBlocking_(const std::string& host) {
        AddressList addresses = numericAddress_(host);
        if (!addresses.empty() || host.empty()) {
            return addresses;
        }
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
            return addresses;
        }
        for (struct addrinfo* ptr = result; ptr; ptr = ptr->ai_next) {
            if ((ptr->ai_family != AF_INET && ptr->ai_family != AF_INET6) ||
                !ptr->ai_addr || ptr->ai_addrlen > sizeof(sockaddr_storage)) {
                continue;
            }
            char buffer[INET6_ADDRSTRLEN] = {};
            const void* rawAddress = ptr->ai_family == AF_INET
                                         ? static_cast<const void*>(&reinterpret_cast<const sockaddr_in*>(ptr->ai_addr)->sin_addr)
                                         : static_cast<const void*>(&reinterpret_cast<const sockaddr_in6*>(ptr->ai_addr)->sin6_addr);
            if (!::inet_ntop(ptr->ai_family, rawAddress, buffer, sizeof(buffer))) {
                continue;
            }
            bool duplicate = false;
            for (std::size_t i = 0; i < addresses.size(); ++i) {
                if (addresses[i].family == ptr->ai_family && addresses[i].address == SwString(buffer)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                ResolvedAddress address;
                std::memcpy(&address.storage, ptr->ai_addr, static_cast<std::size_t>(ptr->ai_addrlen));
                address.length = static_cast<socklen_t>(ptr->ai_addrlen);
                address.family = ptr->ai_family;
                address.address = SwString(buffer);
                addresses.push_back(address);
            }
        }
        ::freeaddrinfo(result);
        return addresses;
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
    std::unordered_map<std::string, std::vector<ResolveCallback>> callbacks_;
};
