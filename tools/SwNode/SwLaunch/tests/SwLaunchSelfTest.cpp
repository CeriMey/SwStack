#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwString.h"
#include "SwList.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonArray.h"
#include "SwJsonValue.h"
#include "SwFile.h"
#include "SwDir.h"
#include "SwLaunchDeploySupport.h"
#include "SwStandardLocation.h"

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

// ─── Replicated utility functions from SwLaunch.cpp (static) ────────────
// These mirror the exact logic in SwLaunch.cpp so we can unit-test them.

static bool isAbsPath_(const SwString& p) {
    if (p.isEmpty()) return false;
    if (p.startsWith("/") || p.startsWith("\\")) return true;
    return (p.size() >= 2 && p[1] == ':');
}

static SwString joinPath_(const SwString& base, const SwString& rel) {
    if (base.isEmpty()) return rel;
    SwString b = base;
    b.replace("\\", "/");
    SwString r = rel;
    r.replace("\\", "/");
    while (b.endsWith("/")) b = b.left(static_cast<int>(b.size()) - 1);
    while (r.startsWith("/")) r = r.mid(1);
    return b + "/" + r;
}

static SwString sanitizeFileLeaf_(SwString leaf) {
    leaf.replace("\\", "_");
    leaf.replace("/", "_");
    leaf.replace(":", "_");
    leaf.replace(" ", "_");
    leaf.replace("\"", "_");
    leaf.replace("'", "_");
    return leaf;
}

static bool hasLeafSuffix_(const SwString& p) {
    const std::string s = p.toStdString();
    const size_t dot = s.find_last_of('.');
    const size_t slash = s.find_last_of("/\\");
    return (dot != std::string::npos) && (slash == std::string::npos || dot > slash);
}

static SwString stripKnownLibrarySuffix_(SwString path) {
    const SwString lower = path.toLower();
    if (lower.endsWith(".dll") && path.size() >= 4) {
        return path.left(static_cast<int>(path.size() - 4));
    }
    if (lower.endsWith(".so") && path.size() >= 3) {
        return path.left(static_cast<int>(path.size() - 3));
    }
    if (lower.endsWith(".dylib") && path.size() >= 6) {
        return path.left(static_cast<int>(path.size() - 6));
    }
    return path;
}

static bool isWslDrvFsPath_(const SwString& absPath) {
    SwString p = absPath;
    p.replace("\\", "/");
    if (!p.startsWith("/mnt/")) return false;
    if (p.size() < 6) return false;
    const char drive = p[5];
    if (drive < 'a' || drive > 'z') {
        if (drive < 'A' || drive > 'Z') return false;
    }
    return (p.size() == 6 || p[6] == '/');
}

static SwString wslDrvFsToWindowsPath_(const SwString& absPath) {
    SwString p = absPath;
    p.replace("\\", "/");
    if (!isWslDrvFsPath_(p)) return absPath;

    const char drive = p[5];
    SwString rest = (p.size() > 6) ? p.mid(6) : SwString("/");
    rest.replace("/", "\\");

    SwString out(1, static_cast<char>(drive >= 'a' ? drive - 32 : drive));
    out += ":";
    out += rest;
    return out;
}

static SwJsonObject getObjectOrEmpty_(const SwJsonValue& v) {
    if (v.isObject()) return v.toObject();
    return SwJsonObject();
}

static SwJsonArray getArrayOrEmpty_(const SwJsonValue& v) {
    if (v.isArray()) return v.toArray();
    return SwJsonArray();
}

// ─── isAbsPath_ ─────────────────────────────────────────────────────────

static void testIsAbsPath() {
    std::cout << "[suite] isAbsPath_" << std::endl;

    check("unix absolute", isAbsPath_("/usr/bin"));
    check("windows absolute", isAbsPath_("C:\\foo"));
    check("windows drive D:", isAbsPath_("D:"));
    check("relative path", !isAbsPath_("src/main.cpp"));
    check("empty string", !isAbsPath_(""));
    check("backslash prefix", isAbsPath_("\\server\\share"));
}

// ─── joinPath_ ──────────────────────────────────────────────────────────

static void testJoinPath() {
    std::cout << "[suite] joinPath_" << std::endl;

    check("simple join", joinPath_("/root", "sub") == "/root/sub");
    check("trailing slash stripped", joinPath_("/root/", "sub") == "/root/sub");
    check("leading slash stripped", joinPath_("/root", "/sub") == "/root/sub");
    check("empty base", joinPath_("", "sub") == "sub");
    check("backslash normalized", joinPath_("C:\\root", "sub") == "C:/root/sub");
}

