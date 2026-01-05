#pragma once
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

#include "SwIpcRpc.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace sw {
namespace ipc {

struct RpcRequestSpec {
    SwString method;
    uint64_t requestTypeId{0};
};

template <typename... Args>
inline uint64_t rpcRequestTypeId() {
    return detail::type_id<uint64_t, uint32_t, SwString, typename std::decay<Args>::type...>();
}

template <typename... Args>
inline RpcRequestSpec rpcRequestSpec(const SwString& methodName) {
    RpcRequestSpec spec;
    spec.method = methodName;
    spec.requestTypeId = rpcRequestTypeId<Args...>();
    return spec;
}

// Best-effort discovery: return all "<domain>/<object>" that expose every required RPC request queue.
// Matching is done against the per-domain registry entries ("__rpc__|<method>") and optionally the typeId.
inline SwStringList discoverRpcTargets(const SwString& domain,
                                       const SwList<RpcRequestSpec>& required,
                                       bool requireTypeMatch = true) {
    SwStringList out;
    if (domain.isEmpty()) return out;

    std::map<std::string, std::string> expectedTypeIdHexByMethod;
    if (!required.isEmpty()) {
        for (size_t i = 0; i < required.size(); ++i) {
            const std::string method = required[i].method.toStdString();
            expectedTypeIdHexByMethod[method] = detail::hex64(required[i].requestTypeId).toStdString();
        }
    }

    std::map<std::string, std::map<std::string, std::string>> foundByObject; // object -> method -> typeIdHex
    SwJsonArray all = shmRegistrySnapshot(domain);

    static const std::string kRpcPrefix("__rpc__|");
    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject() || !v.toObject()) continue;
        const SwJsonObject o(*v.toObject());

        const std::string object = SwString(o["object"].toString()).toStdString();
        const std::string signal = SwString(o["signal"].toString()).toStdString();
        if (signal.size() < kRpcPrefix.size()) continue;
        if (signal.compare(0, kRpcPrefix.size(), kRpcPrefix) != 0) continue;

        const std::string method = signal.substr(kRpcPrefix.size());
        if (!expectedTypeIdHexByMethod.empty() && expectedTypeIdHexByMethod.find(method) == expectedTypeIdHexByMethod.end()) {
            continue;
        }

        const std::string typeIdHex = SwString(o["typeId"].toString()).toStdString();
        foundByObject[object][method] = typeIdHex;
    }

    std::vector<std::string> results;
    results.reserve(foundByObject.size());

    const std::string domainStd = domain.toStdString();
    for (auto itObj = foundByObject.begin(); itObj != foundByObject.end(); ++itObj) {
        const std::string& object = itObj->first;
        const std::map<std::string, std::string>& methods = itObj->second;

        bool ok = true;
        if (!expectedTypeIdHexByMethod.empty()) {
            for (auto itReq = expectedTypeIdHexByMethod.begin(); itReq != expectedTypeIdHexByMethod.end(); ++itReq) {
                const std::string& reqMethod = itReq->first;
                const std::string& reqTypeHex = itReq->second;

                auto itHave = methods.find(reqMethod);
                if (itHave == methods.end()) {
                    ok = false;
                    break;
                }
                if (requireTypeMatch && itHave->second != reqTypeHex) {
                    ok = false;
                    break;
                }
            }
        }

        if (ok) {
            results.push_back(domainStd + "/" + object);
        }
    }

    std::sort(results.begin(), results.end());
    out.reserve(results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        out.append(SwString(results[i]));
    }
    return out;
}

// Lightweight base class for "proxy objects" (client-side RPC wrappers).
// Use the SW_PROXY_OBJECT_* macros below to generate typed RPC methods.
class ProxyObjectBase {
public:
    ProxyObjectBase(const SwString& domain, const SwString& object, const SwString& clientInfo = SwString())
        : domain_(domain), object_(object), clientInfo_(clientInfo) {}

    const SwString& domain() const { return domain_; }
    const SwString& object() const { return object_; }
    const SwString& clientInfo() const { return clientInfo_; }

    // "<domain>/<object>" (human-readable remote target id).
    SwString target() const { return domain_ + "/" + object_; }

    // Best-effort: does the remote process exposing this object still exist?
    // Uses the registry PID(s) for this object and checks OS-level liveness.
    uint32_t remotePid() const { return remotePid_(); }

    bool isAlive() const {
        const uint32_t pid = remotePid_();
        if (!pid) return false;
        return (detail::pidStateBestEffort_(pid) == detail::PidState::Alive);
    }

