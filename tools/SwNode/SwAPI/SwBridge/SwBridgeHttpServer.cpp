#include "SwBridgeHttpServer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <set>
#include <cstdlib>

#include "SwJsonDocument.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"
#include "SwHttpApp.h"
#include "SwList.h"
#include "SwMap.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

namespace {

static SwJsonArray registryForTarget(const SwString& nameSpace, const SwString& objectName);

static uint64_t parseHexU64(const std::string& s) {
    return static_cast<uint64_t>(std::strtoull(s.c_str(), nullptr, 16));
}

static SwString computeAcceptKey(const SwString& clientKeyBase64) {
    static const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SwString input = clientKeyBase64 + kGuid;
    std::vector<unsigned char> sha1 = SwCrypto::generateHashSHA1(input.toStdString());
    return SwString(SwCrypto::base64Encode(sha1));
}

static SwByteArray buildServerFrame(uint8_t opcode, const SwByteArray& payload, bool fin = true) {
    const uint64_t len = static_cast<uint64_t>(payload.size());
    SwByteArray frame;
    frame.reserve(static_cast<size_t>(2 + (len <= 125 ? 0 : (len <= 65535 ? 2 : 8)) + len));

    frame.append(static_cast<char>((fin ? 0x80 : 0x00) | (opcode & 0x0F)));
    if (len <= 125) {
        frame.append(static_cast<char>(len & 0x7F));
    } else if (len <= 65535) {
        frame.append(static_cast<char>(126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.append(static_cast<char>((len >> (8 * i)) & 0xFF));
        }
    }
    if (!payload.isEmpty()) {
        frame.append(payload.constData(), payload.size());
    }
    return frame;
}

static bool parseClientFrame(SwByteArray& buffer,
                             uint8_t& outOpcode,
                             bool& outFin,
                             SwByteArray& outPayload,
                             bool& outNeedMoreData) {
    outNeedMoreData = false;
    if (buffer.size() < 2) {
        outNeedMoreData = true;
        return false;
    }

    const unsigned char* data = reinterpret_cast<const unsigned char*>(buffer.constData());
    const uint8_t b0 = data[0];
    const uint8_t b1 = data[1];

    outFin = (b0 & 0x80) != 0;
    outOpcode = static_cast<uint8_t>(b0 & 0x0F);

    if ((b0 & 0x70) != 0) {
        return false;
    }

    const bool masked = (b1 & 0x80) != 0;
    if (!masked) {
        // RFC: client->server frames MUST be masked.
        return false;
    }

    uint64_t payloadLen = static_cast<uint64_t>(b1 & 0x7F);
    size_t pos = 2;

    if (payloadLen == 126) {
        if (buffer.size() < pos + 2) {
            outNeedMoreData = true;
            return false;
        }
        payloadLen = (static_cast<uint64_t>(data[pos]) << 8) |
                     (static_cast<uint64_t>(data[pos + 1]));
        pos += 2;
    } else if (payloadLen == 127) {
        if (buffer.size() < pos + 8) {
            outNeedMoreData = true;
            return false;
        }
        payloadLen = 0;
        for (int i = 0; i < 8; ++i) {
            payloadLen = (payloadLen << 8) | static_cast<uint64_t>(data[pos + i]);
        }
        pos += 8;
    }

    if (buffer.size() < pos + 4) {
        outNeedMoreData = true;
        return false;
    }
    unsigned char maskKey[4] = { data[pos + 0], data[pos + 1], data[pos + 2], data[pos + 3] };
    pos += 4;

    if (payloadLen > static_cast<uint64_t>(buffer.size() - pos)) {
        outNeedMoreData = true;
        return false;
    }

    outPayload = payloadLen > 0
        ? buffer.mid(static_cast<int>(pos), static_cast<int>(payloadLen))
        : SwByteArray();

    for (size_t i = 0; i < outPayload.size(); ++i) {
        outPayload[i] = static_cast<char>(
            static_cast<unsigned char>(outPayload[i]) ^ maskKey[i % 4]);
    }

    buffer.remove(0, static_cast<int>(pos + payloadLen));
    return true;
}

static SwString executableDirPath() {
#if defined(_WIN32)
    char buf[MAX_PATH + 1] = {0};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return SwDir::currentPath();
    SwString p(buf);
    p.replace("\\", "/");
    const size_t slash = p.lastIndexOf('/');
    if (slash == static_cast<size_t>(-1)) return SwDir::currentPath();
    return p.left(static_cast<int>(slash));
#else
    return SwDir::currentPath();
#endif
}

static SwString jsonString(const SwJsonValue& v) {
    SwJsonDocument d;
    if (v.isObject()) d.setObject(v.toObject());
    else if (v.isArray()) d.setArray(v.toArray());
    else {
        SwJsonObject o;
        o["value"] = v;
        d.setObject(o);
    }
    return d.toJson(SwJsonDocument::JsonFormat::Compact);
}

static SwString normalizedPath_(SwString p) {
    p.replace("\\", "/");
    while (p.contains("//")) p.replace("//", "/");
    while (p.startsWith("/")) p = p.mid(1);
    while (p.endsWith("/")) p = p.left(static_cast<int>(p.size()) - 1);
    return p;
}

static bool tryParseIndex_(const SwString& s, size_t& out) {
    if (s.isEmpty()) return false;
    size_t x = 0;
    const size_t max = static_cast<size_t>(-1);

    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        const size_t digit = static_cast<size_t>(c - '0');
        if (x > (max - digit) / 10) return false;
        x = x * 10 + digit;
    }
    out = x;
    return true;
}

static bool tryGetPath_(const SwJsonValue& root, const SwString& rawPath, SwJsonValue& out, SwString& err) {
    err.clear();
    out = SwJsonValue();

    const SwString p = normalizedPath_(rawPath.trimmed());
    if (p.isEmpty()) {
        out = root;
        return true;
    }

    SwList<SwString> toks = p.split('/');
    SwJsonValue cur = root;

    for (size_t i = 0; i < toks.size(); ++i) {
        const SwString token = toks[i];
        if (token.isEmpty()) continue;

        if (cur.isObject()) {
            const SwJsonObject obj(cur.toObject());
            if (!obj.contains(token)) {
                err = SwString("path not found: missing key '") + token + "'";
                return false;
            }
            cur = obj[token];
            continue;
        }

        if (cur.isArray()) {
            size_t idx = 0;
            if (!tryParseIndex_(token, idx)) {
                err = SwString("path not found: expected array index, got '") + token + "'";
                return false;
            }
            const SwJsonArray arr(cur.toArray());
            if (idx >= arr.size()) {
                err = SwString("path not found: array index out of range: ") + token;
                return false;
            }
            cur = arr[idx];
            continue;
        }

        err = SwString("path not found: leaf is not an object/array at '") + token + "'";
        return false;
    }

    out = cur;
    return true;
}

static SwString sanitizeSegmentForFile(const SwString& in) {
    std::string s = in.toStdString();
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            (c == '_') || (c == '-') || (c == '.');
        if (!ok) s[i] = '_';
    }
    if (s.empty()) s = "root";
    return SwString(s);
}

static SwString sanitizeNsForFile(const SwString& nsIn) {
    std::string s = nsIn.toStdString();
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '/' || c == '\\') s[i] = '_';
    }
    while (!s.empty() && (s.front() == '_' || s.front() == '/')) s.erase(0, 1);
    while (!s.empty() && (s.back() == '_' || s.back() == '/')) s.pop_back();
    if (s.empty()) s = "root";
    return sanitizeSegmentForFile(SwString(s));
}

static SwString configRootAbsolute(const SwString& root = "systemConfig") {
    const SwString r = root.isEmpty() ? SwString("systemConfig") : root;
    const std::string s = r.toStdString();
    const bool isAbs = (!s.empty() && (s[0] == '/' || s[0] == '\\')) ||
                       (s.size() > 1 && s[1] == ':');
    if (isAbs) return r;
    return SwDir::currentPath() + r;
}

static bool loadDocObject(const SwString& path, SwJsonDocument& outDoc) {
    if (!SwFile::isFile(path)) return false;
    SwFile f(path);
    if (!f.open(SwFile::Read)) return false;
    const SwString raw = f.readAll();
    f.close();
    SwJsonDocument d;
    SwString err;
    if (!d.loadFromJson(raw.toStdString(), err) || !d.isObject()) return false;
    outDoc = d;
    return true;
}

static void mergeValueDeep(SwJsonValue& target, const SwJsonValue& src);
static SwString findConfigDocSignalForTarget(const SwString& nameSpace, const SwString& objectName);

static void mergeObjectDeep(SwJsonObject& target, const SwJsonObject& src) {
    SwJsonObject::Container data = src.data();
    for (SwJsonObject::Container::const_iterator it = data.begin(); it != data.end(); ++it) {
        const SwString k(it->first);
        if (target.contains(k) && target[k].isObject() && it->second.isObject()) {
            SwJsonValue tv = target[k];
            mergeValueDeep(tv, it->second);
            target[k] = tv;
        } else {
            target.insert(it->first, it->second);
        }
    }
}

static void mergeValueDeep(SwJsonValue& target, const SwJsonValue& src) {
    if (target.isObject() && src.isObject()) {
        SwJsonObject t(target.toObject());
        SwJsonObject s(src.toObject());
        mergeObjectDeep(t, s);
        target = SwJsonValue(t);
        return;
    }
    target = src;
}

static SwJsonObject loadMergedConfigForTarget(const SwString& sysName, const SwString& objectFqn) {
    const SwString root = configRootAbsolute("systemConfig");
    const SwString nsFile = sanitizeNsForFile(sysName);
    const SwString objFile = sanitizeSegmentForFile(objectFqn);

    // Global config is shared by objectName only (leaf of "nameSpace/objectName").
    SwString objLeaf = objectFqn;
    {
        SwString x = objectFqn;
        x.replace("\\", "/");
        SwList<SwString> partsRaw = x.split('/');
        SwList<SwString> parts;
        for (size_t i = 0; i < partsRaw.size(); ++i) {
            if (!partsRaw[i].isEmpty()) parts.append(partsRaw[i]);
        }
        if (!parts.isEmpty()) objLeaf = parts[parts.size() - 1];
    }
    const SwString objLeafFile = sanitizeSegmentForFile(objLeaf);

    const SwString globalPath = root + "/global/" + objLeafFile + ".json";
    const SwString localPath  = root + "/local/" + nsFile + "_" + objFile + ".json";
    const SwString userPath   = root + "/user/"  + nsFile + "_" + objFile + ".json";

    SwJsonDocument gd(SwJsonObject{}), ld(SwJsonObject{}), ud(SwJsonObject{});
    SwJsonDocument tmp;
    if (loadDocObject(globalPath, tmp)) gd = tmp;
    if (loadDocObject(localPath, tmp))  ld = tmp;
    if (loadDocObject(userPath, tmp))   ud = tmp;

    SwJsonObject merged;
    if (gd.isObject()) mergeObjectDeep(merged, gd.object());
    if (ld.isObject()) mergeObjectDeep(merged, ld.object());
    if (ud.isObject()) mergeObjectDeep(merged, ud.object());
    return merged;
}

