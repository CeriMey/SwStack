#ifndef SWSTANDARDPATHS_H
#define SWSTANDARDPATHS_H
/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwString.h"
#include "SwList.h"
#include "Sw.h"
#include "SwDir.h"
#include "SwFileInfo.h"
#include "SwStandardLocation.h"
#include "platform/SwPlatformSelector.h"

#include <cstdlib>

#if !defined(_WIN32)
#include <unistd.h>
#endif

class SwStandardPaths {
public:
    enum StandardLocation {
        DesktopLocation,
        DocumentsLocation,
        PicturesLocation,
        MusicLocation,
        MoviesLocation,
        DownloadLocation,
        ApplicationsLocation,
        FontsLocation,
        HomeLocation,
        TempLocation,
        GenericDataLocation,
        GenericConfigLocation,
        AppDataLocation,
        AppConfigLocation,
        AppLocalDataLocation,
        CacheLocation,
        RuntimeLocation
    };

    enum LocateOption {
        LocateFile = 0x1,
        LocateDirectory = 0x2
    };
    using LocateOptions = SwFlagSet<LocateOption>;

    static SwString writableLocation(StandardLocation type) {
        SwList<SwString> locations = writableLocations(type);
        return locations.isEmpty() ? SwString() : locations[0];
    }

    static SwList<SwString> writableLocations(StandardLocation type) {
        SwList<SwString> results;
        if (isTestModeEnabled()) {
            SwString path = joinPath(testRootPath(), displayName(type));
            ensureDirectory(path);
            appendUnique(results, normalizeForPlatform(path));
            return results;
        }

        appendUnique(results, normalizeForPlatform(resolveLocation(type)));

        SwList<SwString> fallbacks = fallbackLocations(type);
        for (const SwString& entry : fallbacks) {
            appendUnique(results, entry);
        }

        if (results.isEmpty()) {
            SwString fallback = resolveLocation(HomeLocation);
            if (!fallback.isEmpty()) {
                appendUnique(results, normalizeForPlatform(fallback));
            }
        }
        return results;
    }

    static SwList<SwString> standardLocations(StandardLocation type) {
        SwList<SwString> results = writableLocations(type);
        if (results.isEmpty()) {
            SwString fallback = resolveLocation(HomeLocation);
            if (!fallback.isEmpty()) {
                appendUnique(results, normalizeForPlatform(fallback));
            }
        }
        return results;
    }

    static SwString displayName(StandardLocation type) {
        switch (type) {
        case DesktopLocation: return SwString("Desktop");
        case DocumentsLocation: return SwString("Documents");
        case PicturesLocation: return SwString("Pictures");
        case MusicLocation: return SwString("Music");
        case MoviesLocation: return SwString("Videos");
        case DownloadLocation: return SwString("Downloads");
        case ApplicationsLocation: return SwString("Applications");
        case FontsLocation: return SwString("Fonts");
        case HomeLocation: return SwString("Home");
        case TempLocation: return SwString("Temporary");
        case GenericDataLocation: return SwString("Data");
        case GenericConfigLocation: return SwString("Config");
        case AppDataLocation: return SwString("Application Data");
        case AppConfigLocation: return SwString("Application Config");
        case AppLocalDataLocation: return SwString("Application Local Data");
        case CacheLocation: return SwString("Cache");
        case RuntimeLocation: return SwString("Runtime");
        default: return SwString("Unknown");
        }
    }

    static void setTestModeEnabled(bool enabled) {
        bool& flag = testModeEnabledFlag();
        if (flag == enabled) {
            return;
        }
        flag = enabled;
        if (flag) {
            ensureDirectory(testRootPath());
        }
    }

    static bool isTestModeEnabled() {
        return testModeEnabledFlag();
    }

    static SwString findExecutable(const SwString& executableName,
                                   const SwList<SwString>& searchPaths = SwList<SwString>()) {
        if (executableName.isEmpty()) {
            return SwString();
        }

        SwString direct = SwDir::normalizePath(executableName);
        if (containsPathSeparator(direct)) {
            if (isExecutableFile(direct)) {
                return direct;
            }
#ifdef _WIN32
            if (!direct.toLower().endsWith(".exe")) {
                SwString withExe = direct + ".exe";
                if (isExecutableFile(withExe)) {
                    return withExe;
                }
            }
#endif
            return SwString();
        }

        SwList<SwString> paths = searchPaths;
        SwList<SwString> envPaths = environmentPaths();
        for (const SwString& entry : envPaths) {
            paths.append(entry);
        }

        for (const SwString& base : paths) {
            if (base.isEmpty()) {
                continue;
            }
            SwString candidate = joinPath(base, executableName);
            if (isExecutableFile(candidate)) {
                return SwDir::normalizePath(candidate);
            }
#ifdef _WIN32
            if (!candidate.toLower().endsWith(".exe")) {
                SwString withExe = candidate + ".exe";
                if (isExecutableFile(withExe)) {
                    return SwDir::normalizePath(withExe);
                }
            }
#endif
        }
        return SwString();
    }