    // Runtime introspection: list RPC methods exposed by the remote object (from shm registry).
    SwStringList functions() const {
        SwStringList out;
        if (domain_.isEmpty() || object_.isEmpty()) return out;

        const SwJsonArray all = shmRegistrySnapshot(domain_);
        std::vector<std::string> methods;

        static const std::string kRpcPrefix("__rpc__|");
        for (size_t i = 0; i < all.size(); ++i) {
            const SwJsonValue v = all[i];
            if (!v.isObject() || !v.toObject()) continue;
            const SwJsonObject o(*v.toObject());

            if (SwString(o["object"].toString()) != object_) continue;

            const std::string sig = SwString(o["signal"].toString()).toStdString();
            if (sig.size() < kRpcPrefix.size()) continue;
            if (sig.compare(0, kRpcPrefix.size(), kRpcPrefix) != 0) continue;

            const std::string method = sig.substr(kRpcPrefix.size());
            if (!method.empty()) methods.push_back(method);
        }

        if (methods.empty()) return out;
        std::sort(methods.begin(), methods.end());
        methods.erase(std::unique(methods.begin(), methods.end()), methods.end());

        out.reserve(methods.size());
        for (size_t i = 0; i < methods.size(); ++i) {
            out.append(SwString(methods[i]));
        }
        return out;
    }

    // Runtime introspection: list argument types for a remote RPC method (from shm registry).
    // Returned type names are best-effort (compiler-dependent), e.g. "int", "unsigned __int64", "SwString"...
    SwStringList argType(const SwString& functionName) const {
        SwStringList out;
        if (domain_.isEmpty() || object_.isEmpty()) return out;
        if (functionName.isEmpty()) return out;

        const uint32_t preferPid = remotePid_();
        const std::string wantSig = std::string("__rpc__|") + functionName.toStdString();

        std::string typeName;
        std::string fallbackTypeName;

        const SwJsonArray all = shmRegistrySnapshot(domain_);
        for (size_t i = 0; i < all.size(); ++i) {
            const SwJsonValue v = all[i];
            if (!v.isObject() || !v.toObject()) continue;
            const SwJsonObject o(*v.toObject());

            if (SwString(o["object"].toString()) != object_) continue;
            if (SwString(o["signal"].toString()).toStdString() != wantSig) continue;

            const uint32_t pid = static_cast<uint32_t>(o["pid"].toInt());
            const std::string t = SwString(o["typeName"].toString()).toStdString();
            if (!preferPid || pid == preferPid) {
                typeName = t;
                break;
            }
            if (fallbackTypeName.empty()) fallbackTypeName = t;
        }

        if (typeName.empty()) typeName = fallbackTypeName;
        if (typeName.empty()) return out;

        std::vector<std::string> types = parseArgTypesFromTypeName_(typeName);
        // Request queue is: <uint64_t callId, uint32_t pid, SwString clientInfo, Args...>
        if (types.size() < 3) return out;
        types.erase(types.begin(), types.begin() + 3);

        out.reserve(types.size());
        for (size_t i = 0; i < types.size(); ++i) {
            out.append(SwString(types[i]));
        }
        return out;
    }

private:
    static std::vector<std::string> parseArgTypesFromTypeName_(const std::string& typeName) {
        // type_name() returns compiler-specific strings, e.g.:
        // MSVC: "class SwString __cdecl sw::ipc::detail::type_name<int,class SwString>(void)"
        // GCC/Clang: "... type_name() [with Args = int; Args = SwString]"
        //
        // We implement a best-effort parser for the MSVC-like "<...>" chunk.
        std::vector<std::string> out;
        const size_t lt = typeName.find('<');
        const size_t gt = (lt == std::string::npos) ? std::string::npos : typeName.find('>', lt + 1);
        if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1) return out;
        std::string inside = typeName.substr(lt + 1, gt - lt - 1);