static bool loadConfigFromIpcForTarget(const SwString& nameSpace, const SwString& objectName, SwJsonObject& outCfg) {
    const SwString cfgSig = findConfigDocSignalForTarget(nameSpace, objectName);
    if (cfgSig.isEmpty()) return false;

    sw::ipc::Registry reg(nameSpace, objectName);
    sw::ipc::Signal<uint64_t, SwString> sig(reg, cfgSig);

    uint64_t pubId = 0;
    SwString json;
    if (!sig.readLatest(pubId, json)) return false;

    SwJsonDocument d;
    SwString err;
    if (!d.loadFromJson(json.toStdString(), err) || !d.isObject()) return false;
    outCfg = d.object();
    return true;
}

static SwJsonArray registryForTarget(const SwString& nameSpace, const SwString& objectName) {
    SwJsonArray out;
    SwJsonArray all = sw::ipc::shmRegistrySnapshot(nameSpace);
    const SwString ns = nameSpace;
    const SwString obj = objectName;
    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        SwJsonObject o(v.toObject());
        if (SwString(o["domain"].toString()) != ns) continue;
        if (SwString(o["object"].toString()) != obj) continue;
        out.append(SwJsonValue(o));
    }
    return out;
}

static bool activePidsForDomain(const SwString& domain, std::set<uint32_t>& out) {
    out.clear();
    bool found = false;

    SwJsonArray apps = sw::ipc::shmAppsSnapshot();
    for (size_t i = 0; i < apps.size(); ++i) {
        const SwJsonValue v = apps[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["domain"].toString()) != domain) continue;
        found = true;

        const SwJsonValue pidsVal = o["pids"];
        if (!pidsVal.isArray()) continue;
        const SwJsonArray pids = pidsVal.toArray();
        for (size_t k = 0; k < pids.size(); ++k) {
            const SwJsonValue pv = pids[k];
            if (!pv.isObject()) continue;
            const SwJsonObject po(pv.toObject());
            const int pid = po["pid"].toInt();
            if (pid > 0) out.insert(static_cast<uint32_t>(pid));
        }
        break; // domain unique
    }

    return found;
}

static SwJsonArray devicesForDomain(const SwString& domain) {
    // Group signals registry entries by objectName.
    SwJsonArray all = sw::ipc::shmRegistrySnapshot(domain);
    std::set<uint32_t> activePids;
    const bool haveDomain = activePidsForDomain(domain, activePids);
    if (!haveDomain) return SwJsonArray();

    struct DeviceAgg {
        uint64_t lastSeenMs{0};
        std::set<uint32_t> pids;
        // Presence of "__config__|*" means "this object is a SwRemoteObject" (config-doc signal).
        std::vector<std::string> configIds; // from "__config__|<objectName>"
        std::set<std::string> configIdsSet;
    };
    std::map<std::string, DeviceAgg> agg; // key=object

    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());

        const std::string object = SwString(o["object"].toString()).toStdString();
        if (object.empty()) continue;

        DeviceAgg& a = agg[object];
        const uint64_t t = static_cast<uint64_t>(o["lastSeenMs"].toDouble());
        if (t > a.lastSeenMs) a.lastSeenMs = t;
        const uint32_t pid = static_cast<uint32_t>(o["pid"].toInt());
        if (pid != 0) {
            // Filter stale registry entries (process dead): keep only active PIDs from the apps registry.
            if (!activePids.count(pid)) continue;
            a.pids.insert(pid);
        }

        const std::string sig = SwString(o["signal"].toString()).toStdString();
        if (sig.size() > 10 && sig.rfind("__config__|", 0) == 0) {
            const std::string cfgId = sig.substr(std::strlen("__config__|"));
            if (!cfgId.empty() && !a.configIdsSet.count(cfgId)) {
                a.configIdsSet.insert(cfgId);
                a.configIds.push_back(cfgId);
            }
        }
    }

    SwJsonArray out;
    for (std::map<std::string, DeviceAgg>::const_iterator it = agg.begin(); it != agg.end(); ++it) {
        // Only list "real devices" (SwRemoteObject instances): they expose a "__config__|*" config-doc signal.
        if (it->second.configIds.empty()) continue;

        const std::string objectFqn = it->first;
        std::string nsPart;
        std::string objName = objectFqn;
        const size_t slash = objectFqn.rfind('/');
        if (slash != std::string::npos) {
            nsPart = objectFqn.substr(0, slash);
            objName = (slash + 1 < objectFqn.size()) ? objectFqn.substr(slash + 1) : std::string();
        }
        if (objName.empty()) continue;

        SwJsonObject d;
        d["domain"] = SwJsonValue(domain.toStdString());
        d["nameSpace"] = SwJsonValue(nsPart);
        d["objectName"] = SwJsonValue(objName);
        d["object"] = SwJsonValue(objectFqn);
        d["target"] = SwJsonValue((domain + "/" + SwString(objectFqn)).toStdString());
        d["lastSeenMs"] = SwJsonValue(static_cast<double>(it->second.lastSeenMs));

        const std::string cfg0 = it->second.configIds.empty() ? std::string() : it->second.configIds.front();
        d["configId"] = SwJsonValue(cfg0);

        SwJsonArray cfgIds;
        for (size_t k = 0; k < it->second.configIds.size(); ++k) {
            cfgIds.append(SwJsonValue(it->second.configIds[k]));
        }
        d["configIds"] = SwJsonValue(cfgIds);

        SwJsonArray pids;
        for (std::set<uint32_t>::const_iterator pit = it->second.pids.begin(); pit != it->second.pids.end(); ++pit) {
            pids.append(SwJsonValue(static_cast<int>(*pit)));
        }
        d["pids"] = SwJsonValue(pids);
        out.append(SwJsonValue(d));
    }
    return out;
}

static SwString normalizePrefix_(SwString x) {
    x.replace("\\", "/");
    while (x.startsWith("/")) x = x.mid(1);
    while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
    return x;
}

static bool matchesNamespacePrefix_(const SwString& nodeNs, const SwString& nsPrefix) {
    if (nsPrefix.isEmpty()) return true;
    const SwString a = normalizePrefix_(nodeNs);
    const SwString b = normalizePrefix_(nsPrefix);
    if (a == b) return true;
    if (a.startsWith(b + "/")) return true;
    return false;
}

static SwList<SwString> domainsFromApps_() {
    SwList<SwString> out;
    std::set<std::string> seen;

    SwJsonArray apps = sw::ipc::shmAppsSnapshot();
    for (size_t i = 0; i < apps.size(); ++i) {
        const SwJsonValue v = apps[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        const SwString dom = SwString(o["domain"].toString());
        const std::string key = dom.toStdString();
        if (dom.isEmpty()) continue;
        if (!seen.insert(key).second) continue;
        out.push_back(dom);
    }
    return out;
}

static SwJsonArray nodesForDomain_(const SwString& domain, bool includeStale, const SwString& nsPrefix) {
    SwJsonArray out;
    if (domain.isEmpty()) return out;

    const SwJsonArray all = sw::ipc::shmRegistrySnapshot(domain);
    std::set<uint32_t> activePids;
    const bool haveDomain = activePidsForDomain(domain, activePids);
    if (!haveDomain && !includeStale) return out;

    struct Agg {
        uint64_t lastSeenMs{0};
        std::set<uint32_t> pids;
        std::vector<std::string> configIds;
        std::set<std::string> configIdsSet;
    };
    std::map<std::string, Agg> agg; // key=objectFqn

    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());

        const std::string object = SwString(o["object"].toString()).toStdString();
        if (object.empty()) continue;

        const uint32_t pid = static_cast<uint32_t>(o["pid"].toInt());
        if (pid != 0 && haveDomain && !includeStale && !activePids.count(pid)) {
            continue;
        }

        Agg& a = agg[object];
        const uint64_t t = static_cast<uint64_t>(o["lastSeenMs"].toDouble());
        if (t > a.lastSeenMs) a.lastSeenMs = t;
        if (pid != 0) a.pids.insert(pid);

        const std::string sig = SwString(o["signal"].toString()).toStdString();
        if (sig.size() > 10 && sig.rfind("__config__|", 0) == 0) {
            const std::string cfgId = sig.substr(std::strlen("__config__|"));
            if (!cfgId.empty() && !a.configIdsSet.count(cfgId)) {
                a.configIdsSet.insert(cfgId);
                a.configIds.push_back(cfgId);
            }
        }
    }

    for (std::map<std::string, Agg>::const_iterator it = agg.begin(); it != agg.end(); ++it) {
        const std::string objectFqn = it->first;
        const Agg& a = it->second;
        if (a.configIds.empty()) continue; // only SwRemoteObject nodes

        std::string nsPart;
        std::string objName = objectFqn;
        const size_t slash = objectFqn.rfind('/');
        if (slash != std::string::npos) {
            nsPart = objectFqn.substr(0, slash);
            objName = (slash + 1 < objectFqn.size()) ? objectFqn.substr(slash + 1) : std::string();
        }
        if (objName.empty()) continue;

        if (!matchesNamespacePrefix_(SwString(nsPart), nsPrefix)) continue;

        SwJsonObject d;
        d["target"] = SwJsonValue((domain + "/" + SwString(objectFqn)).toStdString());
        d["domain"] = SwJsonValue(domain.toStdString());
        d["nameSpace"] = SwJsonValue(nsPart);
        d["objectName"] = SwJsonValue(objName);
        d["object"] = SwJsonValue(objectFqn);
        d["lastSeenMs"] = SwJsonValue(static_cast<double>(a.lastSeenMs));
        d["alive"] = SwJsonValue(!a.pids.empty());

        SwJsonArray pidArr;
        for (std::set<uint32_t>::const_iterator pit = a.pids.begin(); pit != a.pids.end(); ++pit) {
            pidArr.append(SwJsonValue(static_cast<int>(*pit)));
        }
        d["pids"] = SwJsonValue(pidArr);

        SwJsonArray cfgIds;
        for (size_t k = 0; k < a.configIds.size(); ++k) {
            cfgIds.append(SwJsonValue(a.configIds[k]));
        }
        d["configIds"] = SwJsonValue(cfgIds);

        out.append(SwJsonValue(d));
    }

    return out;
}

