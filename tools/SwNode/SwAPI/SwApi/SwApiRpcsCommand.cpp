#include "../SwApiRpcWire.h"
#include "SwIpcJsonCodec.h"
#include "SwApiRpcsCommand.h"

#include "SwApiJson.h"

#include "SwSharedMemorySignal.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>

namespace {

using namespace swapi::wire;

static bool parseTokenByType(const std::string& type, const SwString& tok, SwJsonValue& out, SwString& err) {
    err.clear();
    if (isBoolType(type)) {
        const SwString s = tok.trimmed().toLower();
        if (s == "1" || s == "true" || s == "yes" || s == "on") { out = SwJsonValue(true); return true; }
        if (s == "0" || s == "false" || s == "no" || s == "off") { out = SwJsonValue(false); return true; }
        err = "rpc: arg parse failed (bool)";
        return false;
    }
    if (isIntType(type) || isU32Type(type) || isU64Type(type) ||
        type == "int64_t" || type == "int8_t" || type == "uint8_t" || type == "int16_t" || type == "uint16_t") {
        out = SwJsonValue(tok.trimmed());
        return true; // strict width/range validation is done by the shared codec
    }
    if (isFloatType(type)) {
        bool ok = false;
        const double x = tok.trimmed().toDouble(&ok);
        if (!ok) { err = "rpc: arg parse failed (double)"; return false; }
        out = SwJsonValue(x);
        return true;
    }
    if (isStringType(type) || isBytesType(type)) {
        out = SwJsonValue(tok);
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
        std::cout << o["method"].toString().toStdString() << "\n";
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

    const SwString requestedMethod = args()[2];
    if (requestedMethod.isEmpty()) {
        std::cerr << "swapi rpc call: empty <method>\n";
        return 2;
    }

    int timeoutMs = cli().intValue("timeout_ms", 2000);
    if (timeoutMs <= 0) timeoutMs = 2000;

    const SwString clientInfo = cli().value("clientInfo", SwString("swapi"));

    RpcQueueInfo reqInfo;
    SwString method;
    if (!findRpcRequestQueueForMethod(target.domain, target.object, requestedMethod, reqInfo, method)) {
        std::cerr << "swapi rpc call: rpc request queue not found in registry (__rpc__|method)\n";
        return 3;
    }
    const uint32_t queueCapacity = rpcQueueCapacityFromQueueMethod(method);

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
    if (!openRpcQueueAccess(reqInfo, queueCapacity, reqQ, qErr)) {
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
                if (!openRpcQueueAccess(respInfo, queueCapacity, respQ, openErr)) {
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
            o["method"] = SwJsonValue(method);
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
        o["method"] = SwJsonValue(method);
        o["callId"] = SwJsonValue(std::to_string(callId));
        o["returnType"] = SwJsonValue(retType);
        if (!ok) {
            o["error"] = SwJsonValue(rpcErr);
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
