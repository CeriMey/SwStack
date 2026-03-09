#pragma once

#include "SwDir.h"
#include "SwFileInfo.h"
#include "SwString.h"

inline SwString swCreatorEditorNormalizePath(const SwString& rawPath) {
    SwString normalized = SwDir::normalizePath(rawPath);
    normalized.replace("\\", "/");
    while (normalized.endsWith("/") && normalized.size() > 1) {
        if (normalized.size() == 3 && normalized[1] == ':') {
            break;
        }
        normalized.chop(1);
    }
    return normalized;
}

inline SwString swCreatorEditorParentPath(const SwString& rawPath) {
    const SwString normalized = swCreatorEditorNormalizePath(rawPath);
    const std::string stdPath = normalized.toStdString();
    const std::string::size_type slash = stdPath.find_last_of('/');
    if (slash == std::string::npos) {
        return normalized;
    }
    if (slash == 0) {
        return SwString("/");
    }
    if (slash == 2 && normalized.size() >= 2 && normalized[1] == ':') {
        return normalized.substr(0, slash + 1);
    }
    return normalized.substr(0, slash);
}

inline SwString swCreatorEditorFileName(const SwString& rawPath) {
    const SwFileInfo info(swCreatorEditorNormalizePath(rawPath).toStdString());
    return SwString(info.fileName());
}

inline SwString swCreatorEditorDirectoryName(const SwString& rawPath) {
    const SwString parent = swCreatorEditorParentPath(rawPath);
    if (parent.isEmpty() || parent == rawPath) {
        return swCreatorEditorFileName(rawPath);
    }
    return swCreatorEditorFileName(parent);
}

inline bool swCreatorEditorIsSameOrChildPath(const SwString& rawParentPath, const SwString& rawChildPath) {
    const SwString parentPath = swCreatorEditorNormalizePath(rawParentPath);
    const SwString childPath = swCreatorEditorNormalizePath(rawChildPath);
    if (parentPath.isEmpty() || childPath.isEmpty()) {
        return false;
    }
    if (parentPath == childPath) {
        return true;
    }

    SwString prefix = parentPath;
    if (!prefix.endsWith("/")) {
        prefix += "/";
    }
    return childPath.startsWith(prefix);
}

inline SwString swCreatorEditorResolveExistingPath(const SwString& rawPath) {
    if (rawPath.isEmpty()) {
        return swCreatorEditorNormalizePath(SwDir::currentPath());
    }

    const SwString directPath = swCreatorEditorNormalizePath(rawPath);
    const SwFileInfo directInfo(directPath.toStdString());
    if (directInfo.exists()) {
        return SwString(directInfo.absoluteFilePath());
    }

    const SwString candidate = swCreatorEditorNormalizePath(SwDir::currentPath() + "/" + rawPath);
    const SwFileInfo candidateInfo(candidate.toStdString());
    if (candidateInfo.exists()) {
        return SwString(candidateInfo.absoluteFilePath());
    }

    return directPath;
}