    static SwString locate(StandardLocation type,
                           const SwString& fileName,
                           LocateOptions options = LocateFile) {
        if (fileName.isEmpty()) {
            return SwString();
        }
        if (isMatch(fileName, options)) {
            return SwDir::normalizePath(fileName);
        }
        SwList<SwString> candidates = standardLocations(type);
        for (const SwString& base : candidates) {
            SwString candidate = joinPath(base, fileName);
            if (isMatch(candidate, options)) {
                return SwDir::normalizePath(candidate);
            }
        }
        return SwString();
    }

    static SwList<SwString> locateAll(StandardLocation type,
                                      const SwString& fileName,
                                      LocateOptions options = LocateFile) {
        SwList<SwString> matches;
        if (fileName.isEmpty()) {
            return matches;
        }

        if (isMatch(fileName, options)) {
            matches.append(SwDir::normalizePath(fileName));
        }

        SwList<SwString> candidates = standardLocations(type);
        for (const SwString& base : candidates) {
            SwString candidate = joinPath(base, fileName);
            if (isMatch(candidate, options)) {
                matches.append(SwDir::normalizePath(candidate));
            }
        }
        return matches;
    }

private:
    static SwString resolveLocation(StandardLocation type) {
        if (isTestModeEnabled()) {
            return joinPath(testRootPath(), displayName(type));
        }
        SwStandardLocationId id = toLocationId(type);
        return SwStandardLocation::standardLocation(id);
    }

    static SwList<SwString> fallbackLocations(StandardLocation type) {
        SwList<SwString> results;
        auto appendId = [&results](SwStandardLocationId id) {
            SwString location = SwStandardLocation::standardLocation(id);
            if (!location.isEmpty()) {
                appendUnique(results, normalizeForPlatform(location));
            }
        };

        switch (type) {
        case ApplicationsLocation:
            appendId(SwStandardLocationId::ProgramFiles);
            appendId(SwStandardLocationId::ProgramFilesX86);
            appendId(SwStandardLocationId::ProgramFilesCommon);
            appendId(SwStandardLocationId::ProgramFilesCommonX86);
            break;
        case FontsLocation: {
            SwString windows = SwStandardLocation::standardLocation(SwStandardLocationId::Windows);
            if (!windows.isEmpty()) {
                appendUnique(results, normalizeForPlatform(joinPath(windows, "Fonts")));
            }
            break;
        }
        case GenericDataLocation:
            appendId(SwStandardLocationId::AppData);
            appendId(SwStandardLocationId::LocalAppData);
            appendId(SwStandardLocationId::RoamingAppData);
            break;
        case AppDataLocation:
            appendId(SwStandardLocationId::RoamingAppData);
            appendId(SwStandardLocationId::LocalAppData);
            break;
        case AppConfigLocation:
            appendId(SwStandardLocationId::Config);
            appendId(SwStandardLocationId::LocalAppData);
            break;
        case AppLocalDataLocation:
            appendId(SwStandardLocationId::LocalAppData);
            break;
        case CacheLocation:
            appendId(SwStandardLocationId::Cache);
            appendId(SwStandardLocationId::LocalAppData);
            break;
        default:
            break;
        }
        return results;
    }

    static SwStandardLocationId toLocationId(StandardLocation type) {
        switch (type) {
        case DesktopLocation: return SwStandardLocationId::Desktop;
        case DocumentsLocation: return SwStandardLocationId::Documents;
        case PicturesLocation: return SwStandardLocationId::Pictures;
        case MusicLocation: return SwStandardLocationId::Music;
        case MoviesLocation: return SwStandardLocationId::Videos;
        case DownloadLocation: return SwStandardLocationId::Downloads;
        case ApplicationsLocation: return SwStandardLocationId::ProgramFiles;
        case FontsLocation: return SwStandardLocationId::Windows;
        case HomeLocation: return SwStandardLocationId::Home;
        case TempLocation: return SwStandardLocationId::Temp;
        case GenericDataLocation: return SwStandardLocationId::AppData;
        case GenericConfigLocation: return SwStandardLocationId::Config;
        case AppDataLocation: return SwStandardLocationId::AppData;
        case AppConfigLocation: return SwStandardLocationId::Config;
        case AppLocalDataLocation: return SwStandardLocationId::LocalAppData;
        case CacheLocation: return SwStandardLocationId::Cache;
        case RuntimeLocation: return SwStandardLocationId::Temp;
        default: return SwStandardLocationId::Home;
        }
    }

