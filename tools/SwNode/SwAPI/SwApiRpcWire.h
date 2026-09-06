#pragma once
#include "SwIpcJsonCodec.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace swapi { namespace wire {
static uint64_t parseHexU64(const std::string& s) { return static_cast<uint64_t>(std::strtoull(s.c_str(), nullptr, 16)); }

static std::vector<std::string> parseArgTypesFromTypeName(const std::string& typeName) {
    // Best-effort parser for the MSVC-like "<...>" chunk.
    std::vector<std::string> out;
    const size_t lt = typeName.find('<');
    const size_t gt = (lt == std::string::npos) ? std::string::npos : typeName.find('>', lt + 1);
    if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1) return out;

    std::string inside = typeName.substr(lt + 1, gt - lt - 1);

    auto trimInPlace = [](std::string& s) {
        while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.erase(0, 1);
        while (!s.empty() && (s[s.size() - 1] == ' ' || s[s.size() - 1] == '\t')) s.pop_back();
    };

    size_t start = 0;
    while (start < inside.size()) {
        size_t comma = inside.find(',', start);
        if (comma == std::string::npos) comma = inside.size();
        std::string token = inside.substr(start, comma - start);
        trimInPlace(token);
        const std::string classPrefix = "class ";
        const std::string structPrefix = "struct ";
        if (token.find(classPrefix) == 0) token.erase(0, classPrefix.size());
        if (token.find(structPrefix) == 0) token.erase(0, structPrefix.size());
        trimInPlace(token);
        if (!token.empty()) out.push_back(token);
        start = comma + 1;
    }

    return out;
}

static bool isBoolType(const std::string& t) { return t == "bool" || t == "BOOL"; }
static bool isIntType(const std::string& t) { return t == "int" || t == "int32_t" || t == "signed int"; }
static bool isU32Type(const std::string& t) { return t == "uint32_t" || t == "unsigned int" || t == "unsigned long"; }
static bool isU64Type(const std::string& t) { return t == "uint64_t" || t == "unsigned __int64" || t == "unsigned long long"; }
static bool isFloatType(const std::string& t) { return t == "double" || t == "float"; }
static bool isStringType(const std::string& t) { return t == "SwString" || t == "class SwString" || t == "struct SwString"; }
static bool isBytesType(const std::string& t) { return t == "SwByteArray" || t == "class SwByteArray" || t == "struct SwByteArray"; }

struct RpcQueueInfo {
    SwString signal;
    SwString shmName;
    SwString domain;
    SwString object;
    SwString typeName;
    uint64_t typeId{0};
};

static bool findSignalInRegistryForTarget(const SwString& domain,
                                         const SwString& object,
                                         const SwString& signalName,
                                         RpcQueueInfo& out) {
    SwJsonArray all = sw::ipc::shmRegistrySnapshot(domain);
    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["object"].toString()) != object) continue;
        if (SwString(o["signal"].toString()) != signalName) continue;

        out.domain = domain; out.object = object;
        out.signal = signalName;
        out.shmName = SwString(o["shmName"].toString());
        out.typeName = SwString(o["typeName"].toString());
        out.typeId = parseHexU64(o["typeId"].toString().toStdString());
        return true;
    }
    return false;
}

static bool methodMatchesQueueMethod(const SwString& requestedMethod,
                                     const SwString& queueMethod) {
    if (requestedMethod == queueMethod) {
        return true;
    }

    const int sep = queueMethod.indexOf("|");
    if (sep <= 0) {
        return false;
    }

    const SwString tag = queueMethod.left(sep);
    for (char c : tag.toStdString()) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return requestedMethod == queueMethod.mid(sep + 1);
}

static bool findRpcRequestQueueForMethod(const SwString& domain,
                                         const SwString& object,
                                         const SwString& requestedMethod,
                                         RpcQueueInfo& out,
                                         SwString& queueMethodOut) {
    const SwString exactSignal = SwString("__rpc__|") + requestedMethod;
    if (findSignalInRegistryForTarget(domain, object, exactSignal, out)) {
        queueMethodOut = requestedMethod;
        return true;
    }

    const SwString prefix("__rpc__|");
    SwJsonArray all = sw::ipc::shmRegistrySnapshot(domain);
    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["object"].toString()) != object) continue;

        const SwString signal = SwString(o["signal"].toString());
        if (!signal.startsWith(prefix)) continue;

        const SwString queueMethod = signal.mid(static_cast<int>(prefix.size()));
        if (!methodMatchesQueueMethod(requestedMethod, queueMethod)) continue;

        out.domain = domain; out.object = object;
        out.signal = signal;
        out.shmName = SwString(o["shmName"].toString());
        out.typeName = SwString(o["typeName"].toString());
        out.typeId = parseHexU64(o["typeId"].toString().toStdString());
        queueMethodOut = queueMethod;
        return true;
    }

    return false;
}

