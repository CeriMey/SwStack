#include "SwApiSignalsCommand.h"

#include "SwApiJson.h"

#include "SwSharedMemorySignal.h"
#include "SwTimer.h"
#include "types/SwByteArray.h"

#include <cstdlib>
#include <cctype>
#include <iostream>
#include <sstream>

SwApiSignalsCommand::SwApiSignalsCommand(const SwApiCli& cli,
                                         SwApiIpcInspector& inspector,
                                         const SwStringList& args,
                                         SwObject* parent)
    : SwApiCommand(cli, inspector, args, parent) {}

SwApiSignalsCommand::~SwApiSignalsCommand() = default;

void SwApiSignalsCommand::printUsage_() const {
    std::cerr
        << "Usage:\n"
        << "  swapi signal list <target> [--domain <sys>] [--json] [--pretty]\n"
        << "  swapi signal echo <target> <signal> [--domain <sys>] [--duration_ms <ms>] [--no-initial]\n"
        << "  swapi signal publish <target> <signal> [args...] [--domain <sys>] [--args <json-array>] [--json] [--pretty]\n";
}

int SwApiSignalsCommand::cmdList_() {
    const bool json = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 2) {
        std::cerr << "swapi signal list: missing <target>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi signal list: " << err.toStdString() << "\n";
        return 2;
    }

    SwJsonArray sigs = inspector().signalsForTarget(target, /*includeStale=*/false);
    if (json) {
        std::cout << SwApiJson::toJson(sigs, pretty).toStdString() << "\n";
        return 0;
    }

    for (size_t i = 0; i < sigs.size(); ++i) {
        const SwJsonValue v = sigs[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        std::cout << SwString(o["signal"].toString()).toStdString() << "\n";
    }
    return 0;
}

static bool isU64Type(const SwString& t) { return t == "uint64_t" || t == "unsigned __int64" || t == "unsigned long long"; }

static bool isIntType(const SwString& t) { return t == "int" || t == "int32_t" || t == "signed int"; }

static bool isBoolType(const SwString& t) { return t == "bool" || t == "BOOL"; }

static bool isDoubleType(const SwString& t) { return t == "double" || t == "float"; }

static bool isStringType(const SwString& t) { return t == "SwString" || t == "class SwString"; }

static bool isBytesType(const SwString& t) { return t == "SwByteArray" || t == "class SwByteArray"; }

static SwStringList parseArgTypesFromTypeName(const SwString& typeName) {
    // Best-effort parser for the MSVC-like "<...>" chunk.
    SwStringList out;
    const int lt = typeName.indexOf("<");
    const int gt = (lt < 0) ? -1 : typeName.indexOf(">", static_cast<size_t>(lt + 1));
    if (lt < 0 || gt < 0 || gt <= lt + 1) return out;

    SwString inside = typeName.mid(lt + 1, gt - lt - 1);
    const int insideLen = static_cast<int>(inside.size());

    int start = 0;
    while (start < insideLen) {
        const int comma = inside.indexOf(",", static_cast<size_t>(start));
        const int end = (comma < 0) ? insideLen : comma;

        SwString token = inside.mid(start, end - start).trimmed();
        if (token.startsWith("class ")) token = token.mid(static_cast<int>(SwString("class ").size()));
        if (token.startsWith("struct ")) token = token.mid(static_cast<int>(SwString("struct ").size()));
        token = token.trimmed();
        if (!token.isEmpty()) out.append(token);

        if (comma < 0) break;
        start = comma + 1;
    }
    return out;
}

static bool parseBool(const SwString& s, bool& out) {
    const SwString x = s.trimmed().toLower();
    if (x == "1" || x == "true" || x == "yes" || x == "y" || x == "on") { out = true; return true; }
    if (x == "0" || x == "false" || x == "no" || x == "n" || x == "off") { out = false; return true; }
    return false;
}

