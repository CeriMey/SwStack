#include "SwBuildProjectScanner.h"

#include "SwBuildUtils.h"

#include "SwDir.h"
#include "SwFile.h"

namespace {

static bool isExcludedDirName_(const SwString& name) {
    if (name.isEmpty()) return true;
    if (name == "." || name == "..") return true;
    if (name == ".git" || name == ".vs" || name == ".idea") return true;
    if (name == "build" || name == "install" || name == "log") return true;
    if (name.startsWith("build-")) return true;
    return false;
}

static void findCMakeProjectDirs_(const SwString& dirAbs, SwList<SwString>& outDirs) {
    if (!swDirPlatform().isDirectory(dirAbs)) return;

    // If this directory has a CMakeLists.txt, register it.
    const SwString cmakeFile = swbuild::joinPath(dirAbs, "CMakeLists.txt");
    if (swFilePlatform().isFile(cmakeFile)) {
        outDirs.append(dirAbs);
    }

    SwDir d(dirAbs);
    const SwStringList subdirs = d.entryList(EntryType::Directories);
    for (int i = 0; i < subdirs.size(); ++i) {
        const SwString name = subdirs[i];
        if (isExcludedDirName_(name)) continue;
        const SwString sub = swbuild::joinPath(dirAbs, name);
        findCMakeProjectDirs_(sub, outDirs);
    }
}

static SwList<SwString> filterNestedProjects_(const SwList<SwString>& dirsIn) {
    SwList<SwString> dirs;
    dirs.reserve(dirsIn.size());
    for (int i = 0; i < dirsIn.size(); ++i) {
        SwString d = swbuild::normalizePath(dirsIn[i]);
        while (d.endsWith("/")) d.chop(1);
        dirs.append(d);
    }

    SwList<SwString> out;
    for (int i = 0; i < dirs.size(); ++i) {
        const SwString di = dirs[i];
        bool nested = false;
        for (int j = 0; j < dirs.size(); ++j) {
            if (i == j) continue;
            const SwString dj = dirs[j];
            if (dj.isEmpty()) continue;
            if (!di.startsWith(dj)) continue;
            if (di.size() <= dj.size()) continue;
            if (di[dj.size()] == '/') {
                nested = true;
                break;
            }
        }
        if (!nested) out.append(di);
    }

    out.removeDuplicates();
    return out;
}

static SwList<SwString> listDependFiles_(const SwString& dirAbs) {
    SwList<SwString> out;
    if (!swDirPlatform().isDirectory(dirAbs)) return out;

    SwDir d(dirAbs);
    const SwStringList files = d.entryList(EntryType::Files);
    for (int i = 0; i < files.size(); ++i) {
        const SwString name = files[i];
        if (name == ".depend" || name.endsWith(".depend")) {
            out.append(swbuild::joinPath(dirAbs, name));
        }
    }
    return out;
}

static SwString stripInlineComment_(const SwString& line) {
    const size_t pos = line.indexOf('#');
    if (pos == static_cast<size_t>(-1)) return line;
    return line.left(static_cast<int>(pos));
}

static SwList<SwString> parseDepends_(const SwString& projectDirAbs,
                                      const SwString& rootDirAbs) {
    SwList<SwString> depsAbs;

    const SwList<SwString> dependFiles = listDependFiles_(projectDirAbs);
    for (int f = 0; f < dependFiles.size(); ++f) {
        SwFile file(dependFiles[f]);
        if (!file.open(SwFile::Read)) {
            continue;
        }
        const SwString raw = file.readAll();
        const SwList<SwString> lines = raw.split('\n');

        for (int i = 0; i < lines.size(); ++i) {
            SwString line = stripInlineComment_(lines[i]).trimmed();
            if (line.isEmpty()) continue;

            const SwString resolved = swbuild::resolveDependencyDir(rootDirAbs, projectDirAbs, line);
            if (!resolved.isEmpty()) {
                depsAbs.append(resolved);
            }
        }
    }

    depsAbs.removeDuplicates();
    return depsAbs;
}

} // namespace

SwList<SwBuildProject> SwBuildProjectScanner::scan(const SwBuildOptions& options, SwString& errOut) const {
    errOut.clear();

    const SwString scanRoot = options.scanDirAbs();
    if (!swDirPlatform().isDirectory(scanRoot)) {
        errOut = SwString("scan dir not found: ") + scanRoot;
        return {};
    }

    SwList<SwString> dirs;
    findCMakeProjectDirs_(scanRoot, dirs);
    dirs.removeDuplicates();
    if (!options.includeNested()) {
        dirs = filterNestedProjects_(dirs);
    }

    SwList<SwBuildProject> projects;
    projects.reserve(dirs.size());

    for (int i = 0; i < dirs.size(); ++i) {
        const SwString dirAbs = swbuild::normalizePath(dirs[i]);
        SwBuildProject project(options.rootDirAbs(), dirAbs, options.buildRootDir(), options.logRootDir());
        project.setDependenciesAbs(parseDepends_(dirAbs, options.rootDirAbs()));
        projects.append(project);
    }

    return projects;
}