static bool nodeInfoForTarget_(const SwString& domain, const SwString& object, bool includeStale, SwJsonObject& out, SwString& err) {
    out = SwJsonObject{};
    err.clear();

    if (domain.isEmpty() || object.isEmpty()) {
        err = "invalid target";
        return false;
    }

    std::set<uint32_t> activePids;
    const bool haveDomain = activePidsForDomain(domain, activePids);
    if (!haveDomain && !includeStale) {
        err = "domain not found in apps registry";
        return false;
    }

    SwJsonArray entries = registryForTarget(domain, object);
    SwJsonArray filtered;
    for (size_t i = 0; i < entries.size(); ++i) {
        const SwJsonValue v = entries[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        const uint32_t pid = static_cast<uint32_t>(o["pid"].toInt());
        if (pid != 0 && haveDomain && !includeStale && !activePids.count(pid)) continue;
        filtered.append(v);
    }

    if (filtered.isEmpty() && !includeStale) {
        err = "target not found in registry";
        return false;
    }

    uint64_t lastSeen = 0;
    std::set<uint32_t> pids;
    std::vector<std::string> configIds;
    std::set<std::string> cfgSet;
    size_t rpcCount = 0;

    for (size_t i = 0; i < filtered.size(); ++i) {
        const SwJsonValue v = filtered[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());

        const uint64_t t = static_cast<uint64_t>(o["lastSeenMs"].toDouble());
        if (t > lastSeen) lastSeen = t;

        const int pid = o["pid"].toInt();
        if (pid > 0) pids.insert(static_cast<uint32_t>(pid));

        const std::string sig = SwString(o["signal"].toString()).toStdString();
        if (sig.size() > 10 && sig.rfind("__config__|", 0) == 0) {
            const std::string cfgId = sig.substr(std::strlen("__config__|"));
            if (!cfgId.empty() && !cfgSet.count(cfgId)) {
                cfgSet.insert(cfgId);
                configIds.push_back(cfgId);
            }
        }
        if (sig.size() > 7 && sig.rfind("__rpc__|", 0) == 0) rpcCount++;
    }

    std::string nsPart;
    std::string objName = object.toStdString();
    const std::string objectFqn = object.toStdString();
    const size_t slash = objectFqn.rfind('/');
    if (slash != std::string::npos) {
        nsPart = objectFqn.substr(0, slash);
        objName = (slash + 1 < objectFqn.size()) ? objectFqn.substr(slash + 1) : std::string();
    }

    out["target"] = SwJsonValue((domain + "/" + object).toStdString());
    out["domain"] = SwJsonValue(domain.toStdString());
    out["object"] = SwJsonValue(object.toStdString());
    out["nameSpace"] = SwJsonValue(nsPart);
    out["objectName"] = SwJsonValue(objName);
    out["lastSeenMs"] = SwJsonValue(static_cast<double>(lastSeen));
    out["alive"] = SwJsonValue(!pids.empty());
    out["signalCount"] = SwJsonValue(static_cast<int>(filtered.size()));
    out["rpcCount"] = SwJsonValue(static_cast<int>(rpcCount));

    SwJsonArray pidArr;
    for (std::set<uint32_t>::const_iterator it = pids.begin(); it != pids.end(); ++it) {
        pidArr.append(SwJsonValue(static_cast<int>(*it)));
    }
    out["pids"] = SwJsonValue(pidArr);

    SwJsonArray cfgArr;
    for (size_t i = 0; i < configIds.size(); ++i) {
        cfgArr.append(SwJsonValue(configIds[i]));
    }
    out["configIds"] = SwJsonValue(cfgArr);

    return true;
}

static std::vector<std::string> parseArgTypesFromTypeName(const std::string& typeName) {
    // typeName() returns compiler-specific strings, e.g.:
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
        while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.erase(0, 1);
        while (!s.empty() && (s[s.size() - 1] == ' ' || s[s.size() - 1] == '\t')) s.pop_back();
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

static SwJsonArray argTypesToJson(const std::vector<std::string>& args) {
    SwJsonArray a;
    for (size_t i = 0; i < args.size(); ++i) {
        a.append(SwJsonValue(args[i]));
    }
    return a;
}

static bool isBoolType(const std::string& t) {
    return t == "bool" || t == "BOOL";
}
static bool isIntType(const std::string& t) {
    return t == "int" || t == "int32_t" || t == "signed int";
}
static bool isU32Type(const std::string& t) {
    return t == "uint32_t" || t == "unsigned int" || t == "unsigned long";
}
static bool isU64Type(const std::string& t) {
    return t == "uint64_t" || t == "unsigned __int64" || t == "unsigned long long";
}
static bool isFloatType(const std::string& t) {
    return t == "double" || t == "float";
}
static bool isStringType(const std::string& t) {
    return t == "SwString" || t == "class SwString" || t == "struct SwString";
}
static bool isBytesType(const std::string& t) {
    return t == "SwByteArray" || t == "class SwByteArray" || t == "struct SwByteArray";
}

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
    SwJsonArray entries = registryForTarget(domain, object);
    for (size_t i = 0; i < entries.size(); ++i) {
        const SwJsonValue v = entries[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
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
        err = "invalid rpc queue info (missing shmName/typeId)";
        return false;
    }

    try {
        out.shmName = info.shmName;
        out.typeId = info.typeId;
        out.map = RpcQueueAccess::Mapping::openOrCreate(info.shmName, info.typeId);
    } catch (const std::exception& e) {
        err = SwString("rpc: open SHM failed: ") + e.what();
        return false;
    }

#if defined(_WIN32)
    const std::string mtxName = (info.shmName + "_mtx").toStdString();
    out.mtx = WinHandle(::CreateMutexA(NULL, FALSE, mtxName.c_str()));
    if (!out.mtx) {
        err = "rpc: CreateMutex failed";
        return false;
    }

    const std::string evtName = (info.shmName + "_evt").toStdString();
    out.evt = WinHandle(::CreateEventA(NULL, FALSE, FALSE, evtName.c_str()));
    if (!out.evt) {
        err = "rpc: CreateEvent failed";
        return false;
    }
#endif

    return true;
}

static bool rpcQueuePushRaw(RpcQueueAccess& q, const uint8_t* data, size_t size, SwString& err) {
    if (!q.map) {
        err = "rpc: queue not opened";
        return false;
    }
    if (size > RpcQueueAccess::kMaxPayload) {
        err = "rpc: payload too large";
        return false;
    }

    RpcQueueAccess::Layout* L = q.map->layout();
    if (!L) {
        err = "rpc: invalid queue layout";
        return false;
    }

    bool ok = false;

#if defined(_WIN32)
    ::WaitForSingleObject(q.mtx.h, INFINITE);
    const uint64_t inFlight = (L->seq >= L->readSeq) ? (L->seq - L->readSeq) : 0;
    if (inFlight < RpcQueueAccess::kCapacity) {
        const uint64_t next = L->seq + 1;
        RpcQueueAccess::Layout::Slot& slot = L->entries[next % RpcQueueAccess::kCapacity];
        slot.seq = next;
        slot.size = static_cast<uint32_t>(size);
        if (slot.size <= RpcQueueAccess::kMaxPayload) {
            if (slot.size != 0) std::memcpy(slot.data, data, slot.size);
            L->seq = next;
            ok = true;
        }
    }
    ::ReleaseMutex(q.mtx.h);

    if (ok && q.evt) {
        (void)::SetEvent(q.evt.h);
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
            if (slot.size != 0) std::memcpy(slot.data, data, slot.size);
            L->seq = next;
            ok = true;
        }
    }
    if (ok) pthread_cond_broadcast(&L->cv);
    pthread_mutex_unlock(&L->mtx);
#endif

    if (!ok) {
        err = "rpc: request queue full (or payload too large)";
        return false;
    }
    return true;
}

static bool rpcQueuePopOneRaw(RpcQueueAccess& q, std::vector<uint8_t>& out) {
    out.clear();
    if (!q.map) return false;
    RpcQueueAccess::Layout* L = q.map->layout();
    if (!L) return false;

    bool have = false;

#if defined(_WIN32)
    ::WaitForSingleObject(q.mtx.h, INFINITE);
    const uint64_t seq = L->seq;
    uint64_t readSeq = L->readSeq;
    if (readSeq < seq) {
        const uint64_t next = readSeq + 1;
        RpcQueueAccess::Layout::Slot& slot = L->entries[next % RpcQueueAccess::kCapacity];
        const uint32_t sz = slot.size;
        if (slot.seq == next && sz <= RpcQueueAccess::kMaxPayload) {
            out.assign(slot.data, slot.data + sz);
            have = true;
        }
        L->readSeq = next;
    }
    ::ReleaseMutex(q.mtx.h);
#else
    pthread_mutex_lock(&L->mtx);
    const uint64_t seq = L->seq;
    uint64_t readSeq = L->readSeq;
    if (readSeq < seq) {
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

struct SignalAccess {
    static const size_t kMaxPayload = 4096;
    typedef sw::ipc::ShmMapping<kMaxPayload> Mapping;
    typedef sw::ipc::ShmLayout<kMaxPayload> Layout;

    std::shared_ptr<Mapping> map;
    SwString shmName;
    uint64_t typeId{0};
#if defined(_WIN32)
    WinHandle mtx;
#endif
};

static bool openSignalAccess(const RpcQueueInfo& info, SignalAccess& out, SwString& err) {
    if (info.shmName.isEmpty() || info.typeId == 0) {
        err = "signal: missing shmName/typeId in registry";
        return false;
    }

    try {
        out.map = SignalAccess::Mapping::openOrCreate(info.shmName, info.typeId);
        out.shmName = info.shmName;
        out.typeId = info.typeId;
    } catch (const std::exception& e) {
        err = SwString("signal: open mapping failed: ") + e.what();
        return false;
    } catch (...) {
        err = "signal: open mapping failed";
        return false;
    }

#if defined(_WIN32)
    const std::string base = info.shmName.toStdString();
    out.mtx = WinHandle(::OpenMutexA(SYNCHRONIZE, FALSE, (base + "_mtx").c_str()));
#endif

    return true;
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


static SwString findConfigDocSignalForTarget(const SwString& nameSpace, const SwString& objectName) {
    SwJsonArray entries = registryForTarget(nameSpace, objectName);
    for (size_t i = 0; i < entries.size(); ++i) {
        SwJsonValue v = entries[i];
        if (!v.isObject()) continue;
        SwJsonObject o(v.toObject());
        const SwString sig = SwString(o["signal"].toString());
        if (sig.startsWith("__config__|")) {
            return SwString(sig);
        }
    }
    return SwString();
}

} // namespace

// ---------------------------------------------------------------------------
// Class implementation
// ---------------------------------------------------------------------------

SwBridgeHttpServer::SwBridgeHttpServer(uint16_t httpPort, SwObject* parent)
    : SwObject(parent), httpPort_(httpPort), wsPort_(httpPort + 1), app_(this) {
    wsServer_ = new SwTcpServer(this);
    connect(wsServer_, &SwTcpServer::newConnection, this, &SwBridgeHttpServer::onWsNewConnection_);
}

SwBridgeHttpServer::~SwBridgeHttpServer() = default;

void SwBridgeHttpServer::setApiKey(const SwString& apiKey) {
    apiKey_ = apiKey;
}

bool SwBridgeHttpServer::start() {
    registerRoutes_();
    if (!app_.listen(httpPort_)) return false;
    if (!wsServer_->listen(wsPort_)) return false;
    std::cout << "[SwBridge] WebSocket on port " << wsPort_ << "\n";
    return true;
}

void SwBridgeHttpServer::sendErrorJson_(SwHttpContext& ctx, int statusCode, const SwString& message) {
    SwJsonObject o;
    o["ok"] = SwJsonValue(false);
    o["error"] = SwJsonValue(message);
    ctx.json(SwJsonValue(o), statusCode);
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void SwBridgeHttpServer::registerRoutes_() {

    // --- Auth middleware ---
    if (!apiKey_.isEmpty()) {
        SwString key = apiKey_;
        app_.use("/api", [key](SwHttpContext& ctx, const SwHttpApp::SwHttpNext& next) {
            SwString provided = ctx.headerValue("x-api-key");
            if (provided.isEmpty()) provided = ctx.queryValue("apiKey");
            if (provided != key) {
                SwJsonObject o;
                o["ok"] = SwJsonValue(false);
                o["error"] = SwJsonValue("unauthorized");
                ctx.json(SwJsonValue(o), 401);
                return;
            }
            next();
        });
    }

    // --- Static files ---
    const SwString staticRoot = executableDirPath();
    app_.mountStatic("/home", staticRoot);

    // --- Root redirect ---
    app_.get("/", [](SwHttpContext& ctx) {
        ctx.redirect("/home/");
    });

    // =======================================================================
    // GET routes
    // =======================================================================

    app_.get("/api/registry", [](SwHttpContext& ctx) {
        ctx.json(SwJsonValue(sw::ipc::shmAppsSnapshot()));
    });

    app_.get("/api/apps", [](SwHttpContext& ctx) {
        ctx.json(SwJsonValue(sw::ipc::shmAppsSnapshot()));
    });

    app_.get("/api/devices", [](SwHttpContext& ctx) {
        const SwString domain = ctx.queryValue("domain");
        if (domain.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing query param: domain");
            return;
        }
        ctx.json(SwJsonValue(devicesForDomain(domain)));
    });

    app_.get("/api/nodes", [](SwHttpContext& ctx) {
        const SwString domain = ctx.queryValue("domain");
        SwString nsPrefix = ctx.queryValue("ns");
        if (nsPrefix.isEmpty()) nsPrefix = ctx.queryValue("namespace");
        SwString includeStaleStr = ctx.queryValue("includeStale");
        if (includeStaleStr.isEmpty()) includeStaleStr = ctx.queryValue("include-stale");
        if (includeStaleStr.isEmpty()) includeStaleStr = ctx.queryValue("stale");
        const bool includeStale = (!includeStaleStr.isEmpty() && includeStaleStr != "0" && includeStaleStr != "false");

        if (!domain.isEmpty()) {
            ctx.json(SwJsonValue(nodesForDomain_(domain, includeStale, nsPrefix)));
            return;
        }

        SwJsonArray out;
        const SwList<SwString> doms = domainsFromApps_();
        for (size_t i = 0; i < doms.size(); ++i) {
            SwJsonArray part = nodesForDomain_(doms[i], includeStale, nsPrefix);
            for (size_t k = 0; k < part.size(); ++k) {
                out.append(part[k]);
            }
        }
        ctx.json(SwJsonValue(out));
    });

    app_.get("/api/nodeInfo", [](SwHttpContext& ctx) {
        const SwString target = ctx.queryValue("target");
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing query param: target");
            return;
        }
        SwString includeStaleStr = ctx.queryValue("includeStale");
        if (includeStaleStr.isEmpty()) includeStaleStr = ctx.queryValue("include-stale");
        if (includeStaleStr.isEmpty()) includeStaleStr = ctx.queryValue("stale");
        const bool includeStale = (!includeStaleStr.isEmpty() && includeStaleStr != "0" && includeStaleStr != "false");

        SwString domain, obj;
        if (!splitTarget_(target, domain, obj) || domain.isEmpty()) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        SwJsonObject info;
        SwString err;
        if (!nodeInfoForTarget_(domain, obj, includeStale, info, err)) {
            sendErrorJson_(ctx, 404, err);
            return;
        }
        ctx.json(SwJsonValue(info));
    });

    app_.get("/api/signals", [](SwHttpContext& ctx) {
        const SwString target = ctx.queryValue("target");
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing query param: target");
            return;
        }

        SwString ns, obj;
        if (!splitTarget_(target, ns, obj)) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        SwJsonArray entries = registryForTarget(ns, obj);
        SwJsonArray out;
        for (size_t i = 0; i < entries.size(); ++i) {
            SwJsonValue v = entries[i];
            if (!v.isObject()) continue;
            SwJsonObject o(v.toObject());

            const SwString sig = SwString(o["signal"].toString()).toStdString();
            const std::string typeName = SwString(o["typeName"].toString()).toStdString();
            const std::vector<std::string> args = parseArgTypesFromTypeName(typeName);

            SwJsonObject item;
            item["signal"] = SwJsonValue(sig);
            item["typeName"] = SwJsonValue(typeName);
            item["args"] = SwJsonValue(argTypesToJson(args));
            item["kind"] = SwJsonValue(
                sig.startsWith("__cfg__|") ? "configValue" :
                (sig.startsWith("__config__|") ? "configDoc" : "signal"));
            out.append(SwJsonValue(item));
        }

        ctx.json(SwJsonValue(out));
    });

    app_.get("/api/signalLatest", [](SwHttpContext& ctx) {
        const SwString target = ctx.queryValue("target");
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing query param: target");
            return;
        }

        SwString name = ctx.queryValue("name");
        if (name.isEmpty()) name = ctx.queryValue("signal");
        if (name.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing query param: name");
            return;
        }
        if (name.startsWith("__rpc__|") || name.startsWith("__rpc_ret__|")) {
            sendErrorJson_(ctx, 400, "signalLatest does not support rpc queues");
            return;
        }

        SwString domain, obj;
        if (!splitTarget_(target, domain, obj) || domain.isEmpty()) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        RpcQueueInfo info;
        if (!findSignalInRegistryForTarget(domain, obj, name, info)) {
            sendErrorJson_(ctx, 404, "signal not found in registry (not created yet?)");
            return;
        }

        SignalAccess sigA;
        SwString err;
        if (!openSignalAccess(info, sigA, err)) {
            sendErrorJson_(ctx, 500, err);
            return;
        }

        SignalAccess::Layout* L = sigA.map ? sigA.map->layout() : nullptr;
        if (!L) {
            sendErrorJson_(ctx, 500, "signal: mapping layout is null");
            return;
        }

        uint64_t seq = 0;
        uint32_t sz = 0;
        std::array<uint8_t, SignalAccess::kMaxPayload> tmp;

#if defined(_WIN32)
        if (sigA.mtx) ::WaitForSingleObject(sigA.mtx.h, INFINITE);
        seq = L->seq;
        sz = L->size;
        if (sz != 0 && sz <= SignalAccess::kMaxPayload) {
            std::memcpy(tmp.data(), L->data, sz);
        }
        if (sigA.mtx) ::ReleaseMutex(sigA.mtx.h);
#else
        pthread_mutex_lock(&L->mtx);
        seq = L->seq;
        sz = L->size;
        if (sz != 0 && sz <= SignalAccess::kMaxPayload) {
            std::memcpy(tmp.data(), L->data, sz);
        }
        pthread_mutex_unlock(&L->mtx);
#endif

        uint64_t since = 0;
        const SwString sinceStr = ctx.queryValue("sinceSeq");
        if (!sinceStr.isEmpty()) {
            since = static_cast<uint64_t>(std::strtoull(sinceStr.toStdString().c_str(), NULL, 10));
        }

        SwJsonObject out;
        out["target"] = SwJsonValue(target.toStdString());
        out["signal"] = SwJsonValue(name.toStdString());
        out["typeName"] = SwJsonValue(info.typeName.toStdString());
        out["seq"] = SwJsonValue(std::to_string(seq));
        out["changed"] = SwJsonValue(seq > since);

        SwJsonArray argsOut;
        if (sz != 0 && sz <= SignalAccess::kMaxPayload) {
            sw::ipc::detail::Decoder dec(tmp.data(), sz);
            const std::vector<std::string> argTypes = parseArgTypesFromTypeName(info.typeName.toStdString());
            for (size_t i = 0; i < argTypes.size(); ++i) {
                SwJsonValue v;
                SwString derr;
                if (!decodeJsonValueByType(dec, argTypes[i], v, derr)) {
                    sendErrorJson_(ctx, 500, derr);
                    return;
                }
                argsOut.append(v);
            }
        }
        out["args"] = SwJsonValue(argsOut);

        ctx.json(SwJsonValue(out));
    });

    app_.get("/api/rpcs", [](SwHttpContext& ctx) {
        const SwString target = ctx.queryValue("target");
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing query param: target");
            return;
        }

        SwString domain, obj;
        if (!splitTarget_(target, domain, obj)) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        SwJsonArray entries = registryForTarget(domain, obj);
        SwJsonArray out;
        for (size_t i = 0; i < entries.size(); ++i) {
            const SwJsonValue v = entries[i];
            if (!v.isObject()) continue;
            const SwJsonObject o(v.toObject());

            const std::string sig = SwString(o["signal"].toString()).toStdString();
            if (sig.rfind("__rpc__|", 0) != 0) continue;

            const std::string method = sig.substr(std::strlen("__rpc__|"));
            const std::string typeName = SwString(o["typeName"].toString()).toStdString();
            std::vector<std::string> args = parseArgTypesFromTypeName(typeName);
            if (args.size() >= 3) args.erase(args.begin(), args.begin() + 3);

            SwJsonObject item;
            item["method"] = SwJsonValue(method);
            item["signal"] = SwJsonValue(sig);
            item["typeName"] = SwJsonValue(typeName);
            item["requestTypeId"] = o["typeId"];
            item["args"] = SwJsonValue(argTypesToJson(args));
            out.append(SwJsonValue(item));
        }

        ctx.json(SwJsonValue(out));
    });

    app_.get("/api/connections", [](SwHttpContext& ctx) {
        SwString domain = ctx.queryValue("domain");
        const SwString target = ctx.queryValue("target");
        SwString filterObject;

        SwString nsPrefix = ctx.queryValue("ns");
        if (nsPrefix.isEmpty()) nsPrefix = ctx.queryValue("namespace");
        nsPrefix = normalizePrefix_(nsPrefix);

        SwString kind = ctx.queryValue("kind");
        if (kind.isEmpty()) kind = "all";
        kind = kind.toLower();

        const SwString resolveStr = ctx.queryValue("resolve");
        const bool resolve = (!resolveStr.isEmpty() && resolveStr != "0" && resolveStr != "false");

        auto startsWith = [](const SwString& s, const SwString& prefix) { return s.startsWith(prefix); };

        auto matchesKind = [&](const SwString& signal) -> bool {
            if (kind.isEmpty() || kind == "all" || kind == "*") return true;
            const bool internal = startsWith(signal, "__");
            const bool rpc = startsWith(signal, "__rpc__|") || startsWith(signal, "__rpc_ret__|");
            const bool cfg = startsWith(signal, "__config__|") || startsWith(signal, "__cfg__|");
            if (kind == "topic" || kind == "topics") return !internal;
            if (kind == "internal" || kind == "internals") return internal;
            if (kind == "rpc" || kind == "service" || kind == "services") return rpc;
            if (kind == "config" || kind == "param" || kind == "params") return cfg;
            return false;
        };

        auto isKnownKind = [&]() -> bool {
            if (kind.isEmpty()) return true;
            return kind == "all" || kind == "*" || kind == "topic" || kind == "topics" || kind == "internal" || kind == "internals" ||
                   kind == "rpc" || kind == "service" || kind == "services" || kind == "config" || kind == "param" || kind == "params";
        };

        if (!isKnownKind()) {
            sendErrorJson_(ctx, 400, SwString("unknown kind: ") + kind);
            return;
        }

        auto matchesPrefix = [](const SwString& s, const SwString& prefix) -> bool {
            if (prefix.isEmpty()) return true;
            if (s == prefix) return true;
            if (s.size() > prefix.size() && s.startsWith(prefix + "/")) return true;
            return false;
        };

        auto filterTargetsByNs = [&](const std::vector<SwString>& targets, const SwString& dom, const SwString& nsP) -> std::vector<SwString> {
            if (targets.empty() || nsP.isEmpty()) return targets;
            const SwString domPrefix = dom + "/";
            std::vector<SwString> out;
            for (size_t i = 0; i < targets.size(); ++i) {
                const SwString& t = targets[i];
                if (!t.startsWith(domPrefix)) continue;
                const SwString obj = normalizePrefix_(t.mid(static_cast<int>(domPrefix.size())));
                if (matchesPrefix(obj, nsP)) out.push_back(t);
            }
            return out;
        };

        auto buildPidMap = [&](const SwString& dom) -> std::map<int, std::vector<SwString>> {
            std::map<int, std::vector<SwString>> map;
            SwJsonArray nodes = nodesForDomain_(dom, false, SwString());
            for (size_t i = 0; i < nodes.size(); ++i) {
                const SwJsonValue v = nodes[i];
                if (!v.isObject()) continue;
                const SwJsonObject o(v.toObject());
                const SwString t = SwString(o["target"].toString());
                const SwJsonValue pidsVal = o["pids"];
                if (!pidsVal.isArray()) continue;
                const SwJsonArray pids = pidsVal.toArray();
                for (size_t k = 0; k < pids.size(); ++k) {
                    const int pid = pids[k].toInt();
                    if (pid <= 0) continue;
                    map[pid].push_back(t);
                }
            }
            return map;
        };

        struct Group {
            SwString domain;
            SwString object;
            SwString signal;
            SwString pubTarget;
            std::set<std::string> subTargets;
            void initFrom(const SwString& d, const SwString& obj, const SwString& sig) {
                if (!domain.isEmpty()) return;
                domain = d; object = obj; signal = sig;
                pubTarget = d + "/" + obj;
            }
        };

        auto processOneDomain = [&](const SwString& dom, const SwString& filterObj) -> SwJsonArray {
            SwJsonArray out;
            const std::map<int, std::vector<SwString>> pidMap = resolve ? buildPidMap(dom) : std::map<int, std::vector<SwString>>();
            std::map<std::string, Group> groups;

            SwJsonArray subs = sw::ipc::shmSubscribersSnapshot(dom);
            for (size_t i = 0; i < subs.size(); ++i) {
                const SwJsonValue v = subs[i];
                if (!v.isObject()) continue;
                const SwJsonObject o(v.toObject());

                const SwString rawObj = normalizePrefix_(SwString(o["object"].toString()));
                const SwString sig = SwString(o["signal"].toString());
                const SwString subObj = o.contains("subObject") ? normalizePrefix_(SwString(o["subObject"].toString())) : SwString();
                const SwString subTarget = o.contains("subTarget") ? SwString(o["subTarget"].toString()) : SwString();

                const bool objHasNs = rawObj.contains("/");
                const SwString objWithNs = (!nsPrefix.isEmpty() && !objHasNs) ? normalizePrefix_(nsPrefix + "/" + rawObj) : rawObj;
                const bool subObjHasNs = (!subObj.isEmpty() && subObj.contains("/"));
                const SwString subObjWithNs = (!nsPrefix.isEmpty() && !subObj.isEmpty() && !subObjHasNs) ? normalizePrefix_(nsPrefix + "/" + subObj) : subObj;

                if (!matchesPrefix(objWithNs, nsPrefix)) continue;
                if (!subObjWithNs.isEmpty() && !matchesPrefix(subObjWithNs, nsPrefix)) continue;
                if (!filterObj.isEmpty() && normalizePrefix_(filterObj) != objWithNs) continue;
                if (!matchesKind(sig)) continue;

                if (!resolve) { out.append(v); continue; }

                const SwString pubTarget = dom + "/" + objWithNs;
                const std::string key = pubTarget.toStdString() + "\n" + sig.toStdString();
                Group& g = groups[key];
                g.initFrom(dom, objWithNs, sig);

                if (!subTarget.isEmpty()) {
                    g.subTargets.insert(subTarget.toStdString());
                } else {
                    const int subPid = o.contains("subPid") ? o["subPid"].toInt() : 0;
                    std::map<int, std::vector<SwString>>::const_iterator it = pidMap.find(subPid);
                    if (it != pidMap.end()) {
                        const std::vector<SwString> filtered = filterTargetsByNs(it->second, dom, nsPrefix);
                        for (size_t k = 0; k < filtered.size(); ++k) {
                            if (filtered[k] == pubTarget) continue;
                            g.subTargets.insert(filtered[k].toStdString());
                        }
                    }
                }
            }

            if (!resolve) return out;

            for (std::map<std::string, Group>::const_iterator it = groups.begin(); it != groups.end(); ++it) {
                const Group& g = it->second;
                SwJsonObject x;
                x["domain"] = SwJsonValue(g.domain.toStdString());
                x["object"] = SwJsonValue(g.object.toStdString());
                x["signal"] = SwJsonValue(g.signal.toStdString());
                x["pubTarget"] = SwJsonValue(g.pubTarget.toStdString());
                SwJsonArray subArr;
                for (std::set<std::string>::const_iterator sit = g.subTargets.begin(); sit != g.subTargets.end(); ++sit) {
                    subArr.append(SwJsonValue(*sit));
                }
                x["subTargets"] = SwJsonValue(subArr);
                out.append(SwJsonValue(x));
            }
            return out;
        };

        if (domain.isEmpty()) {
            if (!target.isEmpty()) {
                SwString ns, obj;
                if (!splitTarget_(target, ns, obj)) {
                    sendErrorJson_(ctx, 400, "invalid target format");
                    return;
                }
                domain = ns;
                filterObject = obj;
            }
        }

        if (!domain.isEmpty()) {
            ctx.json(SwJsonValue(processOneDomain(domain, filterObject)));
            return;
        }

        SwJsonArray out;
        const SwList<SwString> doms = domainsFromApps_();
        for (size_t i = 0; i < doms.size(); ++i) {
            SwJsonArray part = processOneDomain(doms[i], SwString());
            for (size_t k = 0; k < part.size(); ++k) {
                out.append(part[k]);
            }
        }
        ctx.json(SwJsonValue(out));
    });

    app_.get("/api/state", [this](SwHttpContext& ctx) {
        const SwString target = ctx.queryValue("target");
        if (!target.isEmpty()) ensureTargetSubscriptions_(target);

        SwJsonObject o;
        o["target"] = SwJsonValue(subscribedTarget_);
        o["lastPong"] = SwJsonValue(lastPong_);
        o["lastConfigAck"] = SwJsonValue(lastConfigAck_);
        ctx.json(SwJsonValue(o));
    });

    app_.get("/api/configDoc", [this](SwHttpContext& ctx) {
        const SwString target = ctx.queryValue("target");
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing query param: target");
            return;
        }

        const SwString path = ctx.queryValue("path");

        SwString ns, obj;
        if (!splitTarget_(target, ns, obj) || ns.isEmpty()) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        const SwString cfgSig = findConfigDocSignalForTarget(ns, obj);
        if (cfgSig.isEmpty()) {
            sendErrorJson_(ctx, 404, "target does not expose a __config__|* signal (not a SwRemoteObject?)");
            return;
        }

        sw::ipc::Registry reg(ns, obj);
        sw::ipc::Signal<uint64_t, SwString> sig(reg, cfgSig);

        uint64_t pubId = 0;
        SwString json;
        if (!sig.readLatest(pubId, json)) {
            sendErrorJson_(ctx, 500, "failed to read latest config snapshot");
            return;
        }

        SwJsonDocument d;
        SwString err;
        if (!d.loadFromJson(json.toStdString(), err) || !d.isObject()) {
            sendErrorJson_(ctx, 500, SwString("invalid JSON from target: ") + err);
            return;
        }

        SwJsonObject out;
        out["target"] = SwJsonValue(target.toStdString());
        out["configSignal"] = SwJsonValue(cfgSig.toStdString());
        out["pubId"] = SwJsonValue(std::to_string(pubId));

        if (!path.isEmpty()) {
            SwJsonValue v;
            SwString perr;
            if (!tryGetPath_(d.toJsonValue(), path, v, perr)) {
                sendErrorJson_(ctx, 400, perr);
                return;
            }
            out["value"] = v;
            ctx.json(SwJsonValue(out));
            return;
        }

        out["config"] = SwJsonValue(d.object());
        ctx.json(SwJsonValue(out));
    });

    app_.get("/api/config", [](SwHttpContext& ctx) {
        const SwString target = ctx.queryValue("target");
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing query param: target");
            return;
        }

        const SwString path = ctx.queryValue("path");

        SwString ns, obj;
        if (!splitTarget_(target, ns, obj)) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        SwJsonObject cfg;
        if (!loadConfigFromIpcForTarget(ns, obj, cfg)) {
            cfg = loadMergedConfigForTarget(ns, obj);
        }

        if (!path.isEmpty()) {
            SwJsonValue v;
            SwString perr;
            if (!tryGetPath_(SwJsonValue(cfg), path, v, perr)) {
                sendErrorJson_(ctx, 400, perr);
                return;
            }
            ctx.json(v);
            return;
        }

        ctx.json(SwJsonValue(cfg));
    });

    // =======================================================================
    // POST routes
    // =======================================================================

    app_.post("/api/target", [this](SwHttpContext& ctx) {
        SwJsonDocument d;
        SwString err;
        if (!ctx.parseJsonBody(d, err) || !d.isObject()) {
            sendErrorJson_(ctx, 400, SwString("invalid JSON: ") + err);
            return;
        }
        const SwJsonObject obj = d.object();
        const SwString target = obj["target"].toString();
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: target");
            return;
        }
        ensureTargetSubscriptions_(target);
        SwJsonObject ok;
        ok["ok"] = SwJsonValue(true);
        ok["target"] = SwJsonValue(subscribedTarget_);
        ctx.json(SwJsonValue(ok));
    });

    app_.post("/api/ping", [this](SwHttpContext& ctx) {
        SwJsonDocument d;
        SwString err;
        if (!ctx.parseJsonBody(d, err) || !d.isObject()) {
            sendErrorJson_(ctx, 400, SwString("invalid JSON: ") + err);
            return;
        }
        const SwJsonObject obj = d.object();
        const SwString target = obj["target"].toString();
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: target");
            return;
        }
        ensureTargetSubscriptions_(target);
        const int n = obj["n"].toInt();
        const SwString s = obj["s"].toString();
        const bool ok = ipcSendPing_(target, n, s);
        SwJsonObject out;
        out["ok"] = SwJsonValue(ok);
        ctx.json(SwJsonValue(out));
    });

    app_.post("/api/config", [this](SwHttpContext& ctx) {
        SwJsonDocument d;
        SwString err;
        if (!ctx.parseJsonBody(d, err) || !d.isObject()) {
            sendErrorJson_(ctx, 400, SwString("invalid JSON: ") + err);
            return;
        }
        const SwJsonObject obj = d.object();
        const SwString target = obj["target"].toString();
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: target");
            return;
        }
        const SwString name = obj["name"].toString();
        if (name.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: name");
            return;
        }

        SwString valueStr;
        const SwJsonValue v = obj["value"];
        if (v.isString()) {
            valueStr = SwString(v.toString());
        } else if (v.isBool()) {
            valueStr = v.toBool() ? "true" : "false";
        } else if (v.isInt()) {
            valueStr = SwString(std::to_string(v.toInt()));
        } else if (v.isDouble()) {
            std::ostringstream oss;
            oss << v.toDouble();
            valueStr = SwString(oss.str());
        } else {
            sendErrorJson_(ctx, 400, "field 'value' must be string/bool/int/double");
            return;
        }

        const bool ok = ipcSendConfigRaw_(target, name, valueStr);
        SwJsonObject out;
        out["ok"] = SwJsonValue(ok);
        out["name"] = SwJsonValue(name);
        out["value"] = SwJsonValue(valueStr);
        ctx.json(SwJsonValue(out));
    });

    app_.post("/api/configAll", [](SwHttpContext& ctx) {
        SwJsonDocument d;
        SwString err;
        if (!ctx.parseJsonBody(d, err) || !d.isObject()) {
            sendErrorJson_(ctx, 400, SwString("invalid JSON: ") + err);
            return;
        }
        const SwJsonObject obj = d.object();
        const SwString target = obj["target"].toString();
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: target");
            return;
        }

        SwString ns, oName;
        if (!splitTarget_(target, ns, oName)) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        const SwJsonValue cfgVal = obj["config"];
        if (!cfgVal.isObject()) {
            sendErrorJson_(ctx, 400, "missing field: config (object)");
            return;
        }

        const SwString cfgSig = findConfigDocSignalForTarget(ns, oName);
        if (cfgSig.isEmpty()) {
            sendErrorJson_(ctx, 404, "target does not expose a __config__|* signal (not a SwRemoteObject?)");
            return;
        }

        sw::ipc::Registry reg(ns, oName);
        sw::ipc::Signal<uint64_t, SwString> sig(reg, cfgSig);
        SwJsonDocument doc(cfgVal.toObject());
        const bool ok = sig.publish(0, doc.toJson(SwJsonDocument::JsonFormat::Compact));

        SwJsonObject out;
        out["ok"] = SwJsonValue(ok);
        out["configSignal"] = SwJsonValue(cfgSig);
        ctx.json(SwJsonValue(out));
    });

    app_.post("/api/signal", [](SwHttpContext& ctx) {
        SwJsonDocument d;
        SwString err;
        if (!ctx.parseJsonBody(d, err) || !d.isObject()) {
            sendErrorJson_(ctx, 400, SwString("invalid JSON: ") + err);
            return;
        }
        const SwJsonObject obj = d.object();
        const SwString target = obj["target"].toString();
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: target");
            return;
        }
        const SwString sigName = obj["name"].toString();
        if (sigName.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: name");
            return;
        }
        const SwJsonValue argsVal = obj["args"];
        if (!argsVal.isArray()) {
            sendErrorJson_(ctx, 400, "missing field: args (array)");
            return;
        }

        SwString ns, oName;
        if (!splitTarget_(target, ns, oName)) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        std::string typeName;
        {
            SwJsonArray entries = registryForTarget(ns, oName);
            for (size_t i = 0; i < entries.size(); ++i) {
                SwJsonValue v = entries[i];
                if (!v.isObject()) continue;
                SwJsonObject e(v.toObject());
                if (SwString(e["signal"].toString()) == sigName) {
                    typeName = SwString(e["typeName"].toString()).toStdString();
                    break;
                }
            }
        }
        if (typeName.empty()) {
            sendErrorJson_(ctx, 404, "signal not found in registry (not created yet?)");
            return;
        }

        const std::vector<std::string> argTypes = parseArgTypesFromTypeName(typeName);
        const SwJsonArray arr = argsVal.toArray();
        if (arr.size() != argTypes.size()) {
            sendErrorJson_(ctx, 400, "args count mismatch vs registry signature");
            return;
        }

        sw::ipc::Registry reg(ns, oName);

        auto asBool = [](const SwJsonValue& v, bool& out) -> bool {
            if (v.isBool()) { out = v.toBool(); return true; }
            if (v.isInt()) { out = (v.toInt() != 0); return true; }
            if (v.isString()) { const std::string s = SwString(v.toString()).toStdString(); out = (s == "1" || s == "true" || s == "TRUE" || s == "True"); return true; }
            return false;
        };
        auto asInt = [](const SwJsonValue& v, int& out) -> bool {
            if (v.isInt()) { out = v.toInt(); return true; }
            if (v.isDouble()) { out = static_cast<int>(v.toDouble()); return true; }
            if (v.isString()) { out = std::atoi(SwString(v.toString()).toStdString().c_str()); return true; }
            if (v.isBool()) { out = v.toBool() ? 1 : 0; return true; }
            return false;
        };
        auto asU64 = [](const SwJsonValue& v, uint64_t& out) -> bool {
            if (v.isInt()) { out = static_cast<uint64_t>(v.toInt()); return true; }
            if (v.isDouble()) { out = static_cast<uint64_t>(v.toDouble()); return true; }
            if (v.isString()) { out = static_cast<uint64_t>(std::strtoull(SwString(v.toString()).toStdString().c_str(), NULL, 10)); return true; }
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

        bool ok = false;
        if (argTypes.size() == 1 && isBoolType(argTypes[0])) {
            bool a0 = false;
            if (!asBool(arr[0], a0)) { sendErrorJson_(ctx, 400, "arg0 bool parse failed"); return; }
            sw::ipc::Signal<bool> sig(reg, sigName); ok = sig.publish(a0);
        } else if (argTypes.size() == 1 && isIntType(argTypes[0])) {
            int a0 = 0;
            if (!asInt(arr[0], a0)) { sendErrorJson_(ctx, 400, "arg0 int parse failed"); return; }
            sw::ipc::Signal<int> sig(reg, sigName); ok = sig.publish(a0);
        } else if (argTypes.size() == 1 && isFloatType(argTypes[0])) {
            double a0 = 0.0;
            if (!asDouble(arr[0], a0)) { sendErrorJson_(ctx, 400, "arg0 double parse failed"); return; }
            sw::ipc::Signal<double> sig(reg, sigName); ok = sig.publish(a0);
        } else if (argTypes.size() == 1 && isStringType(argTypes[0])) {
            SwString a0;
            if (!asString(arr[0], a0)) { sendErrorJson_(ctx, 400, "arg0 SwString parse failed"); return; }
            sw::ipc::Signal<SwString> sig(reg, sigName); ok = sig.publish(a0);
        } else if (argTypes.size() == 1 && isBytesType(argTypes[0])) {
            SwByteArray a0;
            if (!asBytes(arr[0], a0)) { sendErrorJson_(ctx, 400, "arg0 SwByteArray parse failed (use string)"); return; }
            sw::ipc::Signal<SwByteArray> sig(reg, sigName); ok = sig.publish(a0);
        } else if (argTypes.size() == 1 && isU64Type(argTypes[0])) {
            uint64_t a0 = 0;
            if (!asU64(arr[0], a0)) { sendErrorJson_(ctx, 400, "arg0 u64 parse failed"); return; }
            sw::ipc::Signal<uint64_t> sig(reg, sigName); ok = sig.publish(a0);
        } else if (argTypes.size() == 2 && isU64Type(argTypes[0]) && isStringType(argTypes[1])) {
            uint64_t a0 = 0; SwString a1;
            if (!asU64(arr[0], a0) || !asString(arr[1], a1)) { sendErrorJson_(ctx, 400, "args parse failed"); return; }
            sw::ipc::Signal<uint64_t, SwString> sig(reg, sigName); ok = sig.publish(a0, a1);
        } else if (argTypes.size() == 2 && isIntType(argTypes[0]) && isStringType(argTypes[1])) {
            int a0 = 0; SwString a1;
            if (!asInt(arr[0], a0) || !asString(arr[1], a1)) { sendErrorJson_(ctx, 400, "args parse failed"); return; }
            sw::ipc::Signal<int, SwString> sig(reg, sigName); ok = sig.publish(a0, a1);
        } else if (argTypes.size() == 3 && isIntType(argTypes[0]) && isIntType(argTypes[1]) && isIntType(argTypes[2])) {
            int a0 = 0, a1 = 0, a2 = 0;
            if (!asInt(arr[0], a0) || !asInt(arr[1], a1) || !asInt(arr[2], a2)) { sendErrorJson_(ctx, 400, "args parse failed"); return; }
            sw::ipc::Signal<int, int, int> sig(reg, sigName); ok = sig.publish(a0, a1, a2);
        } else if (argTypes.size() == 3 && isU64Type(argTypes[0]) && isStringType(argTypes[1]) && isStringType(argTypes[2])) {
            uint64_t a0 = 0; SwString a1, a2;
            if (!asU64(arr[0], a0) || !asString(arr[1], a1) || !asString(arr[2], a2)) { sendErrorJson_(ctx, 400, "args parse failed"); return; }
            sw::ipc::Signal<uint64_t, SwString, SwString> sig(reg, sigName); ok = sig.publish(a0, a1, a2);
        } else if (argTypes.size() == 3 && isBoolType(argTypes[0]) && isIntType(argTypes[1]) && isStringType(argTypes[2])) {
            bool a0 = false; int a1 = 0; SwString a2;
            if (!asBool(arr[0], a0) || !asInt(arr[1], a1) || !asString(arr[2], a2)) { sendErrorJson_(ctx, 400, "args parse failed"); return; }
            sw::ipc::Signal<bool, int, SwString> sig(reg, sigName); ok = sig.publish(a0, a1, a2);
        } else if (argTypes.size() == 3 && isIntType(argTypes[0]) && isFloatType(argTypes[1]) && isStringType(argTypes[2])) {
            int a0 = 0; double a1 = 0.0; SwString a2;
            if (!asInt(arr[0], a0) || !asDouble(arr[1], a1) || !asString(arr[2], a2)) { sendErrorJson_(ctx, 400, "args parse failed"); return; }
            sw::ipc::Signal<int, double, SwString> sig(reg, sigName); ok = sig.publish(a0, a1, a2);
        } else {
            sendErrorJson_(ctx, 400, "unsupported signal signature in web console (add a dispatcher case)");
            return;
        }

        SwJsonObject out;
        out["ok"] = SwJsonValue(ok);
        out["signal"] = SwJsonValue(sigName);
        out["typeName"] = SwJsonValue(typeName);
        ctx.json(SwJsonValue(out));
    });

    app_.post("/api/rpc", [](SwHttpContext& ctx) {
        static std::atomic<uint64_t> s_callId{1};

        SwJsonDocument d;
        SwString err;
        if (!ctx.parseJsonBody(d, err) || !d.isObject()) {
            sendErrorJson_(ctx, 400, SwString("invalid JSON: ") + err);
            return;
        }
        const SwJsonObject obj = d.object();
        const SwString target = obj["target"].toString();
        if (target.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: target");
            return;
        }
        const SwString method = obj["method"].toString();
        if (method.isEmpty()) {
            sendErrorJson_(ctx, 400, "missing field: method");
            return;
        }
        const SwJsonValue argsVal = obj["args"];
        if (!argsVal.isArray()) {
            sendErrorJson_(ctx, 400, "missing field: args (array)");
            return;
        }
        const SwJsonArray argsArr = argsVal.toArray();

        int timeoutMs = obj.contains("timeoutMs") ? obj["timeoutMs"].toInt() : 2000;
        if (timeoutMs <= 0) timeoutMs = 2000;

        const SwString clientInfo =
            obj.contains("clientInfo") ? SwString(obj["clientInfo"].toString()) : SwString("SwBridge");

        SwString domain, oName;
        if (!splitTarget_(target, domain, oName)) {
            sendErrorJson_(ctx, 400, "invalid target format");
            return;
        }

        const SwString reqSignal = SwString("__rpc__|") + method;
        RpcQueueInfo reqInfo;
        if (!findSignalInRegistryForTarget(domain, oName, reqSignal, reqInfo)) {
            sendErrorJson_(ctx, 404, "rpc request queue not found in registry (__rpc__|method)");
            return;
        }

        std::vector<std::string> reqTypes = parseArgTypesFromTypeName(reqInfo.typeName.toStdString());
        if (reqTypes.size() < 3) {
            sendErrorJson_(ctx, 400, "rpc: invalid request typeName (expected <callId,pid,clientInfo,...>)");
            return;
        }
        reqTypes.erase(reqTypes.begin(), reqTypes.begin() + 3);

        if (argsArr.size() != reqTypes.size()) {
            sendErrorJson_(ctx, 400, "rpc: args count mismatch vs registry signature");
            return;
        }

        const uint64_t callId = s_callId.fetch_add(1, std::memory_order_relaxed);
        const uint32_t pid = sw::ipc::detail::currentPid();

        std::array<uint8_t, RpcQueueAccess::kMaxPayload> tmp;
        sw::ipc::detail::Encoder enc(tmp.data(), tmp.size());
        if (!sw::ipc::detail::Codec<uint64_t>::write(enc, callId) ||
            !sw::ipc::detail::Codec<uint32_t>::write(enc, pid) ||
            !sw::ipc::detail::Codec<SwString>::write(enc, clientInfo)) {
            sendErrorJson_(ctx, 500, "rpc: encode header failed");
            return;
        }

        for (size_t i = 0; i < reqTypes.size(); ++i) {
            SwString perr;
            if (!encodeJsonArg(enc, reqTypes[i], argsArr[i], perr)) {
                sendErrorJson_(ctx, 400, perr);
                return;
            }
        }

        RpcQueueAccess reqQ;
        SwString qErr;
        if (!openRpcQueueAccess(reqInfo, reqQ, qErr)) {
            sendErrorJson_(ctx, 500, qErr);
            return;
        }
        if (!rpcQueuePushRaw(reqQ, tmp.data(), enc.size(), qErr)) {
            sendErrorJson_(ctx, 500, qErr);
            return;
        }

        const SwString respSignal = SwString("__rpc_ret__|") + method + "|" + SwString(std::to_string(pid));

        RpcQueueAccess respQ;
        bool haveRespQ = false;
        std::vector<std::string> respTypes;
        SwString retType;
        bool hasRet = false;

        bool done = false;
        bool ok = false;
        SwString rpcErr;
        SwJsonValue result;

        const auto t0 = std::chrono::steady_clock::now();
        const auto deadline = t0 + std::chrono::milliseconds(timeoutMs);

        while (!done) {
            if (!haveRespQ) {
                RpcQueueInfo respInfo;
                if (findSignalInRegistryForTarget(domain, oName, respSignal, respInfo)) {
                    SwString openErr;
                    if (!openRpcQueueAccess(respInfo, respQ, openErr)) {
                        sendErrorJson_(ctx, 500, openErr);
                        return;
                    }
                    respTypes = parseArgTypesFromTypeName(respInfo.typeName.toStdString());
                    hasRet = (respTypes.size() == 4);
                    retType = hasRet ? SwString(respTypes[3]) : SwString();
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
                    if (!sw::ipc::detail::Codec<uint64_t>::read(dec, gotCallId) ||
                        !sw::ipc::detail::Codec<bool>::read(dec, gotOk) ||
                        !sw::ipc::detail::Codec<SwString>::read(dec, gotErr)) {
                        continue;
                    }
                    if (gotCallId != callId) continue;

                    ok = gotOk;
                    rpcErr = gotErr;

                    if (ok && hasRet) {
                        SwString derr;
                        if (!decodeJsonValueByType(dec, retType.toStdString(), result, derr)) {
                            sendErrorJson_(ctx, 500, derr);
                            return;
                        }
                    }
                    done = true;
                    break;
                }
            }

            if (done) break;

            if (std::chrono::steady_clock::now() >= deadline) {
                sendErrorJson_(ctx, 504, "rpc: timeout");
                return;
            }

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

        SwJsonObject out;
        out["ok"] = SwJsonValue(ok);
        out["method"] = SwJsonValue(method);
        out["callId"] = SwJsonValue(std::to_string(callId));
        out["returnType"] = SwJsonValue(retType);
        if (!ok) {
            out["error"] = SwJsonValue(rpcErr);
        } else if (hasRet) {
            out["result"] = result;
        }
        ctx.json(SwJsonValue(out));
    });
}

