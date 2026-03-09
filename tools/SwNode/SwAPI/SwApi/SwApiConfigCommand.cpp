#include "SwApiConfigCommand.h"

#include "SwApiJson.h"

#include "SwDir.h"
#include "SwFile.h"
#include "SwSharedMemorySignal.h"
#include "SwTimer.h"

#include <iostream>

static SwString sanitizeSegmentForFile_(const SwString& in) {
    std::string s = in.toStdString();
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_') || (c == '-') ||
                        (c == '.');
        if (!ok) s[i] = '_';
    }
    if (s.empty()) s = "root";
    return SwString(s);
}

static SwString sanitizeNsForFile_(const SwString& nsIn) {
    std::string s = nsIn.toStdString();
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '/' || c == '\\') s[i] = '_';
    }
    while (!s.empty() && (s.front() == '_' || s.front() == '/')) s.erase(0, 1);
    while (!s.empty() && (s.back() == '_' || s.back() == '/')) s.pop_back();
    if (s.empty()) s = "root";
    return sanitizeSegmentForFile_(SwString(s));
}

static SwString configRootAbsolute_(const SwString& root = "systemConfig") {
    const SwString r = root.isEmpty() ? SwString("systemConfig") : root;
    const std::string s = r.toStdString();
    const bool isAbs = (!s.empty() && (s[0] == '/' || s[0] == '\\')) || (s.size() > 1 && s[1] == ':');
    if (isAbs) return r;
    return SwDir::currentPath() + r;
}

static bool loadDocObject_(const SwString& path, SwJsonDocument& outDoc) {
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

static void mergeValueDeep_(SwJsonValue& target, const SwJsonValue& src);

static void mergeObjectDeep_(SwJsonObject& target, const SwJsonObject& src) {
    SwJsonObject::Container data = src.data();
    for (SwJsonObject::Container::const_iterator it = data.begin(); it != data.end(); ++it) {
        const SwString k(it->first);
        if (target.contains(k) && target[k].isObject() && it->second.isObject()) {
            SwJsonValue tv = target[k];
            mergeValueDeep_(tv, it->second);
            target[k] = tv;
        } else {
            target.insert(it->first, it->second);
        }
    }
}

static void mergeValueDeep_(SwJsonValue& target, const SwJsonValue& src) {
    if (target.isObject() && src.isObject()) {
        SwJsonObject t(target.toObject());
        SwJsonObject s(src.toObject());
        mergeObjectDeep_(t, s);
        target = SwJsonValue(t);
        return;
    }
    target = src;
}

static SwJsonObject loadMergedConfigForTarget_(const SwString& sysName, const SwString& objectFqn) {
    const SwString root = configRootAbsolute_("systemConfig");
    const SwString nsFile = sanitizeNsForFile_(sysName);
    const SwString objFile = sanitizeSegmentForFile_(objectFqn);

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
    const SwString objLeafFile = sanitizeSegmentForFile_(objLeaf);

    const SwString globalPath = root + "/global/" + objLeafFile + ".json";
    const SwString localPath  = root + "/local/" + nsFile + "_" + objFile + ".json";
    const SwString userPath   = root + "/user/"  + nsFile + "_" + objFile + ".json";

    SwJsonDocument gd(SwJsonObject{}), ld(SwJsonObject{}), ud(SwJsonObject{});
    SwJsonDocument tmp;
    if (loadDocObject_(globalPath, tmp)) gd = tmp;
    if (loadDocObject_(localPath, tmp))  ld = tmp;
    if (loadDocObject_(userPath, tmp))   ud = tmp;

    SwJsonObject merged;
    if (gd.isObject()) mergeObjectDeep_(merged, gd.object());
    if (ld.isObject()) mergeObjectDeep_(merged, ld.object());
    if (ud.isObject()) mergeObjectDeep_(merged, ud.object());
    return merged;
}

SwApiConfigCommand::SwApiConfigCommand(const SwApiCli& cli,
                                       SwApiIpcInspector& inspector,
                                       const SwStringList& args,
                                       SwObject* parent)
    : SwApiCommand(cli, inspector, args, parent) {}

SwApiConfigCommand::~SwApiConfigCommand() = default;

void SwApiConfigCommand::printUsage_() const {
    std::cerr
        << "Usage:\n"
        << "  swapi config dump <target> [--domain <sys>] [--pretty] [--json]\n"
        << "  swapi config get <target> <path> [--domain <sys>] [--pretty] [--json]\n"
        << "  swapi config set <target> <path> <value> [--domain <sys>] [--json]\n"
        << "  swapi config send-all <target> (--file <path> | --config <json>) [--domain <sys>] [--json] [--pretty]\n"
        << "  swapi config watch <target> [path] [--domain <sys>] [--duration_ms <ms>] [--no-initial]\n"
        << "\n"
        << "Notes:\n"
        << "  <path> is a '/'-separated key path (arrays supported with numeric segments).\n";
}

static int printConfigJson(const SwString& json, bool pretty, bool jsonOut) {
    SwJsonDocument doc;
    SwString err;
    if (!doc.loadFromJson(json.toStdString(), err)) {
        std::cerr << "swapi config: invalid JSON from target: " << err.toStdString() << "\n";
        return 3;
    }

    const SwString out = doc.toJson(pretty ? SwJsonDocument::JsonFormat::Pretty : SwJsonDocument::JsonFormat::Compact);
    std::cout << out.toStdString() << "\n";
    return jsonOut ? 0 : 0;
}

int SwApiConfigCommand::cmdDump_() {
    const bool pretty = cli().hasFlag("pretty");
    const bool jsonOut = cli().hasFlag("json");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 2) {
        std::cerr << "swapi config dump: missing <target>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi config dump: " << err.toStdString() << "\n";
        return 2;
    }

    SwString json;
    uint64_t pubId = 0;
    if (!inspector().readConfigDocJson(target, json, pubId, err)) {
        const SwJsonObject merged = loadMergedConfigForTarget_(target.domain, target.object);
        const SwString out = SwApiJson::toJson(merged, pretty);
        std::cout << out.toStdString() << "\n";
        return jsonOut ? 0 : 0;
    }

    return printConfigJson(json, pretty, jsonOut);
}