// ─── sanitizeFileLeaf_ ─────────────────────────────────────────────────

static void testSanitizeFileLeaf() {
    std::cout << "[suite] sanitizeFileLeaf_" << std::endl;

    check("slashes", sanitizeFileLeaf_("a/b\\c") == "a_b_c");
    check("colon", sanitizeFileLeaf_("C:foo") == "C_foo");
    check("space", sanitizeFileLeaf_("hello world") == "hello_world");
    check("quotes", sanitizeFileLeaf_("it's \"fine\"") == "it_s__fine_");
    check("clean unchanged", sanitizeFileLeaf_("clean_name") == "clean_name");
}

// ─── hasLeafSuffix_ ────────────────────────────────────────────────────

static void testHasLeafSuffix() {
    std::cout << "[suite] hasLeafSuffix_" << std::endl;

    check("has .exe", hasLeafSuffix_("program.exe"));
    check("has .dll", hasLeafSuffix_("lib/module.dll"));
    check("no suffix", !hasLeafSuffix_("program"));
    check("dot in directory only", !hasLeafSuffix_("dir.name/program"));
    check("empty", !hasLeafSuffix_(""));
}

// ─── stripKnownLibrarySuffix_ ──────────────────────────────────────────

static void testStripLibrarySuffix() {
    std::cout << "[suite] stripKnownLibrarySuffix_" << std::endl;

    check("strip .dll", stripKnownLibrarySuffix_("MyPlugin.dll") == "MyPlugin");
    check("strip .so", stripKnownLibrarySuffix_("libfoo.so") == "libfoo");
    check("strip .dylib", stripKnownLibrarySuffix_("libbar.dylib") == "libbar");
    check("no suffix untouched", stripKnownLibrarySuffix_("MyPlugin") == "MyPlugin");
    check("case insensitive .DLL", stripKnownLibrarySuffix_("MyPlugin.DLL") == "MyPlugin");
    check(".exe not stripped", stripKnownLibrarySuffix_("app.exe") == "app.exe");
}

// ─── WSL path conversion ───────────────────────────────────────────────

static void testWslPaths() {
    std::cout << "[suite] WSL path conversion" << std::endl;

    check("detect /mnt/c", isWslDrvFsPath_("/mnt/c"));
    check("detect /mnt/c/Users", isWslDrvFsPath_("/mnt/c/Users"));
    check("detect /mnt/d/", isWslDrvFsPath_("/mnt/d/"));
    check("reject /mnt/", !isWslDrvFsPath_("/mnt/"));
    check("reject /home", !isWslDrvFsPath_("/home"));
    check("reject /mnt/12", !isWslDrvFsPath_("/mnt/1"));

    check("convert /mnt/c/Users to C:\\Users",
          wslDrvFsToWindowsPath_("/mnt/c/Users") == "C:\\Users");

    check("convert /mnt/d to D:",
          wslDrvFsToWindowsPath_("/mnt/d") == "D:\\");

    check("non-wsl path passthrough",
          wslDrvFsToWindowsPath_("/home/user") == "/home/user");
}

// ─── JSON config structure validation ──────────────────────────────────

static void testLaunchJsonStructure() {
    std::cout << "[suite] Launch JSON structure" << std::endl;

    // Simulate a minimal valid launch config
    SwString json = "{"
        "\"sys\": \"demo\","
        "\"nodes\": ["
        "  {\"executable\": \"MyNode\", \"ns\": \"video\", \"name\": \"capture\"}"
        "],"
        "\"containers\": ["
        "  {\"executable\": \"SwComponentContainer\", \"ns\": \"plugins\", \"name\": \"echo\","
        "   \"composition\": {\"plugins\": [\"EchoPlugin.dll\"]}}"
        "]"
        "}";

    SwJsonDocument doc;
    SwString err;
    bool ok = doc.loadFromJson(json, err);
    check("valid launch JSON parses", ok);

    SwJsonObject root = doc.object();
    check("sys field exists", root.contains("sys"));
    check("sys is demo", SwString(root["sys"].toString()) == "demo");

    SwJsonArray nodes = getArrayOrEmpty_(root["nodes"]);
    check("nodes array size 1", nodes.size() == 1);

    SwJsonObject nodeSpec = nodes[0].toObject();
    check("node has executable", nodeSpec.contains("executable"));
    check("node has ns", SwString(nodeSpec["ns"].toString()) == "video");
    check("node has name", SwString(nodeSpec["name"].toString()) == "capture");

    SwJsonArray containers = getArrayOrEmpty_(root["containers"]);
    check("containers array size 1", containers.size() == 1);

    SwJsonObject containerSpec = containers[0].toObject();
    SwJsonObject composition = getObjectOrEmpty_(containerSpec["composition"]);
    check("composition has plugins", composition.contains("plugins"));

    SwJsonArray plugins = getArrayOrEmpty_(composition["plugins"]);
    check("1 plugin listed", plugins.size() == 1);

    // Test plugin suffix stripping (as SwLaunch does)
    SwString pluginName = SwString(plugins[0].toString());
    SwString stripped = stripKnownLibrarySuffix_(pluginName);
    check("plugin dll suffix stripped", stripped == "EchoPlugin");
}