// ---------------------------------------------------------------------------
// IPC helpers
// ---------------------------------------------------------------------------

bool SwBridgeHttpServer::splitTarget_(const SwString& fqn, SwString& nsOut, SwString& objOut) {
    SwString x = fqn;
    x.replace("\\", "/");
    std::string s = x.toStdString();
    while (!s.empty() && s[0] == '/') s.erase(0, 1);
    while (!s.empty() && s[s.size() - 1] == '/') s.erase(s.size() - 1);
    if (s.empty()) return false;
    const size_t slash = s.find('/');
    if (slash == std::string::npos) {
        nsOut = "";
        objOut = SwString(s);
        return true;
    }
    nsOut = SwString(s.substr(0, slash));
    objOut = SwString(s.substr(slash + 1));
    return !nsOut.isEmpty() && !objOut.isEmpty();
}

bool SwBridgeHttpServer::ipcSendPing_(const SwString& target, int n, const SwString& s) {
    SwString ns, obj;
    if (!splitTarget_(target, ns, obj)) return false;
    sw::ipc::Registry reg(ns, obj);
    sw::ipc::Signal<int, SwString> sig(reg, "ping");
    return sig.publish(n, s);
}

bool SwBridgeHttpServer::ipcSendConfigRaw_(const SwString& target, const SwString& configName, const SwString& value) {
    SwString ns, obj;
    if (!splitTarget_(target, ns, obj)) return false;
    sw::ipc::Registry reg(ns, obj);
    sw::ipc::Signal<uint64_t, SwString> sig(reg, SwString("__cfg__|") + configName);
    return sig.publish(0, value);
}

