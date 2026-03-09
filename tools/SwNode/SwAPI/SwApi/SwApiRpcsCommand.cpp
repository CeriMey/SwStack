#include "SwApiRpcsCommand.h"

#include "SwApiJson.h"

#include "SwSharedMemorySignal.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>

namespace {

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

        out.signal = signalName;
        out.shmName = SwString(o["shmName"].toString());
        out.typeName = SwString(o["typeName"].toString());
        out.typeId = parseHexU64(SwString(o["typeId"].toString()).toStdString());
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
    static const size_t kCapacity = 10;
    static const size_t kMaxPayload = 4096;
    typedef sw::ipc::ShmQueueLayout<kMaxPayload, kCapacity> Layout;
    typedef sw::ipc::ShmMappingT<Layout> Mapping;

    std::shared_ptr<Mapping> map;
    SwString shmName;
    uint64_t typeId{0};
#if defined(_WIN32)
    WinHandle mtx;
    WinHandle evt;
#endif
};

static bool openRpcQueueAccess(const RpcQueueInfo& info, RpcQueueAccess& out, SwString& err) {
    if (info.shmName.isEmpty() || info.typeId == 0) {
        err = "rpc: missing shmName/typeId in registry";
        return false;
    }

    try {
        out.map = RpcQueueAccess::Mapping::openOrCreate(info.shmName, info.typeId);
        out.shmName = info.shmName;
        out.typeId = info.typeId;
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

static bool rpcQueuePushRaw(RpcQueueAccess& q, const uint8_t* data, size_t size, SwString& err) {
    if (!q.map) {
        err = "rpc: queue not open";
        return false;
    }
    if (size > RpcQueueAccess::kMaxPayload) {
        err = "rpc: payload too large";
        return false;
    }

    RpcQueueAccess::Layout* L = q.map->layout();
    bool ok = false;

#if defined(_WIN32)
    if (q.mtx) {
        ::WaitForSingleObject(q.mtx.h, INFINITE);
    }
    const uint64_t inFlight = (L->seq >= L->readSeq) ? (L->seq - L->readSeq) : 0;
    if (inFlight < RpcQueueAccess::kCapacity) {
        const uint64_t next = L->seq + 1;
        RpcQueueAccess::Layout::Slot& slot = L->entries[next % RpcQueueAccess::kCapacity];
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
    if (inFlight < RpcQueueAccess::kCapacity) {
        const uint64_t next = L->seq + 1;
        RpcQueueAccess::Layout::Slot& slot = L->entries[next % RpcQueueAccess::kCapacity];
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

    if (!ok) err = "rpc: queue full";
    return ok;
}

static bool rpcQueuePopOneRaw(RpcQueueAccess& q, std::vector<uint8_t>& out) {
    out.clear();
    if (!q.map) return false;

    RpcQueueAccess::Layout* L = q.map->layout();
    bool have = false;

#if defined(_WIN32)
    if (q.mtx) {
        ::WaitForSingleObject(q.mtx.h, INFINITE);
    }
    const uint64_t readSeq = L->readSeq;
    if (readSeq < L->seq) {
        const uint64_t next = readSeq + 1;
        RpcQueueAccess::Layout::Slot& slot = L->entries[next % RpcQueueAccess::kCapacity];
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
        RpcQueueAccess::Layout::Slot& slot = L->entries[next % RpcQueueAccess::kCapacity];
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

static bool encodeJsonArg(sw::ipc::detail::Encoder& enc, const std::string& type, const SwJsonValue& v, SwString& err) {
    auto asBool = [](const SwJsonValue& v, bool& out) -> bool {
        if (v.isBool()) { out = v.toBool(); return true; }
        if (v.isInt()) { out = (v.toInt() != 0); return true; }
        if (v.isString()) {
            const std::string s = SwString(v.toString()).toStdString();
            out = (s == "1" || s == "true" || s == "TRUE" || s == "True");
            return true;
        }
        return false;
    };
    auto asInt = [](const SwJsonValue& v, int& out) -> bool {
        if (v.isInt()) { out = v.toInt(); return true; }
        if (v.isDouble()) { out = static_cast<int>(v.toDouble()); return true; }
        if (v.isString()) { out = std::atoi(SwString(v.toString()).toStdString().c_str()); return true; }
        if (v.isBool()) { out = v.toBool() ? 1 : 0; return true; }
        return false;
    };
    auto asU32 = [](const SwJsonValue& v, uint32_t& out) -> bool {
        if (v.isInt()) { out = static_cast<uint32_t>(v.toInt()); return true; }
        if (v.isDouble()) { out = static_cast<uint32_t>(v.toDouble()); return true; }
        if (v.isString()) {
            const std::string s = SwString(v.toString()).toStdString();
            out = static_cast<uint32_t>(std::strtoul(s.c_str(), NULL, 10));
            return true;
        }
        if (v.isBool()) { out = v.toBool() ? 1u : 0u; return true; }
        return false;
    };
    auto asU64 = [](const SwJsonValue& v, uint64_t& out) -> bool {
        if (v.isInt()) { out = static_cast<uint64_t>(v.toInt()); return true; }
        if (v.isDouble()) { out = static_cast<uint64_t>(v.toDouble()); return true; }
        if (v.isString()) {
            const std::string s = SwString(v.toString()).toStdString();
            out = static_cast<uint64_t>(std::strtoull(s.c_str(), NULL, 10));
            return true;
        }
        if (v.isBool()) { out = v.toBool() ? 1ull : 0ull; return true; }
        return false;
    };
    auto asDouble = [](const SwJsonValue& v, double& out) -> bool {
        if (v.isDouble()) { out = v.toDouble(); return true; }
        if (v.isInt()) { out = static_cast<double>(v.toInt()); return true; }
        if (v.isString()) { out = std::atof(SwString(v.toString()).toStdString().c_str()); return true; }
        if (v.isBool()) { out = v.toBool() ? 1.0 : 0.0; return true; }
        return false;
    };
    auto asString = [](const SwJsonValue& v, SwString& out) -> bool {
        if (v.isString()) { out = SwString(v.toString()); return true; }
        if (v.isBool()) { out = v.toBool() ? "true" : "false"; return true; }
        if (v.isInt()) { out = SwString(std::to_string(v.toInt())); return true; }
        if (v.isDouble()) { std::ostringstream oss; oss << v.toDouble(); out = SwString(oss.str()); return true; }
        return false;
    };
    auto asBytes = [](const SwJsonValue& v, SwByteArray& out) -> bool {
        if (v.isString()) { out = SwByteArray(SwString(v.toString()).toStdString()); return true; }
        return false;
    };

    if (isBoolType(type)) {
        bool x = false;
        if (!asBool(v, x)) { err = "rpc: arg parse failed (bool)"; return false; }
        return sw::ipc::detail::Codec<bool>::write(enc, x);
    }
    if (isIntType(type)) {
        int x = 0;
        if (!asInt(v, x)) { err = "rpc: arg parse failed (int)"; return false; }
        return sw::ipc::detail::Codec<int>::write(enc, x);
    }
    if (isU32Type(type)) {
        uint32_t x = 0;
        if (!asU32(v, x)) { err = "rpc: arg parse failed (u32)"; return false; }
        return sw::ipc::detail::Codec<uint32_t>::write(enc, x);
    }
    if (isU64Type(type)) {
        uint64_t x = 0;
        if (!asU64(v, x)) { err = "rpc: arg parse failed (u64)"; return false; }
        return sw::ipc::detail::Codec<uint64_t>::write(enc, x);
    }
    if (isFloatType(type)) {
        double x = 0.0;
        if (!asDouble(v, x)) { err = "rpc: arg parse failed (double)"; return false; }
        return sw::ipc::detail::Codec<double>::write(enc, x);
    }
    if (isStringType(type)) {
        SwString x;
        if (!asString(v, x)) { err = "rpc: arg parse failed (SwString)"; return false; }
        return sw::ipc::detail::Codec<SwString>::write(enc, x);
    }
    if (isBytesType(type)) {
        SwByteArray x;
        if (!asBytes(v, x)) { err = "rpc: arg parse failed (SwByteArray)"; return false; }
        return sw::ipc::detail::Codec<SwByteArray>::write(enc, x);
    }

    err = SwString("rpc: unsupported arg type: ") + type;
    return false;
}

static bool decodeJsonValueByType(sw::ipc::detail::Decoder& dec, const std::string& type, SwJsonValue& out, SwString& err) {
    if (isBoolType(type)) {
        bool x = false;
        if (!sw::ipc::detail::Codec<bool>::read(dec, x)) { err = "rpc: decode failed (bool)"; return false; }
        out = SwJsonValue(x);
        return true;
    }
    if (isIntType(type)) {
        int x = 0;
        if (!sw::ipc::detail::Codec<int>::read(dec, x)) { err = "rpc: decode failed (int)"; return false; }
        out = SwJsonValue(x);
        return true;
    }
    if (isU32Type(type)) {
        uint32_t x = 0;
        if (!sw::ipc::detail::Codec<uint32_t>::read(dec, x)) { err = "rpc: decode failed (u32)"; return false; }
        out = SwJsonValue(static_cast<int>(x));
        return true;
    }
    if (isU64Type(type)) {
        uint64_t x = 0;
        if (!sw::ipc::detail::Codec<uint64_t>::read(dec, x)) { err = "rpc: decode failed (u64)"; return false; }
        out = SwJsonValue(std::to_string(x));
        return true;
    }
    if (isFloatType(type)) {
        double x = 0.0;
        if (!sw::ipc::detail::Codec<double>::read(dec, x)) { err = "rpc: decode failed (double)"; return false; }
        out = SwJsonValue(x);
        return true;
    }
    if (isStringType(type)) {
        SwString x;
        if (!sw::ipc::detail::Codec<SwString>::read(dec, x)) { err = "rpc: decode failed (SwString)"; return false; }
        out = SwJsonValue(x);
        return true;
    }
    if (isBytesType(type)) {
        SwByteArray x;
        if (!sw::ipc::detail::Codec<SwByteArray>::read(dec, x)) { err = "rpc: decode failed (SwByteArray)"; return false; }
        out = SwJsonValue(SwString(std::string(x.constData(), x.size())));
        return true;
    }

    err = SwString("rpc: unsupported return type: ") + type;
    return false;
}

static bool parseTokenByType(const std::string& type, const SwString& tok, SwJsonValue& out, SwString& err) {
    err.clear();
    if (isBoolType(type)) {
        const SwString s = tok.trimmed().toLower();
        if (s == "1" || s == "true" || s == "yes" || s == "on") { out = SwJsonValue(true); return true; }
        if (s == "0" || s == "false" || s == "no" || s == "off") { out = SwJsonValue(false); return true; }
        err = "rpc: arg parse failed (bool)";
        return false;
    }
    if (isIntType(type) || isU32Type(type)) {
        bool ok = false;
        const int x = tok.trimmed().toInt(&ok);
        if (!ok) { err = "rpc: arg parse failed (int)"; return false; }
        out = SwJsonValue(x);
        return true;
    }
    if (isU64Type(type)) {
        const std::string s = tok.trimmed().toStdString();
        if (s.empty()) { err = "rpc: arg parse failed (u64)"; return false; }
        char* end = NULL;
        (void)std::strtoull(s.c_str(), &end, 10);
        if (!end || *end != '\0') { err = "rpc: arg parse failed (u64)"; return false; }
        out = SwJsonValue(s);
        return true;
    }
    if (isFloatType(type)) {
        bool ok = false;
        const double x = tok.trimmed().toDouble(&ok);
        if (!ok) { err = "rpc: arg parse failed (double)"; return false; }
        out = SwJsonValue(x);
        return true;
    }
    if (isStringType(type) || isBytesType(type)) {
        out = SwJsonValue(tok.toStdString());
        return true;
    }

    err = SwString("rpc: unsupported arg type: ") + type;
    return false;
}

} // namespace

SwApiRpcsCommand::SwApiRpcsCommand(const SwApiCli& cli,
                                   SwApiIpcInspector& inspector,
                                   const SwStringList& args,
                                   SwObject* parent)
    : SwApiCommand(cli, inspector, args, parent) {}

SwApiRpcsCommand::~SwApiRpcsCommand() = default;

void SwApiRpcsCommand::printUsage_() const {
    std::cerr
        << "Usage:\n"
        << "  swapi rpc list <target> [--domain <sys>] [--json] [--pretty]\n"
        << "  swapi rpc call <target> <method> [args...] [--domain <sys>] [--args <json-array>] [--timeout_ms <ms>] [--clientInfo <s>] [--json] [--pretty]\n";
}

int SwApiRpcsCommand::cmdList_() {
    const bool json = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 2) {
        std::cerr << "swapi rpc list: missing <target>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi rpc list: " << err.toStdString() << "\n";
        return 2;
    }

    SwJsonArray rpcs = inspector().rpcsForTarget(target, /*includeStale=*/false);
    if (json) {
        std::cout << SwApiJson::toJson(rpcs, pretty).toStdString() << "\n";
        return 0;
    }

    for (size_t i = 0; i < rpcs.size(); ++i) {
        const SwJsonValue v = rpcs[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        std::cout << SwString(o["method"].toString()).toStdString() << "\n";
    }
    return 0;
}

int SwApiRpcsCommand::cmdCall_() {
    static std::atomic<uint64_t> s_callId{1};

    const bool json = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 3) {
        std::cerr << "swapi rpc call: missing <target> <method>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi rpc call: " << err.toStdString() << "\n";
        return 2;
    }

    const SwString method = args()[2];
    if (method.isEmpty()) {
        std::cerr << "swapi rpc call: empty <method>\n";
        return 2;
    }

    int timeoutMs = cli().intValue("timeout_ms", 2000);
    if (timeoutMs <= 0) timeoutMs = 2000;

    const SwString clientInfo = cli().value("clientInfo", SwString("swapi"));

    const SwString reqSignal = SwString("__rpc__|") + method;
    RpcQueueInfo reqInfo;
    if (!findSignalInRegistryForTarget(target.domain, target.object, reqSignal, reqInfo)) {
        std::cerr << "swapi rpc call: rpc request queue not found in registry (__rpc__|method)\n";
        return 3;
    }

    std::vector<std::string> reqTypes = parseArgTypesFromTypeName(reqInfo.typeName.toStdString());
    if (reqTypes.size() < 3) {
        std::cerr << "swapi rpc call: invalid request typeName (expected <callId,pid,clientInfo,...>)\n";
        return 3;
    }
    reqTypes.erase(reqTypes.begin(), reqTypes.begin() + 3);

    SwJsonArray argsArr;
    const SwString argsJson = cli().value("args", SwString());
    if (!argsJson.isEmpty()) {
        if (!SwApiJson::parseArray(argsJson, argsArr, err)) {
            std::cerr << "swapi rpc call: invalid --args JSON array: " << err.toStdString() << "\n";
            return 2;
        }
    } else {
        // args()[0]=call args()[1]=target args()[2]=method args()[3...]=args
        for (size_t i = 3; i < args().size(); ++i) {
            if (i - 3 >= reqTypes.size()) break;
            SwJsonValue v;
            SwString perr;
            if (!parseTokenByType(reqTypes[i - 3], args()[i], v, perr)) {
                std::cerr << "swapi rpc call: " << perr.toStdString() << "\n";
                return 2;
            }
            argsArr.append(v);
        }
    }

    if (argsArr.size() != reqTypes.size()) {
        std::cerr << "swapi rpc call: args count mismatch vs registry signature\n";
        return 2;
    }

    const uint64_t callId = s_callId.fetch_add(1, std::memory_order_relaxed);
    const uint32_t pid = sw::ipc::detail::currentPid();

    std::array<uint8_t, RpcQueueAccess::kMaxPayload> tmp;
    sw::ipc::detail::Encoder enc(tmp.data(), tmp.size());
    if (!sw::ipc::detail::Codec<uint64_t>::write(enc, callId) || !sw::ipc::detail::Codec<uint32_t>::write(enc, pid) ||
        !sw::ipc::detail::Codec<SwString>::write(enc, clientInfo)) {
        std::cerr << "swapi rpc call: encode header failed\n";
        return 3;
    }

    for (size_t i = 0; i < reqTypes.size(); ++i) {
        SwString perr;
        if (!encodeJsonArg(enc, reqTypes[i], argsArr[i], perr)) {
            std::cerr << "swapi rpc call: " << perr.toStdString() << "\n";
            return 2;
        }
    }

    RpcQueueAccess reqQ;
    SwString qErr;
    if (!openRpcQueueAccess(reqInfo, reqQ, qErr)) {
        std::cerr << "swapi rpc call: " << qErr.toStdString() << "\n";
        return 3;
    }
    if (!rpcQueuePushRaw(reqQ, tmp.data(), enc.size(), qErr)) {
        std::cerr << "swapi rpc call: " << qErr.toStdString() << "\n";
        return 3;
    }

    const SwString respSignal = SwString("__rpc_ret__|") + method + "|" + SwString(std::to_string(pid));

    RpcQueueAccess respQ;
    bool haveRespQ = false;
    bool hasRet = false;
    SwString retType;

    bool done = false;
    bool ok = false;
    SwString rpcErr;
    SwJsonValue result;

    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline = t0 + std::chrono::milliseconds(timeoutMs);

    while (!done) {
        if (!haveRespQ) {
            RpcQueueInfo respInfo;
            if (findSignalInRegistryForTarget(target.domain, target.object, respSignal, respInfo)) {
                SwString openErr;
                if (!openRpcQueueAccess(respInfo, respQ, openErr)) {
                    std::cerr << "swapi rpc call: " << openErr.toStdString() << "\n";
                    return 3;
                }
                std::vector<std::string> respTypes = parseArgTypesFromTypeName(respInfo.typeName.toStdString());
                if (respTypes.size() == 4) {
                    hasRet = true;
                    retType = SwString(respTypes[3]);
                } else {
                    hasRet = false;
                    retType.clear();
                }
                haveRespQ = true;
            }
        }

        if (haveRespQ) {
            std::vector<uint8_t> msg;
            while (rpcQueuePopOneRaw(respQ, msg)) {
                sw::ipc::detail::Decoder dec(msg.data(), msg.size());
                uint64_t gotCallId = 0;
                bool gotOk = false;
                SwString gotErr;
                if (!sw::ipc::detail::Codec<uint64_t>::read(dec, gotCallId) || !sw::ipc::detail::Codec<bool>::read(dec, gotOk) ||
                    !sw::ipc::detail::Codec<SwString>::read(dec, gotErr)) {
                    continue;
                }
                if (gotCallId != callId) continue;

                ok = gotOk;
                rpcErr = gotErr;

                if (ok && hasRet) {
                    SwString derr;
                    if (!decodeJsonValueByType(dec, retType.toStdString(), result, derr)) {
                        std::cerr << "swapi rpc call: " << derr.toStdString() << "\n";
                        return 3;
                    }
                }
                done = true;
                break;
            }
        }

        if (done) break;
        if (std::chrono::steady_clock::now() >= deadline) break;

#if defined(_WIN32)
        if (haveRespQ && respQ.evt) {
            const auto now = std::chrono::steady_clock::now();
            const auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            const DWORD waitMs = rem > 50 ? 50 : static_cast<DWORD>((rem > 0) ? rem : 0);
            (void)::WaitForSingleObject(respQ.evt.h, waitMs);
        } else {
            ::Sleep(1);
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }

    if (!done) {
        if (json) {
            SwJsonObject o;
            o["ok"] = SwJsonValue(false);
            o["method"] = SwJsonValue(method.toStdString());
            o["callId"] = SwJsonValue(std::to_string(callId));
            o["error"] = SwJsonValue("rpc: timeout");
            std::cout << SwApiJson::toJson(o, pretty).toStdString() << "\n";
        } else {
            std::cerr << "rpc: timeout\n";
        }
        return 3;
    }

    if (json) {
        SwJsonObject o;
        o["ok"] = SwJsonValue(ok);
        o["method"] = SwJsonValue(method.toStdString());
        o["callId"] = SwJsonValue(std::to_string(callId));
        o["returnType"] = SwJsonValue(retType.toStdString());
        if (!ok) {
            o["error"] = SwJsonValue(rpcErr.toStdString());
        } else if (hasRet) {
            o["result"] = result;
        }
        std::cout << SwApiJson::toJson(o, pretty).toStdString() << "\n";
    } else {
        if (!ok) {
            std::cerr << rpcErr.toStdString() << "\n";
        } else if (hasRet) {
            std::cout << result.toJsonString() << "\n";
        } else {
            std::cout << "ok\n";
        }
    }

    return ok ? 0 : 3;
}

void SwApiRpcsCommand::start() {
    const SwStringList& a = args();
    const SwString sub = a.isEmpty() ? SwString("list") : a[0];
    int code = 2;
    if (sub == "list") code = cmdList_();
    else if (sub == "call") code = cmdCall_();
    else {
        printUsage_();
        finish(2);
        return;
    }
    finish(code);
}