#if defined(_WIN32)
struct WinHandle {
    HANDLE h{NULL};
    WinHandle() = default;
    explicit WinHandle(HANDLE hh) : h(hh) {}
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    WinHandle(WinHandle&& o) noexcept : h(o.h) { o.h = NULL; }
    WinHandle& operator=(WinHandle&& o) noexcept {
        if (this == &o) return *this;
        reset();
        h = o.h;
        o.h = NULL;
        return *this;
    }
    ~WinHandle() { reset(); }
    void reset() {
        if (h) {
            ::CloseHandle(h);
            h = NULL;
        }
    }
    explicit operator bool() const { return h != NULL; }
};
#endif

struct RpcQueueAccess {
    static const size_t kMaxPayload = 4096;

    template <size_t Capacity>
    using LayoutT = sw::ipc::ShmQueueLayout<kMaxPayload, Capacity>;

    template <size_t Capacity>
    using MappingT = sw::ipc::ShmMappingT<LayoutT<Capacity>>;

    std::shared_ptr<void> map;
    SwString domain;
    SwString object;
    SwString signal;
    SwString shmName;
    uint64_t typeId{0};
    size_t capacity{0};
#if defined(_WIN32)
    WinHandle mtx;
    WinHandle evt;
#endif
};

static bool isSupportedRpcQueueCapacity(uint32_t capacity) {
    return capacity == 10u || capacity == 25u || capacity == 50u || capacity == 100u ||
           capacity == 200u || capacity == 500u || capacity == 1000u;
}

static uint32_t rpcQueueCapacityFromQueueMethod(const SwString& queueMethod) {
    const int sep = queueMethod.indexOf("|");
    if (sep <= 0) {
        return 10u;
    }

    const SwString tag = queueMethod.left(sep);
    for (char c : tag.toStdString()) {
        if (c < '0' || c > '9') {
            return 10u;
        }
    }

    const int parsed = tag.toInt();
    const uint32_t capacity = parsed > 0 ? static_cast<uint32_t>(parsed) : 0u;
    return isSupportedRpcQueueCapacity(capacity) ? capacity : 10u;
}

template <size_t Capacity>
static bool openRpcQueueAccessCap(const RpcQueueInfo& info, RpcQueueAccess& out, SwString& err) {
    if (info.shmName.isEmpty() || info.typeId == 0) {
        err = "rpc: missing shmName/typeId in registry";
        return false;
    }

    try {
        out.map = RpcQueueAccess::MappingT<Capacity>::openOrCreate(info.shmName, info.typeId);
        out.domain = info.domain; out.object = info.object; out.signal = info.signal;
        out.shmName = info.shmName;
        out.typeId = info.typeId;
        out.capacity = Capacity;
    } catch (const std::exception& e) {
        err = SwString("rpc: open mapping failed: ") + e.what();
        return false;
    } catch (...) {
        err = "rpc: open mapping failed";
        return false;
    }

#if defined(_WIN32)
    // Open existing sync objects created by the queue owner.
    const std::string base = info.shmName.toStdString();
    out.mtx = WinHandle(::OpenMutexA(SYNCHRONIZE, FALSE, (base + "_mtx").c_str()));
    out.evt = WinHandle(::OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (base + "_evt").c_str()));
#endif

    return true;
}

static bool openRpcQueueAccess(const RpcQueueInfo& info,
                              uint32_t capacity,
                              RpcQueueAccess& out,
                              SwString& err) {
    switch (capacity) {
        case 10u:  return openRpcQueueAccessCap<10>(info, out, err);
        case 25u:  return openRpcQueueAccessCap<25>(info, out, err);
        case 50u:  return openRpcQueueAccessCap<50>(info, out, err);
        case 100u: return openRpcQueueAccessCap<100>(info, out, err);
        case 200u: return openRpcQueueAccessCap<200>(info, out, err);
        case 500u: return openRpcQueueAccessCap<500>(info, out, err);
        case 1000u: return openRpcQueueAccessCap<1000>(info, out, err);
        default:
            err = "rpc: unsupported queue capacity";
            return false;
    }
}