void SwBridgeHttpServer::ensureTargetSubscriptions_(const SwString& target) {
    if (target.isEmpty()) return;
    if (target == subscribedTarget_ && reg_) return;

    subscribedTarget_ = target;
    lastPong_.clear();
    lastConfigAck_.clear();

    SwString ns, obj;
    if (!splitTarget_(target, ns, obj)) return;

    reg_.reset(new sw::ipc::Registry(ns, obj));

    pongSig_.reset(new sw::ipc::Signal<int, SwString>(*reg_, "pong"));
    cfgAckSig_.reset(new sw::ipc::Signal<uint64_t, SwString>(*reg_, "configAck"));

    pongSub_.stop();
    cfgAckSub_.stop();

    pongSub_ = pongSig_->connect([this](int n, SwString s) {
        std::ostringstream oss;
        oss << "n=" << n << " s=" << s.toStdString();
        lastPong_ = SwString(oss.str());
        ++stateSeq_;
        wsBroadcastState_();
        std::cout << "[SwBridge] pong: " << lastPong_.toStdString() << "\n";
    }, /*fireInitial=*/false, /*timeoutMs=*/0);

    cfgAckSub_ = cfgAckSig_->connect([this](uint64_t requesterId, SwString cfgName) {
        std::ostringstream oss;
        oss << "requesterId=" << requesterId << " cfg=" << cfgName.toStdString();
        lastConfigAck_ = SwString(oss.str());
        ++stateSeq_;
        wsBroadcastState_();
        std::cout << "[SwBridge] configAck: " << lastConfigAck_.toStdString() << "\n";
    }, /*fireInitial=*/false, /*timeoutMs=*/0);
}