// ─── getObjectOrEmpty_ / getArrayOrEmpty_ ──────────────────────────────

static void testSafeAccessors() {
    std::cout << "[suite] Safe JSON accessors" << std::endl;

    SwJsonValue nullVal;
    check("getObjectOrEmpty_ on null", getObjectOrEmpty_(nullVal).size() == 0);
    check("getArrayOrEmpty_ on null", getArrayOrEmpty_(nullVal).size() == 0);

    SwJsonValue strVal(std::string("hello"));
    check("getObjectOrEmpty_ on string", getObjectOrEmpty_(strVal).size() == 0);
    check("getArrayOrEmpty_ on string", getArrayOrEmpty_(strVal).size() == 0);

    SwJsonObject obj;
    obj["k"] = SwJsonValue(1);
    SwJsonValue objVal(obj);
    check("getObjectOrEmpty_ on object", getObjectOrEmpty_(objVal).size() == 1);

    SwJsonArray arr;
    arr.append(SwJsonValue(1));
    SwJsonValue arrVal(arr);
    check("getArrayOrEmpty_ on array", getArrayOrEmpty_(arrVal).size() == 1);
}

// ─── Missing config file / json ─────────────────────────────────────────

static void testConfigLoadingEdgeCases() {
    std::cout << "[suite] Config loading edge cases" << std::endl;

    // Test that malformed JSON is correctly rejected
    SwJsonDocument doc;
    SwString err;

    bool ok = doc.loadFromJson("{not valid json}", err);
    check("malformed json rejected", !ok);

    ok = doc.loadFromJson("", err);
    check("empty string rejected", !ok);

    ok = doc.loadFromJson("[1,2,3]", err);
    check("array parses as json", ok);
    check("array is not object", !doc.isObject());
}

static void testDeploySupportHelpers() {
    std::cout << "[suite] Deploy support helpers" << std::endl;

    check("safe relative path accepted", swLaunchIsSafeRelativePath_("bin/app.exe"));
    check("unsafe parent path rejected", !swLaunchIsSafeRelativePath_("../bin/app.exe"));
    check("unsafe absolute path rejected", !swLaunchIsSafeRelativePath_("C:/bin/app.exe"));
    check("sha256 hex accepted", swLaunchIsSha256Hex_("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    check("sha256 bad length rejected", !swLaunchIsSha256Hex_("0123"));
    check("sha256 bad chars rejected", !swLaunchIsSha256Hex_("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"));

    const SwString tempRoot = joinPath_(SwStandardLocation::standardLocation(SwStandardLocationId::Temp), "swlaunch_selftest");
    check("create temp root", SwDir::mkpathAbsolute(tempRoot, false));

    const SwString filePath = joinPath_(tempRoot, "checksum.txt");
    SwString writeErr;
    check("write helper creates file", swLaunchWriteTextFile_(filePath, "abc", &writeErr));

    const SwString checksum = swLaunchChecksumForFile_(filePath).toLower();
    check("checksum for file is sha256(abc)",
          checksum == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const SwString joined = swLaunchJoinRootAndRelative_(tempRoot, "nested/file.bin");
    check("join root and relative keeps child path", joined.endsWith("/nested/file.bin") || joined.endsWith("\\nested\\file.bin"));

    (void)SwDir::removeRecursively(tempRoot);
}

// ─── Main ────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    std::cout << "=== SwLaunch Self-Test ===" << std::endl;
    std::cout << std::endl;

    testIsAbsPath();
    testJoinPath();
    testSanitizeFileLeaf();
    testHasLeafSuffix();
    testStripLibrarySuffix();
    testWslPaths();
    testLaunchJsonStructure();
    testSafeAccessors();
    testConfigLoadingEdgeCases();
    testDeploySupportHelpers();

    std::cout << std::endl;
    std::cout << "=== Results: " << sPassed << " passed, " << sFailed << " failed ===" << std::endl;

    return sFailed > 0 ? 1 : 0;
}