static bool parseU64(const SwString& s, uint64_t& out) {
    const std::string x = s.trimmed().toStdString();
    if (x.empty()) return false;
    char* end = NULL;
    const unsigned long long v = std::strtoull(x.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<uint64_t>(v);
    return true;
}

static bool parseInt(const SwString& s, int& out) {
    bool ok = false;
    const int v = s.trimmed().toInt(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

static bool parseDouble(const SwString& s, double& out) {
    bool ok = false;
    const double v = s.trimmed().toDouble(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

static bool asBool(const SwJsonValue& v, bool& out) {
    if (v.isBool()) { out = v.toBool(); return true; }
    if (v.isInt()) { out = (v.toInt() != 0); return true; }
    if (v.isDouble()) { out = (v.toDouble() != 0.0); return true; }
    if (v.isString()) return parseBool(SwString(v.toString()), out);
    return false;
}

static bool asInt(const SwJsonValue& v, int& out) {
    if (v.isInt()) { out = v.toInt(); return true; }
    if (v.isDouble()) { out = static_cast<int>(v.toDouble()); return true; }
    if (v.isBool()) { out = v.toBool() ? 1 : 0; return true; }
    if (v.isString()) return parseInt(SwString(v.toString()), out);
    return false;
}

static bool asU64(const SwJsonValue& v, uint64_t& out) {
    if (v.isInt()) { out = static_cast<uint64_t>(v.toInt()); return true; }
    if (v.isDouble()) { out = static_cast<uint64_t>(v.toDouble()); return true; }
    if (v.isBool()) { out = v.toBool() ? 1ull : 0ull; return true; }
    if (v.isString()) return parseU64(SwString(v.toString()), out);
    return false;
}

static bool asDouble(const SwJsonValue& v, double& out) {
    if (v.isDouble()) { out = v.toDouble(); return true; }
    if (v.isInt()) { out = static_cast<double>(v.toInt()); return true; }
    if (v.isBool()) { out = v.toBool() ? 1.0 : 0.0; return true; }
    if (v.isString()) return parseDouble(SwString(v.toString()), out);
    return false;
}

static bool asString(const SwJsonValue& v, SwString& out) {
    if (v.isString()) { out = SwString(v.toString()); return true; }
    if (v.isBool()) { out = v.toBool() ? "true" : "false"; return true; }
    if (v.isInt()) { out = SwString::number(v.toInt()); return true; }
    if (v.isDouble()) {
        std::ostringstream oss;
        oss << v.toDouble();
        out = SwString(oss.str());
        return true;
    }
    return false;
}

static bool asBytes(const SwJsonValue& v, SwByteArray& out) {
    if (v.isString()) {
        out = SwByteArray(SwString(v.toString()).toStdString());
        return true;
    }
    return false;
}

void SwApiSignalsCommand::cmdEcho_() {
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));
    const int durationMs = cli().intValue("duration_ms", 0);
    const bool fireInitial = !cli().hasFlag("no-initial");

    if (args().size() < 3) {
        std::cerr << "swapi signal echo: missing <target> <signal>\n";
        finish(2);
        return;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi signal echo: " << err.toStdString() << "\n";
        finish(2);
        return;
    }

    const SwString sigName = args()[2];

    // Find typeName in registry to choose a supported signature.
    const SwJsonArray sigs = inspector().signalsForTarget(target, /*includeStale=*/false);
    SwString typeName;
    for (size_t i = 0; i < sigs.size(); ++i) {
        const SwJsonValue v = sigs[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["signal"].toString()) != sigName) continue;
        typeName = SwString(o["typeName"].toString());
        break;
    }

    if (typeName.isEmpty()) {
        std::cerr << "swapi signal echo: signal not found in registry: " << sigName.toStdString() << "\n";
        finish(3);
        return;
    }

    const SwStringList types = parseArgTypesFromTypeName(typeName);
    sw::ipc::Registry reg(target.domain, target.object);

    if (types.size() == 1 && isBoolType(types[0])) {
        sw::ipc::Signal<bool> sig(reg, sigName);
        auto sub = sig.connect([sigName](bool a0) { std::cout << sigName.toStdString() << " " << (a0 ? "true" : "false") << "\n"; }, fireInitial, 0);
        subscription_.emplace(std::move(sub));
    } else if (types.size() == 1 && isIntType(types[0])) {
        sw::ipc::Signal<int> sig(reg, sigName);
        auto sub = sig.connect([sigName](int a0) { std::cout << sigName.toStdString() << " " << a0 << "\n"; }, fireInitial, 0);
        subscription_.emplace(std::move(sub));
    } else if (types.size() == 1 && isDoubleType(types[0])) {
        sw::ipc::Signal<double> sig(reg, sigName);
        auto sub = sig.connect([sigName](double a0) { std::cout << sigName.toStdString() << " " << a0 << "\n"; }, fireInitial, 0);
        subscription_.emplace(std::move(sub));
    } else if (types.size() == 1 && isStringType(types[0])) {
        sw::ipc::Signal<SwString> sig(reg, sigName);
        auto sub = sig.connect([sigName](SwString a0) { std::cout << sigName.toStdString() << " " << a0.toStdString() << "\n"; }, fireInitial, 0);
        subscription_.emplace(std::move(sub));
    } else if (types.size() == 1 && isU64Type(types[0])) {
        sw::ipc::Signal<uint64_t> sig(reg, sigName);
        auto sub = sig.connect([sigName](uint64_t a0) { std::cout << sigName.toStdString() << " " << a0 << "\n"; }, fireInitial, 0);
        subscription_.emplace(std::move(sub));
    } else if (types.size() == 2 && isU64Type(types[0]) && isStringType(types[1])) {
        sw::ipc::Signal<uint64_t, SwString> sig(reg, sigName);
        auto sub = sig.connect([sigName](uint64_t a0, SwString a1) { std::cout << sigName.toStdString() << " " << a0 << " " << a1.toStdString() << "\n"; }, fireInitial, 0);
        subscription_.emplace(std::move(sub));
    } else if (types.size() == 2 && isIntType(types[0]) && isStringType(types[1])) {
        sw::ipc::Signal<int, SwString> sig(reg, sigName);
        auto sub = sig.connect([sigName](int a0, SwString a1) { std::cout << sigName.toStdString() << " " << a0 << " " << a1.toStdString() << "\n"; }, fireInitial, 0);
        subscription_.emplace(std::move(sub));
    } else {
        std::cerr << "swapi signal echo: unsupported signature (extend in SwBridge)\n";
        finish(3);
        return;
    }

    if (durationMs > 0) {
        SwTimer::singleShot(durationMs, [this]() { finish(0); });
    }
}

int SwApiSignalsCommand::cmdPublish_() {
    const bool jsonOut = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 3) {
        std::cerr << "swapi signal publish: missing <target> <signal>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi signal publish: " << err.toStdString() << "\n";
        return 2;
    }

    const SwString sigName = args()[2];

    // Find typeName in registry to choose a supported signature.
    const SwJsonArray sigs = inspector().signalsForTarget(target, /*includeStale=*/false);
    SwString typeName;
    for (size_t i = 0; i < sigs.size(); ++i) {
        const SwJsonValue v = sigs[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["signal"].toString()) != sigName) continue;
        typeName = SwString(o["typeName"].toString());
        break;
    }

    if (typeName.isEmpty()) {
        std::cerr << "swapi signal publish: signal not found in registry: " << sigName.toStdString() << "\n";
        return 3;
    }

    const SwStringList types = parseArgTypesFromTypeName(typeName);

    SwJsonArray argsArr;
    const SwString argsJson = cli().value("args", SwString());
    if (!argsJson.isEmpty()) {
        if (!SwApiJson::parseArray(argsJson, argsArr, err)) {
            std::cerr << "swapi signal publish: invalid --args JSON array: " << err.toStdString() << "\n";
            return 2;
        }
    } else {
        // args()[0]=publish args()[1]=target args()[2]=signal args()[3...]=args
        for (size_t i = 3; i < args().size(); ++i) {
            argsArr.append(SwJsonValue(args()[i].toStdString()));
        }
    }

    const size_t argc = argsArr.size();
    if (argc != types.size()) {
        std::cerr << "swapi signal publish: args count mismatch vs registry signature\n";
        return 2;
    }

    bool ok = false;
    try {
        sw::ipc::Registry reg(target.domain, target.object);

        if (types.size() == 1 && isBoolType(types[0])) {
            bool a0 = false;
            if (!asBool(argsArr[0], a0)) { std::cerr << "swapi signal publish: arg0 bool parse failed\n"; return 2; }
            sw::ipc::Signal<bool> sig(reg, sigName);
            ok = sig.publish(a0);
        } else if (types.size() == 1 && isIntType(types[0])) {
            int a0 = 0;
            if (!asInt(argsArr[0], a0)) { std::cerr << "swapi signal publish: arg0 int parse failed\n"; return 2; }
            sw::ipc::Signal<int> sig(reg, sigName);
            ok = sig.publish(a0);
        } else if (types.size() == 1 && isDoubleType(types[0])) {
            double a0 = 0.0;
            if (!asDouble(argsArr[0], a0)) { std::cerr << "swapi signal publish: arg0 double parse failed\n"; return 2; }
            sw::ipc::Signal<double> sig(reg, sigName);
            ok = sig.publish(a0);
        } else if (types.size() == 1 && isStringType(types[0])) {
            SwString a0;
            if (!asString(argsArr[0], a0)) { std::cerr << "swapi signal publish: arg0 SwString parse failed\n"; return 2; }
            sw::ipc::Signal<SwString> sig(reg, sigName);
            ok = sig.publish(a0);
        } else if (types.size() == 1 && isBytesType(types[0])) {
            SwByteArray a0;
            if (!asBytes(argsArr[0], a0)) { std::cerr << "swapi signal publish: arg0 SwByteArray parse failed (use string)\n"; return 2; }
            sw::ipc::Signal<SwByteArray> sig(reg, sigName);
            ok = sig.publish(a0);
        } else if (types.size() == 1 && isU64Type(types[0])) {
            uint64_t a0 = 0;
            if (!asU64(argsArr[0], a0)) { std::cerr << "swapi signal publish: arg0 u64 parse failed\n"; return 2; }
            sw::ipc::Signal<uint64_t> sig(reg, sigName);
            ok = sig.publish(a0);
        } else if (types.size() == 2 && isU64Type(types[0]) && isStringType(types[1])) {
            uint64_t a0 = 0;
            SwString a1;
            if (!asU64(argsArr[0], a0) || !asString(argsArr[1], a1)) { std::cerr << "swapi signal publish: args parse failed\n"; return 2; }
            sw::ipc::Signal<uint64_t, SwString> sig(reg, sigName);
            ok = sig.publish(a0, a1);
        } else if (types.size() == 2 && isIntType(types[0]) && isStringType(types[1])) {
            int a0 = 0;
            SwString a1;
            if (!asInt(argsArr[0], a0) || !asString(argsArr[1], a1)) { std::cerr << "swapi signal publish: args parse failed\n"; return 2; }
            sw::ipc::Signal<int, SwString> sig(reg, sigName);
            ok = sig.publish(a0, a1);
        } else if (types.size() == 3 && isIntType(types[0]) && isIntType(types[1]) && isIntType(types[2])) {
            int a0 = 0, a1 = 0, a2 = 0;
            if (!asInt(argsArr[0], a0) || !asInt(argsArr[1], a1) || !asInt(argsArr[2], a2)) { std::cerr << "swapi signal publish: args parse failed\n"; return 2; }
            sw::ipc::Signal<int, int, int> sig(reg, sigName);
            ok = sig.publish(a0, a1, a2);
        } else if (types.size() == 3 && isU64Type(types[0]) && isStringType(types[1]) && isStringType(types[2])) {
            uint64_t a0 = 0;
            SwString a1, a2;
            if (!asU64(argsArr[0], a0) || !asString(argsArr[1], a1) || !asString(argsArr[2], a2)) { std::cerr << "swapi signal publish: args parse failed\n"; return 2; }
            sw::ipc::Signal<uint64_t, SwString, SwString> sig(reg, sigName);
            ok = sig.publish(a0, a1, a2);
        } else if (types.size() == 3 && isBoolType(types[0]) && isIntType(types[1]) && isStringType(types[2])) {
            bool a0 = false;
            int a1 = 0;
            SwString a2;
            if (!asBool(argsArr[0], a0) || !asInt(argsArr[1], a1) || !asString(argsArr[2], a2)) { std::cerr << "swapi signal publish: args parse failed\n"; return 2; }
            sw::ipc::Signal<bool, int, SwString> sig(reg, sigName);
            ok = sig.publish(a0, a1, a2);
        } else if (types.size() == 3 && isIntType(types[0]) && isDoubleType(types[1]) && isStringType(types[2])) {
            int a0 = 0;
            double a1 = 0.0;
            SwString a2;
            if (!asInt(argsArr[0], a0) || !asDouble(argsArr[1], a1) || !asString(argsArr[2], a2)) { std::cerr << "swapi signal publish: args parse failed\n"; return 2; }
            sw::ipc::Signal<int, double, SwString> sig(reg, sigName);
            ok = sig.publish(a0, a1, a2);
        } else {
            std::cerr << "swapi signal publish: unsupported signature (extend dispatcher)\n";
            return 3;
        }
    } catch (...) {
        ok = false;
    }

    if (jsonOut) {
        SwJsonObject o;
        o["ok"] = SwJsonValue(ok);
        o["target"] = SwJsonValue(target.toString().toStdString());
        o["signal"] = SwJsonValue(sigName.toStdString());
        o["typeName"] = SwJsonValue(typeName.toStdString());
        std::cout << SwApiJson::toJson(o, pretty).toStdString() << "\n";
        return ok ? 0 : 3;
    }

    if (!ok) {
        std::cerr << "failed\n";
        return 3;
    }
    std::cout << "ok\n";
    return 0;
}

void SwApiSignalsCommand::start() {
    const SwStringList& a = args();
    if (a.isEmpty()) {
        printUsage_();
        finish(2);
        return;
    }

    const SwString sub = a[0];
    if (sub == "echo") {
        cmdEcho_();
        return; // async
    }

    int code = 2;
    if (sub == "list") code = cmdList_();
    else if (sub == "publish" || sub == "pub") code = cmdPublish_();
    else {
        printUsage_();
        finish(2);
        return;
    }

    finish(code);
}