// ---------------------------------------------------------------------------
// WebSocket (on wsPort_)
// ---------------------------------------------------------------------------

void SwBridgeHttpServer::onWsNewConnection_() {
    if (!wsServer_) return;
    SwTcpSocket* client = wsServer_->nextPendingConnection();
    if (!client) return;

    // The WebSocket server expects raw HTTP upgrade as the first message.
    // We do a minimal inline parse of the upgrade request before switching to WS mode.
    SwTimer* pollTimer = new SwTimer(5, this);
    wsPollTimers_.insert(client, pollTimer);

    // Accumulate data in a temporary SwByteArray stored in a WsConnState with a special marker.
    WsConnState st;
    wsConns_.insert(client, st);

    connect(client, &SwTcpSocket::disconnected, [this, client]() {
        onWsClientDisconnected_(client);
    });

    connect(client, &SwTcpSocket::readyRead, [this, client]() {
        onWsClientData_(client);
    });

    connect(pollTimer, &SwTimer::timeout, [this, client]() {
        onWsClientData_(client);
    });
    pollTimer->start(5);
}

void SwBridgeHttpServer::onWsClientDisconnected_(SwTcpSocket* client) {
    // Stop and clean poll timer
    SwMap<SwTcpSocket*, SwTimer*>::iterator tit = wsPollTimers_.find(client);
    if (tit != wsPollTimers_.end()) {
        if (tit->second) {
            tit->second->stop();
            tit->second->deleteLater();
        }
        wsPollTimers_.erase(tit);
    }
    wsConns_.remove(client);
    client->deleteLater();
}

