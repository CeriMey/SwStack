#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwString.h"
#include "SwList.h"
#include "SwDir.h"
#include "SwFile.h"

#include "SwBuildUtils.h"
#include "SwBuildProject.h"
#include "SwBuildDependencyResolver.h"

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

// ─── swbuild::normalizePath ───────────────────────────────────────────────

static void testNormalizePath() {
    std::cout << "[suite] swbuild::normalizePath" << std::endl;

    check("backslash to slash",
          swbuild::normalizePath("C:\\foo\\bar") == "C:/foo/bar");

    check("collapse double slashes",
          swbuild::normalizePath("a//b///c") == "a/b/c");

    check("mixed slashes and doubles",
          swbuild::normalizePath("C:\\\\a//b\\c") == "C:/a/b/c");

    check("empty string unchanged",
          swbuild::normalizePath("").isEmpty());

    check("single slash preserved",
          swbuild::normalizePath("/") == "/");
}

// ─── swbuild::isAbsPath ──────────────────────────────────────────────────

static void testIsAbsPath() {
    std::cout << "[suite] swbuild::isAbsPath" << std::endl;

    check("unix absolute /usr/bin", swbuild::isAbsPath("/usr/bin"));
    check("windows absolute C:\\foo", swbuild::isAbsPath("C:\\foo"));
    check("windows drive D:", swbuild::isAbsPath("D:"));
    check("relative path", !swbuild::isAbsPath("src/main.cpp"));
    check("empty string", !swbuild::isAbsPath(""));
    check("backslash prefix", swbuild::isAbsPath("\\server\\share"));
}

// ─── swbuild::joinPath ───────────────────────────────────────────────────

static void testJoinPath() {
    std::cout << "[suite] swbuild::joinPath" << std::endl;

    check("simple join",
          swbuild::joinPath("/root", "sub") == "/root/sub");

    check("trailing slash on base stripped",
          swbuild::joinPath("/root/", "sub") == "/root/sub");

    check("leading slash on rel stripped",
          swbuild::joinPath("/root", "/sub") == "/root/sub");

    check("both slashes stripped",
          swbuild::joinPath("/root//", "//sub") == "/root/sub");

    check("empty base returns rel",
          swbuild::joinPath("", "sub") == "sub");

    check("empty rel returns base",
          swbuild::joinPath("/root", "") == "/root");

    check("both empty returns empty",
          swbuild::joinPath("", "").isEmpty());
}

// ─── swbuild::relativeToRoot ─────────────────────────────────────────────

static void testRelativeToRoot() {
    std::cout << "[suite] swbuild::relativeToRoot" << std::endl;

    check("child of root",
          swbuild::relativeToRoot("/home/project", "/home/project/src/main") == "src/main");

    check("same as root",
          swbuild::relativeToRoot("/home/project", "/home/project") == ".");

    check("not under root returns absolute",
          swbuild::relativeToRoot("/home/project", "/other/path") == "/other/path");

    check("prefix collision (root=/a, dir=/abc)",
          swbuild::relativeToRoot("/a", "/abc") == "/abc");

    check("trailing slash on root handled",
          swbuild::relativeToRoot("/home/project/", "/home/project/src") == "src");
}

// ─── swbuild::sanitizeForFileLeaf ────────────────────────────────────────

static void testSanitizeForFileLeaf() {
    std::cout << "[suite] swbuild::sanitizeForFileLeaf" << std::endl;

    check("slashes replaced",
          swbuild::sanitizeForFileLeaf("a/b\\c") == "a_b_c");

    check("colons and spaces replaced",
          swbuild::sanitizeForFileLeaf("C: foo") == "C_ foo".isEmpty() ? false : true);

    // More specific check
    SwString s = swbuild::sanitizeForFileLeaf("C: foo");
    check("colons replaced detail", s == "C_ foo");

    check("quotes replaced",
          swbuild::sanitizeForFileLeaf("he said \"hi\" and 'bye'") == "he said _hi_ and _bye_");

    check("clean leaf unchanged",
          swbuild::sanitizeForFileLeaf("hello_world.txt") == "hello_world.txt");
}

// ─── SwBuildProject ──────────────────────────────────────────────────────

static void testBuildProject() {
    std::cout << "[suite] SwBuildProject" << std::endl;

    SwBuildProject p("/home/root", "/home/root/src/NodeA", "/home/root/build", "/home/root/log");

    check("rootDirAbs normalized",
          p.rootDirAbs() == "/home/root");

    check("sourceDirAbs normalized",
          p.sourceDirAbs() == "/home/root/src/NodeA");

    check("relativeSourceDir correct",
          p.relativeSourceDir() == "src/NodeA");

    check("buildDirAbs uses relativeSourceDir",
          p.buildDirAbs() == "/home/root/build/src/NodeA");

    check("logDirAbs uses relativeSourceDir",
          p.logDirAbs() == "/home/root/log/src/NodeA");

    SwList<SwString> deps;
    deps.append("/home/root/src/Core");
    deps.append("/home/root/src/Utils");
    p.setDependenciesAbs(deps);
    check("dependencies set correctly", p.dependenciesAbs().size() == 2);
    check("first dependency matches", p.dependenciesAbs()[0] == "/home/root/src/Core");
}

// ─── SwBuildDependencyResolver ───────────────────────────────────────────

