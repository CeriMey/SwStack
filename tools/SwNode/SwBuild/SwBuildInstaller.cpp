#include "SwBuildInstaller.h"

#include "SwBuildUtils.h"

#include "SwDebug.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwMap.h"

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace {

static SwString fileNameFromPath_(SwString p) {
    p.replace("\\", "/");
    const SwList<SwString> parts = p.split('/');
    return parts.isEmpty() ? SwString() : parts.last();
}

static SwString baseName_(SwString leaf) {
    leaf.replace("\\", "/");
    const size_t dot = leaf.lastIndexOf('.');
    if (dot == static_cast<size_t>(-1)) return leaf;
    return leaf.left(static_cast<int>(dot));
}

static SwString extension_(SwString leaf) {
    leaf.replace("\\", "/");
    const size_t dot = leaf.lastIndexOf('.');
    if (dot == static_cast<size_t>(-1)) return SwString();
    return leaf.mid(static_cast<int>(dot + 1));
}

static void collectFilesRecursive_(const SwString& dirAbs, SwList<SwString>& out) {
    if (!swDirPlatform().isDirectory(dirAbs)) return;

    SwDir d(dirAbs);
    const SwStringList entries = d.entryList(EntryType::AllEntries);
    for (int i = 0; i < entries.size(); ++i) {
        const SwString name = entries[i];
        if (name == "." || name == "..") continue;
        if (name == "CMakeFiles") continue;
        const SwString full = swbuild::joinPath(dirAbs, name);
        if (swDirPlatform().isDirectory(full)) {
            collectFilesRecursive_(full, out);
            continue;
        }
        if (swFilePlatform().isFile(full)) {
            out.append(full);
        }
    }
}

#if !defined(_WIN32)
static bool isPosixExecutable_(const SwString& pathAbs) {
    struct stat st;
    if (::stat(pathAbs.toStdString().c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    return (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}
#endif

} // namespace

bool SwBuildInstaller::install(const SwBuildProject& project, const SwBuildOptions& options, SwString& errOut) const {
    errOut.clear();

    const SwString installRoot = options.installRootDir();
    const SwString binDir = swbuild::joinPath(installRoot, "bin");
    const SwString libDir = swbuild::joinPath(installRoot, "lib");
    const SwString pluginsDir = swbuild::joinPath(installRoot, "plugins");

    if (!SwDir::mkpathAbsolute(binDir, false) || !SwDir::mkpathAbsolute(libDir, false) ||
        !SwDir::mkpathAbsolute(pluginsDir, false)) {
        errOut = SwString("failed to create install subdirs under: ") + installRoot;
        return false;
    }

    // Prefer the config subdir when it exists (multi-config generators).
    SwString searchRoot = swbuild::joinPath(project.buildDirAbs(), options.buildType());
    if (!swDirPlatform().isDirectory(searchRoot)) {
        searchRoot = project.buildDirAbs();
    }

    SwList<SwString> files;
    collectFilesRecursive_(searchRoot, files);

    SwMap<SwString, SwString> destByBase; // baseName -> destDir (for pdb sidecar)

    for (int i = 0; i < files.size(); ++i) {
        const SwString src = files[i];
        const SwString leaf = fileNameFromPath_(src);
        const SwString ext = extension_(leaf).toLower();

        SwString destDir;
        if (ext == "exe") {
            destDir = binDir;
        } else if (ext.isEmpty()) {
#if !defined(_WIN32)
            if (isPosixExecutable_(src)) {
                destDir = binDir;
            } else {
                continue;
            }
#else
            continue;
#endif
        } else if (ext == "dll" || ext == "so" || ext == "dylib") {
            destDir = pluginsDir;
        } else if (ext == "lib" || ext == "a") {
            destDir = libDir;
        } else if (ext == "pdb") {
            // handled later if we can match to a built artifact
            continue;
        } else {
            continue;
        }

        const SwString dst = swbuild::joinPath(destDir, leaf);
        if (!SwFile::copy(src, dst, true)) {
            errOut = SwString("failed to copy: ") + src + SwString(" -> ") + dst;
            return false;
        }

        destByBase.insert(baseName_(leaf), destDir);
    }

    // Copy PDBs next to their matching exe/dll when possible.
    for (int i = 0; i < files.size(); ++i) {
        const SwString src = files[i];
        const SwString leaf = fileNameFromPath_(src);
        const SwString ext = extension_(leaf).toLower();
        if (ext != "pdb") continue;

        const SwString base = baseName_(leaf);
        if (!destByBase.contains(base)) continue;
        const SwString destDir = destByBase[base];
        const SwString dst = swbuild::joinPath(destDir, leaf);

        (void)SwFile::copy(src, dst, true);
    }

    swDebug() << "[SwBuild] installed artifacts from" << project.relativeSourceDir();
    return true;
}