void SwBridgeHttpServer::onWsClientData_(SwTcpSocket* client) {
    if (!client) return;

    SwMap<SwTcpSocket*, WsConnState>::iterator it = wsConns_.find(client);
    if (it == wsConns_.end()) return;
    WsConnState& st = it->second;

    // Read available data
    while (true) {
        const SwString chunk = client->read();
        if (chunk.isEmpty()) break;
        st.buffer.append(chunk.data(), chunk.size());
    }

    // If fragOpcode == 0xFF, we haven't done the HTTP upgrade yet (special marker).
    // Actually, let's use a simpler approach: check if the buffer starts with "GET " (HTTP request).
    // If we haven't upgraded yet (no fragActive, fragOpcode == 0, and buffer starts with GET):
    if (st.fragOpcode == 0 && !st.fragActive && st.buffer.size() >= 4) {
        // Check for HTTP upgrade request
        const std::string bufStr(st.buffer.constData(), st.buffer.size());
        const size_t headerEnd = bufStr.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return; // Need more data

        // Parse the upgrade request minimally
        if (bufStr.find("Upgrade: websocket") == std::string::npos &&
            bufStr.find("Upgrade: WebSocket") == std::string::npos &&
            bufStr.find("upgrade: websocket") == std::string::npos) {
            client->close();
            return;
        }

        // Extract Sec-WebSocket-Key
        SwString keyLine;
        {
            std::string line;
            std::istringstream iss(bufStr.substr(0, headerEnd));
            while (std::getline(iss, line)) {
                // Trim \r
                while (!line.empty() && line.back() == '\r') line.pop_back();
                std::string lower = line;
                for (size_t ci = 0; ci < lower.size(); ++ci) {
                    if (lower[ci] >= 'A' && lower[ci] <= 'Z') lower[ci] = lower[ci] - 'A' + 'a';
                }
                if (lower.find("sec-websocket-key:") == 0) {
                    std::string val = line.substr(std::strlen("sec-websocket-key:"));
                    while (!val.empty() && val[0] == ' ') val.erase(0, 1);
                    keyLine = SwString(val);
                    break;
                }
            }
        }

        if (keyLine.isEmpty()) {
            client->close();
            return;
        }

        // Send 101 Switching Protocols
        SwString response = "HTTP/1.1 101 Switching Protocols\r\n";
        response += "Upgrade: websocket\r\n";
        response += "Connection: Upgrade\r\n";
        response += "Sec-WebSocket-Accept: " + computeAcceptKey(keyLine) + "\r\n";
        response += "\r\n";
        client->write(response);

        // Move remaining bytes after the HTTP header
        const size_t consumed = headerEnd + 4;
        SwByteArray remaining;
        if (consumed < st.buffer.size()) {
            remaining = st.buffer.mid(static_cast<int>(consumed));
        }
        st.buffer = remaining;

        // Mark as upgraded: set fragActive temporarily to indicate WS mode
        st.fragOpcode = 0;
        st.fragActive = false;

        // Slow down poll timer for WS mode
        SwMap<SwTcpSocket*, SwTimer*>::iterator tit = wsPollTimers_.find(client);
        if (tit != wsPollTimers_.end() && tit->second) {
            tit->second->stop();
            tit->second->start(50);
        }

        // Send initial state
        wsSendState_(client);
    }

    // Process WebSocket frames
    while (true) {
        uint8_t opcode = 0;
        bool fin = false;
        bool needMore = false;
        SwByteArray payload;
        if (!parseClientFrame(st.buffer, opcode, fin, payload, needMore)) {
            if (needMore) return;
            client->close();
            return;
        }

        if (opcode == 0x9 /* ping */) {
            client->write(buildServerFrame(0xA /* pong */, payload));
            continue;
        }
        if (opcode == 0xA /* pong */) {
            continue;
        }
        if (opcode == 0x8 /* close */) {
            client->write(buildServerFrame(0x8 /* close */, payload));
            SwTcpSocket* sock = client;
            SwTimer::singleShot(50, [sock]() {
                if (sock) sock->close();
            });
            return;
        }

        if (opcode == 0x0 /* continuation */) {
            if (!st.fragActive) {
                client->close();
                return;
            }
            st.frag.append(payload);
            if (!fin) continue;
            opcode = st.fragOpcode;
            payload = st.frag;
            st.frag.clear();
            st.fragActive = false;
            st.fragOpcode = 0;
            fin = true;
        } else if ((opcode == 0x1 || opcode == 0x2) && !fin) {
            if (st.fragActive) {
                client->close();
                return;
            }
            st.fragActive = true;
            st.fragOpcode = opcode;
            st.frag = payload;
            continue;
        }

        if (opcode != 0x1 /* text */) continue;

        const SwString text(payload);
        SwJsonDocument doc;
        SwString err;
        if (!doc.loadFromJson(text, err) || !doc.isObject()) continue;

        SwJsonObject msg(doc.object());
        const SwString type = SwString(msg["type"].toString());
        if (type == "setTarget") {
            const SwString tgt = SwString(msg["target"].toString());
            st.target = tgt;
            if (!tgt.isEmpty()) ensureTargetSubscriptions_(tgt);
            wsSendState_(client);
            continue;
        }
        if (type == "getState") {
            wsSendState_(client);
            continue;
        }
        if (type == "ping") {
            SwJsonObject out;
            out["type"] = SwJsonValue("pong");
            SwJsonDocument outDoc(out);
            client->write(buildServerFrame(0x1, SwByteArray(outDoc.toJson().toStdString())));
            continue;
        }
    }
}