        auto trimInPlace = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s[0]))) s.erase(0, 1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s[s.size() - 1]))) s.pop_back();
        };

        size_t start = 0;
        while (start < inside.size()) {
            size_t comma = inside.find(',', start);
            if (comma == std::string::npos) comma = inside.size();
            std::string token = inside.substr(start, comma - start);
            trimInPlace(token);
            // normalize MSVC tokens like "class SwString"
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

    uint32_t remotePid_() const {
        if (domain_.isEmpty() || object_.isEmpty()) return 0;

        const uint32_t selfPid = detail::currentPid();
        uint32_t cfgPrefer = 0;
        uint32_t rpcPrefer = 0;
        uint32_t anyPrefer = 0;

        const SwJsonArray all = shmRegistrySnapshot(domain_);
        for (size_t i = 0; i < all.size(); ++i) {
            const SwJsonValue v = all[i];
            if (!v.isObject() || !v.toObject()) continue;
            const SwJsonObject o(*v.toObject());
            if (SwString(o["object"].toString()) != object_) continue;

            const uint32_t pid = static_cast<uint32_t>(o["pid"].toInt());
            if (!pid) continue;
            if (pid == selfPid) continue;
            if (detail::pidStateBestEffort_(pid) == detail::PidState::Dead) continue;

            if (!anyPrefer) anyPrefer = pid;

            const std::string sig = SwString(o["signal"].toString()).toStdString();
            if (sig.rfind("__config__|", 0) == 0) {
                if (!cfgPrefer) cfgPrefer = pid;
            } else if (sig.rfind("__rpc__|", 0) == 0) {
                if (!rpcPrefer) rpcPrefer = pid;
            }
        }

        if (cfgPrefer) return cfgPrefer;
        if (rpcPrefer) return rpcPrefer;
        if (anyPrefer) return anyPrefer;
        return 0;
    }

    SwString domain_;
    SwString object_;
    SwString clientInfo_;
};

} // namespace ipc
} // namespace sw

// -------------------------------------------------------------------------
// Proxy object helper macros
// -------------------------------------------------------------------------
//
// Example:
//   SW_PROXY_OBJECT_CLASS_BEGIN(DemoRemote)
//       SW_PROXY_OBJECT_RPC(int, add, int, int)
//       SW_PROXY_OBJECT_RPC(SwString, who)
//   SW_PROXY_OBJECT_CLASS_END()
//
//   DemoRemote r(domain, object, "clientInfo");
//   int v = r.add(1, 2);                   // blocking
//   r.add(1, 2, [](int v){ /* async */ }); // non-blocking
//

#define SW_PROXY_OBJECT_CLASS_BEGIN(ClassName)                                                   \
    class ClassName : public sw::ipc::ProxyObjectBase {                                           \
    private:                                                                                      \
        enum { swIpcRemoteSpecBase_ = __COUNTER__ };                                              \
        template <int I>                                                                          \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, I>,                          \
                                          SwList<sw::ipc::RpcRequestSpec>&) {}                    \
        template <int I>                                                                          \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, I>, SwStringList&) {}   \
        template <int I>                                                                          \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, I>,                      \
                                              const SwString&,                                    \
                                              SwStringList&) {                                    \
            return false;                                                                          \
        }                                                                                         \
        template <int I, int N>                                                                   \
        static typename std::enable_if<(I >= N), void>::type                                      \
        swIpcRemoteAppendAllSpecs_(SwList<sw::ipc::RpcRequestSpec>&) {}                           \
        template <int I, int N>                                                                   \
        static typename std::enable_if<(I < N), void>::type                                       \
        swIpcRemoteAppendAllSpecs_(SwList<sw::ipc::RpcRequestSpec>& req) {                        \
            swIpcRemoteAppendSpec(std::integral_constant<int, I>{}, req);                         \
            swIpcRemoteAppendAllSpecs_<I + 1, N>(req);                                            \
        }                                                                                         \
        template <int I, int N>                                                                   \
        static typename std::enable_if<(I >= N), void>::type                                      \
        swIpcRemoteAppendAllFunctions_(SwStringList&) {}                                          \
        template <int I, int N>                                                                   \
        static typename std::enable_if<(I < N), void>::type                                       \
        swIpcRemoteAppendAllFunctions_(SwStringList& out) {                                       \
            swIpcRemoteAppendFunction(std::integral_constant<int, I>{}, out);                     \
            swIpcRemoteAppendAllFunctions_<I + 1, N>(out);                                        \
        }                                                                                         \
        template <int I, int N>                                                                   \
        static typename std::enable_if<(I >= N), bool>::type                                      \
        swIpcRemoteFindArgTypes_(const SwString&, SwStringList&) {                                \
            return false;                                                                          \
        }                                                                                         \
        template <int I, int N>                                                                   \
        static typename std::enable_if<(I < N), bool>::type                                       \
        swIpcRemoteFindArgTypes_(const SwString& functionName, SwStringList& out) {               \
            if (swIpcRemoteAppendArgTypes(std::integral_constant<int, I>{}, functionName, out)) { \
                return true;                                                                      \
            }                                                                                     \
            return swIpcRemoteFindArgTypes_<I + 1, N>(functionName, out);                          \
        }                                                                                         \
                                                                                                  \
    public:                                                                                       \
        ClassName(const SwString& domain, const SwString& object, const SwString& clientInfo = SwString()) \
            : sw::ipc::ProxyObjectBase(domain, object, clientInfo) {}                             \
                                                                                                  \
    private:

#define SW_PROXY_OBJECT_CLASS_END()                                                               \
    private:                                                                                      \
        enum { swIpcRemoteSpecCount_ = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };                  \
                                                                                                  \
    public:                                                                                       \
        static SwStringList candidates(const SwString& domain, bool requireTypeMatch = true) {    \
            SwList<sw::ipc::RpcRequestSpec> req;                                                   \
            swIpcRemoteAppendAllSpecs_<0, swIpcRemoteSpecCount_>(req);                            \
            return sw::ipc::discoverRpcTargets(domain, req, requireTypeMatch);                    \
        }                                                                                         \
                                                                                                  \
        bool matchesInterface(bool requireTypeMatch = true) const {                               \
            SwList<sw::ipc::RpcRequestSpec> req;                                                   \
            swIpcRemoteAppendAllSpecs_<0, swIpcRemoteSpecCount_>(req);                            \
            const SwStringList hits = sw::ipc::discoverRpcTargets(domain(), req, requireTypeMatch); \
            const SwString wanted = domain() + "/" + object();                                    \
            for (size_t i = 0; i < hits.size(); ++i) {                                             \
                if (hits[i] == wanted) return true;                                                \
            }                                                                                     \
            return false;                                                                          \
        }                                                                                         \
                                                                                                  \
        SwStringList interfaceFunctions() const {                                                  \
            SwStringList out;                                                                      \
            swIpcRemoteAppendAllFunctions_<0, swIpcRemoteSpecCount_>(out);                         \
            return out;                                                                            \
        }                                                                                         \
                                                                                                  \
        SwStringList interfaceArgType(const SwString& functionName) const {                        \
            SwStringList out;                                                                      \
            (void)swIpcRemoteFindArgTypes_<0, swIpcRemoteSpecCount_>(functionName, out);           \
            return out;                                                                            \
        }                                                                                         \
                                                                                                  \
        SwStringList missingFunctions() const {                                                    \
            const SwStringList iface = interfaceFunctions();                                       \
            const SwStringList remote = functions();                                               \
            std::map<std::string, bool> remoteSet;                                                 \
            for (size_t i = 0; i < remote.size(); ++i) remoteSet[remote[i].toStdString()] = true; \
            SwStringList out;                                                                      \
            for (size_t i = 0; i < iface.size(); ++i) {                                            \
                const std::string n = iface[i].toStdString();                                      \
                if (remoteSet.find(n) == remoteSet.end()) out.append(iface[i]);                    \
            }                                                                                      \
            return out;                                                                            \
        }                                                                                         \
                                                                                                  \
        SwStringList extraFunctions() const {                                                      \
            const SwStringList iface = interfaceFunctions();                                       \
            const SwStringList remote = functions();                                               \
            std::map<std::string, bool> ifaceSet;                                                  \
            for (size_t i = 0; i < iface.size(); ++i) ifaceSet[iface[i].toStdString()] = true;    \
            SwStringList out;                                                                      \
            for (size_t i = 0; i < remote.size(); ++i) {                                           \
                const std::string n = remote[i].toStdString();                                     \
                if (ifaceSet.find(n) == ifaceSet.end()) out.append(remote[i]);                     \
            }                                                                                      \
            return out;                                                                            \
        }                                                                                         \
    };

// Dispatcher helpers (variadic macro overloading by arg-count).
#define SW_PROXY_OBJECT_CAT_(a, b) a##b
#define SW_PROXY_OBJECT_CAT(a, b) SW_PROXY_OBJECT_CAT_(a, b)
#define SW_PROXY_OBJECT_NARG_(...) SW_PROXY_OBJECT_NARG_I_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define SW_PROXY_OBJECT_NARG_I_(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define SW_PROXY_OBJECT_OVERLOAD(name, ...) SW_PROXY_OBJECT_CAT(name, SW_PROXY_OBJECT_NARG_(__VA_ARGS__))

// Non-void return