int SwApiConfigCommand::cmdGet_() {
    const bool pretty = cli().hasFlag("pretty");
    const bool jsonOut = cli().hasFlag("json");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 3) {
        std::cerr << "swapi config get: missing <target> and/or <path>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi config get: " << err.toStdString() << "\n";
        return 2;
    }

    const SwString path = args()[2];

    SwString raw;
    uint64_t pubId = 0;
    if (!inspector().readConfigDocJson(target, raw, pubId, err)) {
        const SwJsonObject merged = loadMergedConfigForTarget_(target.domain, target.object);
        SwJsonValue v;
        SwString perr;
        if (!SwApiJson::tryGetPath(SwJsonValue(merged), path, v, perr)) {
            std::cerr << "swapi config get: " << perr.toStdString() << "\n";
            return 3;
        }
        std::cout << SwApiJson::toJson(v, pretty).toStdString() << "\n";
        return jsonOut ? 0 : 0;
    }

    SwJsonDocument doc;
    if (!doc.loadFromJson(raw.toStdString(), err)) {
        std::cerr << "swapi config get: invalid JSON from target: " << err.toStdString() << "\n";
        return 3;
    }

    SwJsonValue v;
    if (!SwApiJson::tryGetPath(doc.toJsonValue(), path, v, err)) {
        std::cerr << "swapi config get: " << err.toStdString() << "\n";
        return 3;
    }

    std::cout << SwApiJson::toJson(v, pretty).toStdString() << "\n";
    return jsonOut ? 0 : 0;
}

static bool readAllFile_(const SwString& path, SwString& out) {
    out.clear();
    if (!SwFile::isFile(path)) return false;
    SwFile f(path);
    if (!f.open(SwFile::Read)) return false;
    out = f.readAll();
    f.close();
    return true;
}

int SwApiConfigCommand::cmdSendAll_() {
    const bool jsonOut = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));
    const int timeoutMs = cli().intValue("timeout_ms", 2000);

    if (args().size() < 2) {
        std::cerr << "swapi config send-all: missing <target>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi config send-all: " << err.toStdString() << "\n";
        return 2;
    }

    SwString cfgSig;
    if (!inspector().findConfigDocSignal(target, cfgSig)) {
        std::cerr << "swapi config send-all: target does not expose a __config__|* signal\n";
        return 3;
    }

    SwString raw;
    const SwString filePath = cli().value("file", SwString());
    const SwString inlineJson = cli().value("config", SwString());
    if (!filePath.isEmpty()) {
        if (!readAllFile_(filePath, raw)) {
            std::cerr << "swapi config send-all: failed to read file: " << filePath.toStdString() << "\n";
            return 2;
        }
    } else if (!inlineJson.isEmpty()) {
        raw = inlineJson;
    } else {
        std::cerr << "swapi config send-all: missing --file <path> or --config <json>\n";
        return 2;
    }

    SwJsonObject cfgObj;
    if (!SwApiJson::parseObject(raw, cfgObj, err)) {
        std::cerr << "swapi config send-all: invalid JSON object: " << err.toStdString() << "\n";
        return 2;
    }

    SwJsonDocument doc;
    doc.setObject(cfgObj);
    const SwString compact = doc.toJson(SwJsonDocument::JsonFormat::Compact);

    bool ok = false;
    try {
        sw::ipc::Registry reg(target.domain, target.object);
        sw::ipc::Signal<uint64_t, SwString> sig(reg, cfgSig);
        ok = sig.publish(0, compact);
    } catch (...) {
        ok = false;
    }

    if (jsonOut) {
        SwJsonObject o;
        o["ok"] = SwJsonValue(ok);
        o["target"] = SwJsonValue(target.toString().toStdString());
        o["configSignal"] = SwJsonValue(cfgSig.toStdString());
        o["timeoutMs"] = SwJsonValue(timeoutMs);
        std::cout << SwApiJson::toJson(o, pretty).toStdString() << "\n";
    } else if (!ok) {
        std::cerr << "swapi config send-all: publish failed\n";
    } else {
        std::cout << "ok\n";
    }

    return ok ? 0 : 3;
}

