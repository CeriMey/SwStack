#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwString.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonArray.h"
#include "SwJsonValue.h"

#include "SwApiCli.h"
#include "SwApiJson.h"

#include <iostream>
#include <string>

static int sPassed = 0;
static int sFailed = 0;

static void check(const std::string& name, bool condition) {
    if (condition) {
        ++sPassed;
        std::cout << "  [PASS] " << name << std::endl;
    } else {
        ++sFailed;
        std::cout << "  [FAIL] " << name << std::endl;
    }
}

// ─── Helper to build argc/argv from string list ─────────────────────────

struct FakeArgv {
    SwList<std::string> storage;
    SwList<char*> ptrs;
    int argc() const { return static_cast<int>(ptrs.size()); }
    char** argv() { return ptrs.data(); }

    FakeArgv(std::initializer_list<const char*> args) {
        for (auto a : args) {
            storage.append(std::string(a));
        }
        for (int i = 0; i < storage.size(); ++i) {
            ptrs.append(const_cast<char*>(storage[i].c_str()));
        }
    }
};

// ─── SwApiCli: positional arguments ─────────────────────────────────────

static void testCliPositionals() {
    std::cout << "[suite] SwApiCli positionals" << std::endl;

    FakeArgv a({"swapi", "apps", "nodes"});
    SwApiCli cli(a.argc(), a.argv());

    check("exe is swapi", cli.exe() == "swapi");
    check("2 positionals", cli.positionals().size() == 2);
    check("first positional is apps", cli.positionals()[0] == "apps");
    check("second positional is nodes", cli.positionals()[1] == "nodes");
}

// ─── SwApiCli: --key=value style ────────────────────────────────────────

static void testCliKeyValue() {
    std::cout << "[suite] SwApiCli key=value" << std::endl;

    FakeArgv a({"swapi", "--domain=demo", "--timeout_ms=5000", "apps"});
    SwApiCli cli(a.argc(), a.argv());

    check("domain value", cli.value("domain") == "demo");
    check("timeout_ms int", cli.intValue("timeout_ms", 0) == 5000);
    check("positional after options", cli.positionals().size() == 1);
    check("positional value", cli.positionals()[0] == "apps");
}

// ─── SwApiCli: --key value (space separated) ────────────────────────────

static void testCliKeySpaceValue() {
    std::cout << "[suite] SwApiCli key space value" << std::endl;

    FakeArgv a({"swapi", "--domain", "myDomain", "--target", "ns/obj", "ping"});
    SwApiCli cli(a.argc(), a.argv());

    check("domain space-separated", cli.value("domain") == "myDomain");
    check("target space-separated", cli.value("target") == "ns/obj");
    check("positional after spaced options", cli.positionals()[0] == "ping");
}

// ─── SwApiCli: flags ────────────────────────────────────────────────────

static void testCliFlags() {
    std::cout << "[suite] SwApiCli flags" << std::endl;

    FakeArgv a({"swapi", "--json", "--pretty", "-h", "apps"});
    SwApiCli cli(a.argc(), a.argv());

    check("json flag", cli.hasFlag("json"));
    check("pretty flag", cli.hasFlag("pretty"));
    check("help via -h", cli.hasFlag("help"));
    check("nonexistent flag", !cli.hasFlag("verbose"));
}

// ─── SwApiCli: shorthand flags ──────────────────────────────────────────

static void testCliShortFlags() {
    std::cout << "[suite] SwApiCli short flags" << std::endl;

    FakeArgv a({"swapi", "-j", "-p", "nodes"});
    SwApiCli cli(a.argc(), a.argv());

    check("-j sets json", cli.hasFlag("json"));
    check("-p sets pretty", cli.hasFlag("pretty"));
}

// ─── SwApiCli: -d domain shorthand ──────────────────────────────────────

static void testCliDomainShorthand() {
    std::cout << "[suite] SwApiCli -d shorthand" << std::endl;

    FakeArgv a({"swapi", "-d", "myDomain", "apps"});
    SwApiCli cli(a.argc(), a.argv());

    check("-d sets domain", cli.value("domain") == "myDomain");
}

// ─── SwApiCli: -- stops option parsing ──────────────────────────────────

static void testCliDoubleDash() {
    std::cout << "[suite] SwApiCli -- separator" << std::endl;

    FakeArgv a({"swapi", "--json", "--", "--not-a-flag", "positional"});
    SwApiCli cli(a.argc(), a.argv());

    check("json flag before --", cli.hasFlag("json"));
    check("--not-a-flag is positional", cli.positionals().size() == 2);
    check("--not-a-flag value", cli.positionals()[0] == "--not-a-flag");
    check("positional value", cli.positionals()[1] == "positional");
}

// ─── SwApiCli: default values ───────────────────────────────────────────

static void testCliDefaults() {
    std::cout << "[suite] SwApiCli defaults" << std::endl;

    FakeArgv a({"swapi", "apps"});
    SwApiCli cli(a.argc(), a.argv());

    check("missing option returns default", cli.value("domain", "fallback") == "fallback");
    check("missing int returns default", cli.intValue("timeout_ms", 42) == 42);
}

