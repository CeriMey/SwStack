#pragma once

#include "SwDir.h"
#include "SwString.h"

namespace swbuild {

inline SwString normalizePath(SwString p) {
    p.replace("\\", "/");
    while (p.contains("//")) p.replace("//", "/");
    return p;
}

inline bool isAbsPath(const SwString& p) {
    if (p.isEmpty()) return false;
    if (p.startsWith("/") || p.startsWith("\\")) return true;
    return (p.size() >= 2 && p[1] == ':');
}

inline SwString joinPath(const SwString& base, const SwString& rel) {
    if (base.isEmpty()) return rel;
    if (rel.isEmpty()) return base;

    SwString b = normalizePath(base);
    SwString r = normalizePath(rel);

    while (b.endsWith("/")) b.chop(1);
    while (r.startsWith("/")) r = r.mid(1);

    return b + "/" + r;
}

inline SwString absPath(const SwString& p) {
    if (p.isEmpty()) return SwString();
    return normalizePath(swDirPlatform().absolutePath(p));
}

inline SwString resolveAgainst(const SwString& baseDirAbs, const SwString& maybeRel) {
    if (maybeRel.isEmpty()) return SwString();
    if (isAbsPath(maybeRel)) return absPath(maybeRel);
    return absPath(joinPath(baseDirAbs, maybeRel));
}

inline SwString relativeToRoot(const SwString& rootDirAbs, const SwString& dirAbs) {
    SwString root = normalizePath(rootDirAbs);
    SwString dir = normalizePath(dirAbs);
    while (root.endsWith("/")) root.chop(1);

    if (dir == root) return SwString(".");
    if (!dir.startsWith(root)) return dir;
    if (dir.size() <= root.size()) return SwString(".");

    const char next = dir[root.size()];
    if (next != '/') return dir; // prefix collision
    return dir.mid(static_cast<int>(root.size() + 1));
}

inline SwString sanitizeForFileLeaf(SwString leaf) {
    leaf.replace("\\", "_");
    leaf.replace("/", "_");
    leaf.replace(":", "_");
    leaf.replace(" ", "_");
    leaf.replace("\"", "_");
    leaf.replace("'", "_");
    return leaf;
}

inline SwString resolveDependencyDir(const SwString& rootDirAbs,
                                     const SwString& projectDirAbs,
                                     const SwString& value) {
    if (value.isEmpty()) return SwString();

    // Try absolute first.
    if (isAbsPath(value)) {
        const SwString abs = absPath(value);
        if (swDirPlatform().isDirectory(abs)) return abs;
        return SwString();
    }

    // Try relative to project dir.
    const SwString projRel = absPath(joinPath(projectDirAbs, value));
    if (swDirPlatform().isDirectory(projRel)) return projRel;

    // Try relative to root dir.
    const SwString rootRel = absPath(joinPath(rootDirAbs, value));
    if (swDirPlatform().isDirectory(rootRel)) return rootRel;

    return SwString();
}

inline SwString quoteArgIfNeeded(const SwString& value) {
#if defined(_WIN32)
    if (value.isEmpty()) return value;
    const bool hasSpace = value.contains(" ") || value.contains("\t");
    const bool hasQuote = value.contains("\"");
    if (!hasSpace && !hasQuote) return value;
    if (value.startsWith("\"") && value.endsWith("\"")) return value;

    SwString escaped = value;
    escaped.replace("\"", "\\\"");
    return SwString("\"") + escaped + "\"";
#else
    return value;
#endif
}

} // namespace swbuild