template <size_t Capacity>
static bool rpcQueuePushRawCap(RpcQueueAccess& q, const uint8_t* data, size_t size, SwString& err) {
    if (!q.map) {
        err = "rpc: queue not open";
        return false;
    }
    if (size > RpcQueueAccess::kMaxPayload) {
        err = "rpc: payload too large";
        return false;
    }

    typedef typename RpcQueueAccess::template MappingT<Capacity> Mapping;
    typedef typename RpcQueueAccess::template LayoutT<Capacity> Layout;

    Mapping* mapping = static_cast<Mapping*>(q.map.get());
    if (!mapping) {
        err = "rpc: queue mapping invalid";
        return false;
    }

    Layout* L = mapping->layout();
    bool ok = false;

#if defined(_WIN32)
    if (q.mtx) {
        ::WaitForSingleObject(q.mtx.h, INFINITE);
    }
    const uint64_t inFlight = (L->seq >= L->readSeq) ? (L->seq - L->readSeq) : 0;
    if (inFlight < Capacity) {
        const uint64_t next = L->seq + 1;
        typename Layout::Slot& slot = L->entries[next % Capacity];
        slot.seq = next;
        slot.size = static_cast<uint32_t>(size);
        if (slot.size <= RpcQueueAccess::kMaxPayload) {
            if (size != 0) std::memcpy(slot.data, data, size);
            L->seq = next;
            ok = true;
        }
    }
    if (q.mtx) {
        ::ReleaseMutex(q.mtx.h);
    }
    if (ok && q.evt) {
        ::SetEvent(q.evt.h);
    }
#else
    pthread_mutex_lock(&L->mtx);
    const uint64_t inFlight = (L->seq >= L->readSeq) ? (L->seq - L->readSeq) : 0;
    if (inFlight < Capacity) {
        const uint64_t next = L->seq + 1;
        typename Layout::Slot& slot = L->entries[next % Capacity];
        slot.seq = next;
        slot.size = static_cast<uint32_t>(size);
        if (slot.size <= RpcQueueAccess::kMaxPayload) {
            if (size != 0) std::memcpy(slot.data, data, size);
            L->seq = next;
            ok = true;
        }
    }
    pthread_mutex_unlock(&L->mtx);
    if (ok) pthread_cond_broadcast(&L->cv);
#endif

    if (ok) {
        std::vector<uint32_t> subscribers;
        sw::ipc::detail::SubscribersRegistryTable<>::listSubscriberPids(q.domain, q.object, q.signal, subscribers);
        for (uint32_t pid : subscribers) sw::ipc::detail::LoopPoller::notifyProcess(pid);
    }
    if (!ok) err = "rpc: queue full";
    return ok;
}

static bool rpcQueuePushRaw(RpcQueueAccess& q, const uint8_t* data, size_t size, SwString& err) {
    switch (q.capacity) {
        case 10u:  return rpcQueuePushRawCap<10>(q, data, size, err);
        case 25u:  return rpcQueuePushRawCap<25>(q, data, size, err);
        case 50u:  return rpcQueuePushRawCap<50>(q, data, size, err);
        case 100u: return rpcQueuePushRawCap<100>(q, data, size, err);
        case 200u: return rpcQueuePushRawCap<200>(q, data, size, err);
        case 500u: return rpcQueuePushRawCap<500>(q, data, size, err);
        case 1000u: return rpcQueuePushRawCap<1000>(q, data, size, err);
        default:
            err = "rpc: unsupported queue capacity";
            return false;
    }
}

template <size_t Capacity>
static bool rpcQueuePopOneRawCap(RpcQueueAccess& q, std::vector<uint8_t>& out) {
    out.clear();
    if (!q.map) return false;

    typedef typename RpcQueueAccess::template MappingT<Capacity> Mapping;
    typedef typename RpcQueueAccess::template LayoutT<Capacity> Layout;

    Mapping* mapping = static_cast<Mapping*>(q.map.get());
    if (!mapping) return false;

    Layout* L = mapping->layout();
    bool have = false;

#if defined(_WIN32)
    if (q.mtx) {
        ::WaitForSingleObject(q.mtx.h, INFINITE);
    }
    const uint64_t readSeq = L->readSeq;
    if (readSeq < L->seq) {
        const uint64_t next = readSeq + 1;
        typename Layout::Slot& slot = L->entries[next % Capacity];
        const uint32_t sz = slot.size;
        if (slot.seq == next && sz <= RpcQueueAccess::kMaxPayload) {
            out.assign(slot.data, slot.data + sz);
            have = true;
        }
        L->readSeq = next;
    }
    if (q.mtx) {
        ::ReleaseMutex(q.mtx.h);
    }
#else
    pthread_mutex_lock(&L->mtx);
    const uint64_t readSeq = L->readSeq;
    if (readSeq < L->seq) {
        const uint64_t next = readSeq + 1;
        typename Layout::Slot& slot = L->entries[next % Capacity];
        const uint32_t sz = slot.size;
        if (slot.seq == next && sz <= RpcQueueAccess::kMaxPayload) {
            out.assign(slot.data, slot.data + sz);
            have = true;
        }
        L->readSeq = next;
    }
    pthread_mutex_unlock(&L->mtx);
#endif

    return have;
}

static bool rpcQueuePopOneRaw(RpcQueueAccess& q, std::vector<uint8_t>& out) {
    switch (q.capacity) {
        case 10u:  return rpcQueuePopOneRawCap<10>(q, out);
        case 25u:  return rpcQueuePopOneRawCap<25>(q, out);
        case 50u:  return rpcQueuePopOneRawCap<50>(q, out);
        case 100u: return rpcQueuePopOneRawCap<100>(q, out);
        case 200u: return rpcQueuePopOneRawCap<200>(q, out);
        case 500u: return rpcQueuePopOneRawCap<500>(q, out);
        case 1000u: return rpcQueuePopOneRawCap<1000>(q, out);
        default:
            out.clear();
            return false;
    }
}

static bool encodeJsonArg(sw::ipc::detail::Encoder& enc, const std::string& type, const SwJsonValue& v, SwString& err) {
    return sw::ipc::jsonwire::encode(enc, type, v, err);
}

static bool decodeJsonValueByType(sw::ipc::detail::Decoder& dec, const std::string& type, SwJsonValue& out, SwString& err) {
    return sw::ipc::jsonwire::decode(dec, type, out, err);
}

}}