    static bool& testModeEnabledFlag() {
        static bool enabled = false;
        return enabled;
    }

    static SwString& testRootStorage() {
        static SwString root;
        return root;
    }

    static SwString testRootPath() {
        SwString& root = testRootStorage();
        if (!root.isEmpty()) {
            return root;
        }
        SwString base = SwStandardPaths::writableLocation(TempLocation);
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(HomeLocation);
        }
        if (base.isEmpty()) {
            base = SwString("./SwStandardPathsTest");
        }
        root = joinPath(base, "SwStandardPathsTest");
        ensureDirectory(root);
        return root;
    }

    static bool isMatch(const SwString& path, LocateOptions options) {
        if (path.isEmpty()) {
            return false;
        }
        SwFileInfo info(path.toStdString());
        if (!info.exists()) {
            return false;
        }
        const bool wantDir = options.testFlag(LocateDirectory);
        const bool wantFile = options.testFlag(LocateFile) || !wantDir;
        if (info.isDir()) {
            return wantDir;
        }
        return wantFile;
    }

    static bool isExecutableFile(const SwString& path) {
        SwFileInfo info(path.toStdString());
        if (!info.exists() || info.isDir()) {
            return false;
        }
#if !defined(_WIN32)
        return access(path.toStdString().c_str(), X_OK) == 0;
#else
        return true;
#endif
    }

    static SwList<SwString> environmentPaths() {
        SwList<SwString> paths;
        const char* envPath = std::getenv("PATH");
        if (!envPath) {
            return paths;
        }
#ifdef _WIN32
        const char delimiter = ';';
#else
        const char delimiter = ':';
#endif
        SwString raw(envPath);
        SwList<SwString> parts = raw.split(delimiter);
        for (int i = 0; i < parts.size(); ++i) {
            SwString entry = parts[i].trimmed();
            if (entry.isEmpty()) {
                continue;
            }
            appendUnique(paths, normalizeForPlatform(entry));
        }
        return paths;
    }

    static bool containsPathSeparator(const SwString& path) {
        return path.contains("/") || path.contains("\\") || path.contains(":");
    }

    static void appendUnique(SwList<SwString>& list, const SwString& value) {
        if (value.isEmpty()) {
            return;
        }
        for (int i = 0; i < list.size(); ++i) {
            if (list[i] == value) {
                return;
            }
        }
        list.append(value);
    }

    static SwString normalizeForPlatform(const SwString& path) {
        if (path.isEmpty()) {
            return path;
        }
#ifdef _WIN32
        return SwStandardLocation::convertPath(path, SwStandardPathType::Windows);
#else
        return SwStandardLocation::convertPath(path, SwStandardPathType::Unix);
#endif
    }

    static bool ensureDirectory(const SwString& path) {
        if (path.isEmpty()) {
            return false;
        }
        SwString normalized = normalizeForPlatform(path);
        if (swDirPlatform().isDirectory(normalized)) {
            return true;
        }
        SwString working = normalized;
        working.replace("\\", "/");
        SwList<SwString> parts = working.split('/');
        SwString current;
        int startIndex = 0;
#ifdef _WIN32
        if (working.size() >= 2 && working[1] == ':') {
            const SwString drive = working.left(2);
            current = drive;
            while (startIndex < parts.size() && parts[startIndex].isEmpty()) {
                ++startIndex;
            }
            if (startIndex < parts.size() && parts[startIndex] == drive) {
                ++startIndex;
            }
        }
#endif
        for (int i = startIndex; i < parts.size(); ++i) {
            SwString part = parts[i];
            if (part.isEmpty()) {
                continue;
            }
            if (!current.isEmpty() && !current.endsWith("/") && !current.endsWith("\\")) {
                current += SwString("/");
            }
            current += part;
            SwString native = normalizeForPlatform(current);
            if (!swDirPlatform().isDirectory(native)) {
                if (!swDirPlatform().mkdir(native)) {
                    return false;
                }
            }
        }
        return true;
    }

    static SwString joinPath(const SwString& base, const SwString& child) {
        if (base.isEmpty()) {
            return child;
        }
        if (child.isEmpty()) {
            return base;
        }
        SwString result(base);
        if (!result.endsWith("/") && !result.endsWith("\\") && !result.endsWith(swDirPlatform().pathSeparator())) {
            result += swDirPlatform().pathSeparator();
        }
        result += child;
        return result;
    }
};

#endif // SWSTANDARDPATHS_H