static void testDependencyResolver() {
    std::cout << "[suite] SwBuildDependencyResolver" << std::endl;

    SwBuildDependencyResolver resolver;
    SwString err;

    // Test 1: empty list
    {
        SwList<SwBuildProject> empty;
        bool ok = resolver.sort(empty, err);
        check("empty list sorts ok", ok);
        check("empty list still empty", empty.isEmpty());
    }

    // Test 2: single project
    {
        SwList<SwBuildProject> single;
        single.append(SwBuildProject("/r", "/r/A", "/r/b", "/r/l"));
        bool ok = resolver.sort(single, err);
        check("single project sorts ok", ok);
        check("single project preserved", single.size() == 1);
    }

    // Test 3: linear dependency chain A -> B -> C
    {
        SwBuildProject pA("/r", "/r/A", "/r/b", "/r/l");
        SwBuildProject pB("/r", "/r/B", "/r/b", "/r/l");
        SwBuildProject pC("/r", "/r/C", "/r/b", "/r/l");

        SwList<SwString> depsA;
        depsA.append("/r/B");
        pA.setDependenciesAbs(depsA);

        SwList<SwString> depsB;
        depsB.append("/r/C");
        pB.setDependenciesAbs(depsB);

        SwList<SwBuildProject> projects;
        projects.append(pA);
        projects.append(pB);
        projects.append(pC);

        bool ok = resolver.sort(projects, err);
        check("linear chain sorts ok", ok);
        check("C before B", projects[0].sourceDirAbs() == "/r/C");
        check("B before A", projects[1].sourceDirAbs() == "/r/B");
        check("A last", projects[2].sourceDirAbs() == "/r/A");
    }

    // Test 4: cycle detection
    {
        SwBuildProject pX("/r", "/r/X", "/r/b", "/r/l");
        SwBuildProject pY("/r", "/r/Y", "/r/b", "/r/l");

        SwList<SwString> depsX;
        depsX.append("/r/Y");
        pX.setDependenciesAbs(depsX);

        SwList<SwString> depsY;
        depsY.append("/r/X");
        pY.setDependenciesAbs(depsY);

        SwList<SwBuildProject> projects;
        projects.append(pX);
        projects.append(pY);

        bool ok = resolver.sort(projects, err);
        check("cycle detected", !ok);
        check("error mentions cycle", err.contains("cycle"));
    }

    // Test 5: missing dependency
    {
        SwBuildProject pM("/r", "/r/M", "/r/b", "/r/l");
        SwList<SwString> depsM;
        depsM.append("/r/MISSING");
        pM.setDependenciesAbs(depsM);

        SwList<SwBuildProject> projects;
        projects.append(pM);

        bool ok = resolver.sort(projects, err);
        check("missing dep detected", !ok);
        check("error mentions missing", err.contains("missing") || err.contains("unknown"));
    }

    // Test 6: diamond dependency A -> B,C -> D
    {
        SwBuildProject pA("/r", "/r/A", "/r/b", "/r/l");
        SwBuildProject pB("/r", "/r/B", "/r/b", "/r/l");
        SwBuildProject pC("/r", "/r/C", "/r/b", "/r/l");
        SwBuildProject pD("/r", "/r/D", "/r/b", "/r/l");

        SwList<SwString> depsA;
        depsA.append("/r/B");
        depsA.append("/r/C");
        pA.setDependenciesAbs(depsA);

        SwList<SwString> depsB;
        depsB.append("/r/D");
        pB.setDependenciesAbs(depsB);

        SwList<SwString> depsC;
        depsC.append("/r/D");
        pC.setDependenciesAbs(depsC);

        SwList<SwBuildProject> projects;
        projects.append(pA);
        projects.append(pB);
        projects.append(pC);
        projects.append(pD);

        bool ok = resolver.sort(projects, err);
        check("diamond sorts ok", ok);
        check("D is first (no deps)", projects[0].sourceDirAbs() == "/r/D");
        check("A is last (depends on all)", projects[3].sourceDirAbs() == "/r/A");
    }
}

// ─── swbuild::quoteArgIfNeeded (Windows-specific) ────────────────────────

static void testQuoteArgIfNeeded() {
    std::cout << "[suite] swbuild::quoteArgIfNeeded" << std::endl;

#if defined(_WIN32)
    check("no space no quote unchanged",
          swbuild::quoteArgIfNeeded("simple") == "simple");

    check("with space gets quoted",
          swbuild::quoteArgIfNeeded("with space").startsWith("\""));

    check("already quoted left alone",
          swbuild::quoteArgIfNeeded("\"already quoted\"") == "\"already quoted\"");

    check("empty unchanged",
          swbuild::quoteArgIfNeeded("").isEmpty());
#else
    check("unix: passthrough",
          swbuild::quoteArgIfNeeded("with space") == "with space");
#endif
}

// ─── Main ────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    std::cout << "=== SwBuild Self-Test ===" << std::endl;
    std::cout << std::endl;

    testNormalizePath();
    testIsAbsPath();
    testJoinPath();
    testRelativeToRoot();
    testSanitizeForFileLeaf();
    testBuildProject();
    testDependencyResolver();
    testQuoteArgIfNeeded();

    std::cout << std::endl;
    std::cout << "=== Results: " << sPassed << " passed, " << sFailed << " failed ===" << std::endl;

    return sFailed > 0 ? 1 : 0;
}