// ─── SwApiJson: toJson / parse roundtrip ────────────────────────────────

static void testJsonRoundtrip() {
    std::cout << "[suite] SwApiJson roundtrip" << std::endl;

    SwJsonObject obj;
    obj["name"] = SwJsonValue(std::string("test"));
    obj["value"] = SwJsonValue(42);

    SwString compact = SwApiJson::toJson(obj, false);
    check("compact is non-empty", !compact.isEmpty());
    check("compact contains name", compact.contains("\"name\""));

    SwJsonObject parsed;
    SwString err;
    bool ok = SwApiJson::parseObject(compact, parsed, err);
    check("parse back ok", ok);
    check("name preserved", SwString(parsed["name"].toString()) == "test");
    check("value preserved", parsed["value"].toInt() == 42);
}

// ─── SwApiJson: parseArray ──────────────────────────────────────────────

static void testJsonParseArray() {
    std::cout << "[suite] SwApiJson parseArray" << std::endl;

    SwJsonArray arr;
    SwString err;
    bool ok = SwApiJson::parseArray("[1, 2, 3]", arr, err);
    check("parse array ok", ok);
    check("array size 3", arr.size() == 3);
    check("first element is 1", arr[0].toInt() == 1);

    ok = SwApiJson::parseArray("{\"not\": \"array\"}", arr, err);
    check("object rejected as array", !ok);
    check("error mentions not array", err.contains("not an array"));
}

// ─── SwApiJson: parseObject rejects non-object ──────────────────────────

static void testJsonParseObjectReject() {
    std::cout << "[suite] SwApiJson parseObject rejects" << std::endl;

    SwJsonObject obj;
    SwString err;
    bool ok = SwApiJson::parseObject("[1,2,3]", obj, err);
    check("array rejected as object", !ok);
    check("error mentions not object", err.contains("not an object"));

    ok = SwApiJson::parseObject("invalid json{{{", obj, err);
    check("invalid json rejected", !ok);
}

// ─── SwApiJson: tryGetPath ──────────────────────────────────────────────

static void testJsonTryGetPath() {
    std::cout << "[suite] SwApiJson tryGetPath" << std::endl;

    // Build nested structure: { "a": { "b": [10, 20, 30] } }
    SwJsonArray inner;
    inner.append(SwJsonValue(10));
    inner.append(SwJsonValue(20));
    inner.append(SwJsonValue(30));

    SwJsonObject b;
    b["b"] = SwJsonValue(inner);

    SwJsonObject root;
    root["a"] = SwJsonValue(b);

    SwJsonValue rootVal(root);
    SwJsonValue out;
    SwString err;

    check("empty path returns root",
          SwApiJson::tryGetPath(rootVal, "", out, err) && out.isObject());

    check("path a/b returns array",
          SwApiJson::tryGetPath(rootVal, "a/b", out, err) && out.isArray());

    check("path a/b/1 returns 20",
          SwApiJson::tryGetPath(rootVal, "a/b/1", out, err) && out.toInt() == 20);

    check("path a/b/0 returns 10",
          SwApiJson::tryGetPath(rootVal, "a/b/0", out, err) && out.toInt() == 10);

    bool ok = SwApiJson::tryGetPath(rootVal, "a/b/99", out, err);
    check("out of range index fails", !ok);
    check("error mentions out of range", err.contains("out of range"));

    ok = SwApiJson::tryGetPath(rootVal, "a/missing", out, err);
    check("missing key fails", !ok);
    check("error mentions missing key", err.contains("missing key"));

    ok = SwApiJson::tryGetPath(rootVal, "a/b/notanumber", out, err);
    check("non-numeric array index fails", !ok);

    // Test with leading/trailing slashes (normalized)
    check("leading slash normalized",
          SwApiJson::tryGetPath(rootVal, "/a/b/2", out, err) && out.toInt() == 30);
}

// ─── SwApiJson: pretty vs compact ───────────────────────────────────────

static void testJsonPrettyVsCompact() {
    std::cout << "[suite] SwApiJson pretty vs compact" << std::endl;

    SwJsonObject obj;
    obj["key"] = SwJsonValue(std::string("val"));

    SwString pretty = SwApiJson::toJson(obj, true);
    SwString compact = SwApiJson::toJson(obj, false);

    check("pretty is longer than compact", pretty.size() > compact.size());
    check("pretty has newlines", pretty.contains("\n"));
}

// ─── Main ────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    std::cout << "=== SwAPI Self-Test ===" << std::endl;
    std::cout << std::endl;

    testCliPositionals();
    testCliKeyValue();
    testCliKeySpaceValue();
    testCliFlags();
    testCliShortFlags();
    testCliDomainShorthand();
    testCliDoubleDash();
    testCliDefaults();
    testJsonRoundtrip();
    testJsonParseArray();
    testJsonParseObjectReject();
    testJsonTryGetPath();
    testJsonPrettyVsCompact();

    std::cout << std::endl;
    std::cout << "=== Results: " << sPassed << " passed, " << sFailed << " failed ===" << std::endl;

    return sFailed > 0 ? 1 : 0;
}