int SwApiConfigCommand::cmdSet_() {
    const bool jsonOut = cli().hasFlag("json");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 4) {
        std::cerr << "swapi config set: missing <target> <path> <value>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi config set: " << err.toStdString() << "\n";
        return 2;
    }

    const SwString path = args()[2];
    const SwString value = args()[3];

    const bool ok = inspector().publishConfigValue(target, path, value, err);
    if (jsonOut) {
        SwJsonObject o;
        o["ok"] = SwJsonValue(ok);
        o["target"] = SwJsonValue(target.toString().toStdString());
        o["path"] = SwJsonValue(path.toStdString());
        o["value"] = SwJsonValue(value.toStdString());
        if (!err.isEmpty()) o["error"] = SwJsonValue(err.toStdString());
        std::cout << SwApiJson::toJson(o, cli().hasFlag("pretty")).toStdString() << "\n";
    } else if (!ok) {
        std::cerr << "swapi config set: " << err.toStdString() << "\n";
    }

    return ok ? 0 : 3;
}

void SwApiConfigCommand::cmdWatch_() {
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));
    const int durationMs = cli().intValue("duration_ms", 0);
    const bool fireInitial = !cli().hasFlag("no-initial");

    if (args().size() < 2) {
        std::cerr << "swapi config watch: missing <target>\n";
        finish(2);
        return;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi config watch: " << err.toStdString() << "\n";
        finish(2);
        return;
    }

    SwString cfgSig;
    if (!inspector().findConfigDocSignal(target, cfgSig)) {
        std::cerr << "swapi config watch: target does not expose a __config__|* signal\n";
        finish(3);
        return;
    }

    const bool pretty = cli().hasFlag("pretty");
    const SwString path = (args().size() >= 3) ? args()[2] : SwString();

    sw::ipc::Registry reg(target.domain, target.object);
    sw::ipc::Signal<uint64_t, SwString> sig(reg, cfgSig);

    auto sub = sig.connect([path, pretty](uint64_t pubId, SwString json) {
        (void)pubId;
        if (path.isEmpty()) {
            (void)printConfigJson(json, pretty, /*jsonOut=*/true);
            return;
        }

        SwJsonDocument doc;
        SwString err;
        if (!doc.loadFromJson(json.toStdString(), err)) {
            std::cerr << "swapi config watch: invalid JSON: " << err.toStdString() << "\n";
            return;
        }

        SwJsonValue v;
        if (!SwApiJson::tryGetPath(doc.toJsonValue(), path, v, err)) {
            std::cerr << "swapi config watch: " << err.toStdString() << "\n";
            return;
        }

        std::cout << SwApiJson::toJson(v, pretty).toStdString() << "\n";
    }, fireInitial, /*timeoutMs=*/0);
    subscription_.emplace(std::move(sub));

    if (durationMs > 0) {
        SwTimer::singleShot(durationMs, [this]() { finish(0); });
    }
}

void SwApiConfigCommand::start() {
    const SwStringList& a = args();
    if (a.isEmpty()) {
        printUsage_();
        finish(2);
        return;
    }

    const SwString sub = a[0];
    if (sub == "watch") {
        cmdWatch_();
        return; // async
    }

    int code = 2;
    if (sub == "dump") code = cmdDump_();
    else if (sub == "get") code = cmdGet_();
    else if (sub == "set") code = cmdSet_();
    else if (sub == "send-all" || sub == "sendall") code = cmdSendAll_();
    else {
        printUsage_();
        finish(2);
        return;
    }

    finish(code);
}