void SwBridgeHttpServer::wsSendState_(SwTcpSocket* client) {
    if (!client) return;

    SwJsonObject o;
    o["type"] = SwJsonValue("state");
    o["seq"] = SwJsonValue(std::to_string(stateSeq_));
    o["target"] = SwJsonValue(subscribedTarget_);
    o["lastPong"] = SwJsonValue(lastPong_);
    o["lastConfigAck"] = SwJsonValue(lastConfigAck_);

    SwJsonDocument d(o);
    client->write(buildServerFrame(0x1, SwByteArray(d.toJson().toStdString())));
}

void SwBridgeHttpServer::wsBroadcastState_() {
    SwJsonObject o;
    o["type"] = SwJsonValue("state");
    o["seq"] = SwJsonValue(std::to_string(stateSeq_));
    o["target"] = SwJsonValue(subscribedTarget_);
    o["lastPong"] = SwJsonValue(lastPong_);
    o["lastConfigAck"] = SwJsonValue(lastConfigAck_);

    SwJsonDocument d(o);
    const SwByteArray frame = buildServerFrame(0x1, SwByteArray(d.toJson().toStdString()));

    for (SwMap<SwTcpSocket*, WsConnState>::iterator it = wsConns_.begin(); it != wsConns_.end(); ++it) {
        SwTcpSocket* sock = it->first;
        const WsConnState& st = it->second;
        if (!sock) continue;
        if (!st.target.isEmpty() && st.target != subscribedTarget_) continue;
        sock->write(frame);
    }
}