#define SW_PROXY_OBJECT_RPC0(Ret, name)                                                           \
    public:                                                                                       \
        Ret name(int timeoutMs = 2000) {                                                          \
            return swIpcRpcClient_##name##_.call(timeoutMs);                                      \
        }                                                                                         \
        void name(std::function<void(const Ret&)> onOk, int timeoutMs = 2000) {                   \
            swIpcRpcClient_##name##_.callAsync(std::move(onOk), timeoutMs);                       \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<Ret> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<>(SwString(#name)));                                \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            return true;                                                                          \
        }

#define SW_PROXY_OBJECT_RPC1(Ret, name, T1)                                                       \
    public:                                                                                       \
        Ret name(const T1& a0, int timeoutMs = 2000) {                                             \
            return swIpcRpcClient_##name##_.call(a0, timeoutMs);                                   \
        }                                                                                         \
        void name(const T1& a0, std::function<void(const Ret&)> onOk, int timeoutMs = 2000) {     \
            swIpcRpcClient_##name##_.callAsync(a0, std::move(onOk), timeoutMs);                    \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<Ret, T1> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<T1>(SwString(#name)));                              \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            out.append(SwString(#T1));                                                             \
            return true;                                                                          \
        }

#define SW_PROXY_OBJECT_RPC2(Ret, name, T1, T2)                                                    \
    public:                                                                                       \
        Ret name(const T1& a0, const T2& a1, int timeoutMs = 2000) {                               \
            return swIpcRpcClient_##name##_.call(a0, a1, timeoutMs);                               \
        }                                                                                         \
        void name(const T1& a0, const T2& a1, std::function<void(const Ret&)> onOk, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, std::move(onOk), timeoutMs);                \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<Ret, T1, T2> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<T1, T2>(SwString(#name)));                          \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            out.append(SwString(#T1));                                                             \
            out.append(SwString(#T2));                                                             \
            return true;                                                                          \
        }

#define SW_PROXY_OBJECT_RPC3(Ret, name, T1, T2, T3)                                                \
    public:                                                                                       \
        Ret name(const T1& a0, const T2& a1, const T3& a2, int timeoutMs = 2000) {                 \
            return swIpcRpcClient_##name##_.call(a0, a1, a2, timeoutMs);                           \
        }                                                                                         \
        void name(const T1& a0, const T2& a1, const T3& a2, std::function<void(const Ret&)> onOk, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, a2, std::move(onOk), timeoutMs);            \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<Ret, T1, T2, T3> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<T1, T2, T3>(SwString(#name)));                      \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            out.append(SwString(#T1));                                                             \
            out.append(SwString(#T2));                                                             \
            out.append(SwString(#T3));                                                             \
            return true;                                                                          \
        }

#define SW_PROXY_OBJECT_RPC4(Ret, name, T1, T2, T3, T4)                                            \
    public:                                                                                       \
        Ret name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, int timeoutMs = 2000) {   \
            return swIpcRpcClient_##name##_.call(a0, a1, a2, a3, timeoutMs);                       \
        }                                                                                         \
        void name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, std::function<void(const Ret&)> onOk, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, a2, a3, std::move(onOk), timeoutMs);        \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<Ret, T1, T2, T3, T4> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<T1, T2, T3, T4>(SwString(#name)));                  \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            out.append(SwString(#T1));                                                             \
            out.append(SwString(#T2));                                                             \
            out.append(SwString(#T3));                                                             \
            out.append(SwString(#T4));                                                             \
            return true;                                                                          \
        }

#define SW_PROXY_OBJECT_RPC5(Ret, name, T1, T2, T3, T4, T5)                                        \
    public:                                                                                       \
        Ret name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, const T5& a4, int timeoutMs = 2000) { \
            return swIpcRpcClient_##name##_.call(a0, a1, a2, a3, a4, timeoutMs);                   \
        }                                                                                         \
        void name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, const T5& a4, std::function<void(const Ret&)> onOk, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, a2, a3, a4, std::move(onOk), timeoutMs);    \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<Ret, T1, T2, T3, T4, T5> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<T1, T2, T3, T4, T5>(SwString(#name)));              \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            out.append(SwString(#T1));                                                             \
            out.append(SwString(#T2));                                                             \
            out.append(SwString(#T3));                                                             \
            out.append(SwString(#T4));                                                             \
            out.append(SwString(#T5));                                                             \
            return true;                                                                          \
        }

#define SW_PROXY_OBJECT_RPC6(Ret, name, T1, T2, T3, T4, T5, T6)                                    \
    public:                                                                                       \
        Ret name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, const T5& a4, const T6& a5, int timeoutMs = 2000) { \
            return swIpcRpcClient_##name##_.call(a0, a1, a2, a3, a4, a5, timeoutMs);               \
        }                                                                                         \
        void name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, const T5& a4, const T6& a5, std::function<void(const Ret&)> onOk, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, a2, a3, a4, a5, std::move(onOk), timeoutMs); \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<Ret, T1, T2, T3, T4, T5, T6> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<T1, T2, T3, T4, T5, T6>(SwString(#name)));          \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            out.append(SwString(#T1));                                                             \
            out.append(SwString(#T2));                                                             \
            out.append(SwString(#T3));                                                             \
            out.append(SwString(#T4));                                                             \
            out.append(SwString(#T5));                                                             \
            out.append(SwString(#T6));                                                             \
            return true;                                                                          \
        }

// void return

#define SW_PROXY_OBJECT_VOID0(name)                                                                \
    public:                                                                                       \
        bool name(int timeoutMs = 2000) {                                                         \
            return swIpcRpcClient_##name##_.call(timeoutMs);                                       \
        }                                                                                         \
        void name(std::function<void(bool ok)> onDone, int timeoutMs = 2000) {                    \
            swIpcRpcClient_##name##_.callAsync(std::move(onDone), timeoutMs);                      \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<void> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<>(SwString(#name)));                                \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            return true;                                                                          \
        }

#define SW_PROXY_OBJECT_VOID1(name, T1)                                                            \
    public:                                                                                       \
        bool name(const T1& a0, int timeoutMs = 2000) {                                            \
            return swIpcRpcClient_##name##_.call(a0, timeoutMs);                                   \
        }                                                                                         \
        void name(const T1& a0, std::function<void(bool ok)> onDone, int timeoutMs = 2000) {      \
            swIpcRpcClient_##name##_.callAsync(a0, std::move(onDone), timeoutMs);                  \
        }                                                                                         \
        SwString name##LastError() const {                                                        \
            return swIpcRpcClient_##name##_.lastError();                                          \
        }                                                                                         \
                                                                                                  \
    private:                                                                                      \
        mutable sw::ipc::RpcMethodClient<void, T1> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };            \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                \
            req.append(sw::ipc::rpcRequestSpec<T1>(SwString(#name)));                              \
        }                                                                                         \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                \
            out.append(SwString(#name));                                                           \
        }                                                                                         \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                        \
                                              SwStringList& out) {                                \
            if (functionName != SwString(#name)) return false;                                     \
            out.clear();                                                                           \
            out.append(SwString(#T1));                                                             \
            return true;                                                                          \
        }

#define SW_PROXY_OBJECT_VOID2(name, T1, T2)                                                         \
    public:                                                                                        \
        bool name(const T1& a0, const T2& a1, int timeoutMs = 2000) {                               \
            return swIpcRpcClient_##name##_.call(a0, a1, timeoutMs);                                \
        }                                                                                          \
        void name(const T1& a0, const T2& a1, std::function<void(bool ok)> onDone, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, std::move(onDone), timeoutMs);               \
        }                                                                                          \
        SwString name##LastError() const {                                                         \
            return swIpcRpcClient_##name##_.lastError();                                           \
        }                                                                                          \
                                                                                                   \
    private:                                                                                       \
        mutable sw::ipc::RpcMethodClient<void, T1, T2> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };             \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                 \
            req.append(sw::ipc::rpcRequestSpec<T1, T2>(SwString(#name)));                           \
        }                                                                                          \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                 \
            out.append(SwString(#name));                                                            \
        }                                                                                          \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                         \
                                              SwStringList& out) {                                 \
            if (functionName != SwString(#name)) return false;                                      \
            out.clear();                                                                            \
            out.append(SwString(#T1));                                                              \
            out.append(SwString(#T2));                                                              \
            return true;                                                                           \
        }

#define SW_PROXY_OBJECT_VOID3(name, T1, T2, T3)                                                     \
    public:                                                                                        \
        bool name(const T1& a0, const T2& a1, const T3& a2, int timeoutMs = 2000) {                 \
            return swIpcRpcClient_##name##_.call(a0, a1, a2, timeoutMs);                            \
        }                                                                                          \
        void name(const T1& a0, const T2& a1, const T3& a2, std::function<void(bool ok)> onDone, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, a2, std::move(onDone), timeoutMs);           \
        }                                                                                          \
        SwString name##LastError() const {                                                         \
            return swIpcRpcClient_##name##_.lastError();                                           \
        }                                                                                          \
                                                                                                   \
    private:                                                                                       \
        mutable sw::ipc::RpcMethodClient<void, T1, T2, T3> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };             \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                 \
            req.append(sw::ipc::rpcRequestSpec<T1, T2, T3>(SwString(#name)));                       \
        }                                                                                          \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                 \
            out.append(SwString(#name));                                                            \
        }                                                                                          \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                         \
                                              SwStringList& out) {                                 \
            if (functionName != SwString(#name)) return false;                                      \
            out.clear();                                                                            \
            out.append(SwString(#T1));                                                              \
            out.append(SwString(#T2));                                                              \
            out.append(SwString(#T3));                                                              \
            return true;                                                                           \
        }

#define SW_PROXY_OBJECT_VOID4(name, T1, T2, T3, T4)                                                 \
    public:                                                                                        \
        bool name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, int timeoutMs = 2000) {   \
            return swIpcRpcClient_##name##_.call(a0, a1, a2, a3, timeoutMs);                        \
        }                                                                                          \
        void name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, std::function<void(bool ok)> onDone, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, a2, a3, std::move(onDone), timeoutMs);       \
        }                                                                                          \
        SwString name##LastError() const {                                                         \
            return swIpcRpcClient_##name##_.lastError();                                           \
        }                                                                                          \
                                                                                                   \
    private:                                                                                       \
        mutable sw::ipc::RpcMethodClient<void, T1, T2, T3, T4> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };             \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                 \
            req.append(sw::ipc::rpcRequestSpec<T1, T2, T3, T4>(SwString(#name)));                   \
        }                                                                                          \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                 \
            out.append(SwString(#name));                                                            \
        }                                                                                          \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                         \
                                              SwStringList& out) {                                 \
            if (functionName != SwString(#name)) return false;                                      \
            out.clear();                                                                            \
            out.append(SwString(#T1));                                                              \
            out.append(SwString(#T2));                                                              \
            out.append(SwString(#T3));                                                              \
            out.append(SwString(#T4));                                                              \
            return true;                                                                           \
        }

#define SW_PROXY_OBJECT_VOID5(name, T1, T2, T3, T4, T5)                                             \
    public:                                                                                        \
        bool name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, const T5& a4, int timeoutMs = 2000) { \
            return swIpcRpcClient_##name##_.call(a0, a1, a2, a3, a4, timeoutMs);                    \
        }                                                                                          \
        void name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, const T5& a4, std::function<void(bool ok)> onDone, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, a2, a3, a4, std::move(onDone), timeoutMs);   \
        }                                                                                          \
        SwString name##LastError() const {                                                         \
            return swIpcRpcClient_##name##_.lastError();                                           \
        }                                                                                          \
                                                                                                   \
    private:                                                                                       \
        mutable sw::ipc::RpcMethodClient<void, T1, T2, T3, T4, T5> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };             \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                 \
            req.append(sw::ipc::rpcRequestSpec<T1, T2, T3, T4, T5>(SwString(#name)));               \
        }                                                                                          \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                 \
            out.append(SwString(#name));                                                            \
        }                                                                                          \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                         \
                                              SwStringList& out) {                                 \
            if (functionName != SwString(#name)) return false;                                      \
            out.clear();                                                                            \
            out.append(SwString(#T1));                                                              \
            out.append(SwString(#T2));                                                              \
            out.append(SwString(#T3));                                                              \
            out.append(SwString(#T4));                                                              \
            out.append(SwString(#T5));                                                              \
            return true;                                                                           \
        }

#define SW_PROXY_OBJECT_VOID6(name, T1, T2, T3, T4, T5, T6)                                         \
    public:                                                                                        \
        bool name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, const T5& a4, const T6& a5, int timeoutMs = 2000) { \
            return swIpcRpcClient_##name##_.call(a0, a1, a2, a3, a4, a5, timeoutMs);                \
        }                                                                                          \
        void name(const T1& a0, const T2& a1, const T3& a2, const T4& a3, const T5& a4, const T6& a5, std::function<void(bool ok)> onDone, int timeoutMs = 2000) { \
            swIpcRpcClient_##name##_.callAsync(a0, a1, a2, a3, a4, a5, std::move(onDone), timeoutMs); \
        }                                                                                          \
        SwString name##LastError() const {                                                         \
            return swIpcRpcClient_##name##_.lastError();                                           \
        }                                                                                          \
                                                                                                   \
    private:                                                                                       \
        mutable sw::ipc::RpcMethodClient<void, T1, T2, T3, T4, T5, T6> swIpcRpcClient_##name##_{domain(), object(), SwString(#name), clientInfo()}; \
        enum { swIpcRemoteSpecIndex_##name = __COUNTER__ - swIpcRemoteSpecBase_ - 1 };             \
        static void swIpcRemoteAppendSpec(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                          SwList<sw::ipc::RpcRequestSpec>& req) {                 \
            req.append(sw::ipc::rpcRequestSpec<T1, T2, T3, T4, T5, T6>(SwString(#name)));           \
        }                                                                                          \
        static void swIpcRemoteAppendFunction(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              SwStringList& out) {                                 \
            out.append(SwString(#name));                                                            \
        }                                                                                          \
        static bool swIpcRemoteAppendArgTypes(std::integral_constant<int, swIpcRemoteSpecIndex_##name>, \
                                              const SwString& functionName,                         \
                                              SwStringList& out) {                                 \
            if (functionName != SwString(#name)) return false;                                      \
            out.clear();                                                                            \
            out.append(SwString(#T1));                                                              \
            out.append(SwString(#T2));                                                              \
            out.append(SwString(#T3));                                                              \
            out.append(SwString(#T4));                                                              \
            out.append(SwString(#T5));                                                              \
            out.append(SwString(#T6));                                                              \
            return true;                                                                           \
        }

// Single-macro API: SW_PROXY_OBJECT_RPC(Ret, name, [T1..T6])
#define SW_PROXY_OBJECT_RPC_2(Ret, name) SW_PROXY_OBJECT_RPC0(Ret, name)
#define SW_PROXY_OBJECT_RPC_3(Ret, name, T1) SW_PROXY_OBJECT_RPC1(Ret, name, T1)
#define SW_PROXY_OBJECT_RPC_4(Ret, name, T1, T2) SW_PROXY_OBJECT_RPC2(Ret, name, T1, T2)
#define SW_PROXY_OBJECT_RPC_5(Ret, name, T1, T2, T3) SW_PROXY_OBJECT_RPC3(Ret, name, T1, T2, T3)
#define SW_PROXY_OBJECT_RPC_6(Ret, name, T1, T2, T3, T4) SW_PROXY_OBJECT_RPC4(Ret, name, T1, T2, T3, T4)
#define SW_PROXY_OBJECT_RPC_7(Ret, name, T1, T2, T3, T4, T5) SW_PROXY_OBJECT_RPC5(Ret, name, T1, T2, T3, T4, T5)
#define SW_PROXY_OBJECT_RPC_8(Ret, name, T1, T2, T3, T4, T5, T6) SW_PROXY_OBJECT_RPC6(Ret, name, T1, T2, T3, T4, T5, T6)
#define SW_PROXY_OBJECT_RPC(...) SW_PROXY_OBJECT_OVERLOAD(SW_PROXY_OBJECT_RPC_, __VA_ARGS__)(__VA_ARGS__)

// Single-macro API: SW_PROXY_OBJECT_VOID(name, [T1..T6])
#define SW_PROXY_OBJECT_VOID_1(name) SW_PROXY_OBJECT_VOID0(name)
#define SW_PROXY_OBJECT_VOID_2(name, T1) SW_PROXY_OBJECT_VOID1(name, T1)
#define SW_PROXY_OBJECT_VOID_3(name, T1, T2) SW_PROXY_OBJECT_VOID2(name, T1, T2)
#define SW_PROXY_OBJECT_VOID_4(name, T1, T2, T3) SW_PROXY_OBJECT_VOID3(name, T1, T2, T3)
#define SW_PROXY_OBJECT_VOID_5(name, T1, T2, T3, T4) SW_PROXY_OBJECT_VOID4(name, T1, T2, T3, T4)
#define SW_PROXY_OBJECT_VOID_6(name, T1, T2, T3, T4, T5) SW_PROXY_OBJECT_VOID5(name, T1, T2, T3, T4, T5)
#define SW_PROXY_OBJECT_VOID_7(name, T1, T2, T3, T4, T5, T6) SW_PROXY_OBJECT_VOID6(name, T1, T2, T3, T4, T5, T6)
#define SW_PROXY_OBJECT_VOID(...) SW_PROXY_OBJECT_OVERLOAD(SW_PROXY_OBJECT_VOID_, __VA_ARGS__)(__VA_ARGS__)
