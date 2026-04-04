#pragma once

/**
 * @file src/core/remote/SwRemoteObjectNode.h
 * @ingroup core_remote
 * @brief Declares the public interface exposed by SwRemoteObjectNode in the CoreSw remote and IPC
 * layer.
 *
 * This header belongs to the CoreSw remote and IPC layer. It provides the abstractions used to
 * expose objects across process boundaries and to transport data or signals between peers.
 *
 * Within that layer, this file focuses on the remote object node interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Remote-facing declarations in this area usually coordinate identity, proxying, serialization,
 * and synchronization across runtimes.
 *
 */

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

#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwFile.h"
#include "SwJsonDocument.h"
#include "SwRemoteObject.h"
#include "SwTimer.h"
static constexpr const char* kSwLogCategory_SwRemoteObjectNode = "sw.core.remote.swremoteobjectnode";


namespace sw {
namespace node {
namespace detail {

inline SwJsonObject objectOrEmpty_(const SwJsonValue& v) {
    if (v.isObject()) {
        return v.toObject();
    }
    return SwJsonObject();
}

inline bool loadJsonObjectFromArgs_(const SwCoreApplication& app, SwJsonObject& out, SwString& errOut) {
    errOut = SwString();
    out = SwJsonObject();

    const SwString configFile = app.getArgument("config_file", SwString());
    const SwString configJson = app.getArgument("config_json", SwString());

    SwString raw;
    if (!configFile.isEmpty()) {
        if (!SwFile::isFile(configFile)) {
            errOut = SwString("config_file not found: ") + configFile;
            return false;
        }
        SwFile f(configFile);
        if (!f.open(SwFile::Read)) {
            errOut = SwString("failed to open config_file: ") + configFile;
            return false;
        }
        raw = f.readAll();
    } else if (!configJson.isEmpty()) {
        raw = configJson;
    } else {
        return false;
    }

    SwJsonDocument doc;
    SwString parseErr;
    if (!doc.loadFromJson(raw, parseErr)) {
        errOut = SwString("json parse error: ") + parseErr;
        return false;
    }
    if (!doc.isObject()) {
        errOut = SwString("json root must be an object");
        return false;
    }

    out = doc.object();
    return true;
}

inline bool objectHasStructuredKeys_(const SwJsonObject& o) {
    return o.contains("sys") || o.contains("ns") || o.contains("name") || o.contains("namespace") || o.contains("object") ||
           o.contains("duration_ms") || o.contains("params") || o.contains("options") || o.contains("config_root");
}

inline SwJsonObject extractParams_(const SwJsonObject& cfg) {
    SwJsonObject out;

    // 1) Preferred: explicit params object.
    const SwJsonValue p = cfg["params"];
    if (p.isObject()) {
        out = p.toObject();
    }

    // 2) Allow "root params" style:
    //    keep any non-structured keys at the root (ex: { "composition": {...}, "options": {...} }).
    SwJsonObject::Container data = cfg.data();
    for (SwJsonObject::Container::const_iterator it = data.begin(); it != data.end(); ++it) {
        const SwString k(it->first);
        if (k == "sys" || k == "ns" || k == "name" || k == "namespace" || k == "object" || k == "duration_ms" ||
            k == "params" || k == "options" || k == "config_root") {
            continue;
        }
        if (!out.contains(k.toStdString())) {
            out[k.toStdString()] = it->second;
        }
    }

    // Backward-compatible: if the root has no structured keys, treat it as params directly.
    if (out.size() == 0 && !objectHasStructuredKeys_(cfg)) {
        return cfg;
    }

    return out;
}

inline SwJsonObject extractOptions_(const SwJsonObject& cfg) {
    const SwJsonValue o = cfg["options"];
    if (o.isObject()) {
        return o.toObject();
    }
    return SwJsonObject();
}

inline void applyOptions_(SwCoreApplication& app, const SwJsonObject& options) {
    if (options.contains("watchdog") && options["watchdog"].toBool()) {
        app.activeWatchDog();
    }
    if (options.contains("activeWatchDog") && options["activeWatchDog"].toBool()) {
        app.activeWatchDog();
    }
    // systemTray is handled by SwLaunch, not by the node itself
}

inline void applyParams_(SwRemoteObject& node,
                         const SwJsonObject& params,
                         bool saveToDisk,
                         bool publishToShm) {
    SwJsonObject::Container data = params.data();
    for (SwJsonObject::Container::const_iterator it = data.begin(); it != data.end(); ++it) {
        const SwString path(it->first);
        (void)node.setConfigValue(path, it->second, saveToDisk, publishToShm);
    }
}

inline SwString argOr_(const SwCoreApplication& app, const SwString& key, const SwString& def) {
    return app.getArgument(key, def);
}

inline bool argToBool_(const SwCoreApplication& app, const SwString& key, bool def) {
    if (!app.hasArgument(key)) return def;
    return app.getArgument(key, def ? "1" : "0").toInt() != 0;
}

} // namespace detail
} // namespace node
} // namespace sw

#define SW_NODE_MAIN_WITH_DEFAULTS(NodeType, DefaultNsLiteral, DefaultNameLiteral)                             \
    int main(int argc, char** argv) {                                                                         \
        SwCoreApplication app(argc, argv);                                                                     \
                                                                                                               \
        SwString sys = app.getArgument("sys", "demo");                                                         \
        SwString ns = app.getArgument("ns", SwString(DefaultNsLiteral));                                       \
        if (ns.isEmpty()) ns = app.getArgument("namespace", SwString(DefaultNsLiteral));                       \
        SwString name = app.getArgument("name", SwString(DefaultNameLiteral));                                 \
        if (name.isEmpty()) name = app.getArgument("object", SwString(DefaultNameLiteral));                    \
                                                                                                               \
        int durationMs = app.getArgument("duration_ms", "0").toInt();                                          \
        bool publishToShm = sw::node::detail::argToBool_(app, "publish_to_shm", true);                          \
        bool saveToDisk = sw::node::detail::argToBool_(app, "save_to_disk", false);                             \
        SwString configRoot = app.getArgument("config_root", SwString());                                      \
                                                                                                               \
        SwJsonObject cfg;                                                                                      \
        SwString cfgErr;                                                                                       \
        const bool hasCfg = sw::node::detail::loadJsonObjectFromArgs_(app, cfg, cfgErr);                        \
        if (!hasCfg && !cfgErr.isEmpty()) {                                                                    \
            swCError(kSwLogCategory_SwRemoteObjectNode) << "[SwNode] invalid config: " << cfgErr;                                                 \
            return 2;                                                                                           \
        }                                                                                                      \
                                                                                                               \
        if (hasCfg) {                                                                                           \
            if (!app.hasArgument("sys") && cfg.contains("sys")) sys = SwString(cfg["sys"].toString());          \
            if (!app.hasArgument("ns") && cfg.contains("ns")) ns = SwString(cfg["ns"].toString());              \
            if (!app.hasArgument("name") && cfg.contains("name")) name = SwString(cfg["name"].toString());      \
            if (!app.hasArgument("ns") && ns.isEmpty() && cfg.contains("namespace")) ns = SwString(cfg["namespace"].toString()); \
            if (!app.hasArgument("name") && name.isEmpty() && cfg.contains("object")) name = SwString(cfg["object"].toString()); \
            if (!app.hasArgument("duration_ms") && cfg.contains("duration_ms")) durationMs = cfg["duration_ms"].toInt();          \
            if (!app.hasArgument("config_root") && cfg.contains("config_root")) configRoot = SwString(cfg["config_root"].toString()); \
        }                                                                                                      \
                                                                                                               \
        if (ns.isEmpty()) ns = SwString(DefaultNsLiteral);                                                     \
        if (name.isEmpty()) name = SwString(DefaultNameLiteral);                                               \
                                                                                                               \
        SwJsonObject params;                                                                                   \
        SwJsonObject options;                                                                                  \
        if (hasCfg) {                                                                                           \
            params = sw::node::detail::extractParams_(cfg);                                                     \
            options = sw::node::detail::extractOptions_(cfg);                                                   \
        }                                                                                                      \
                                                                                                               \
        sw::node::detail::applyOptions_(app, options);                                                         \
                                                                                                               \
        NodeType node(sys, ns, name, nullptr);                                                                 \
        if (!configRoot.isEmpty()) {                                                                           \
            node.setConfigRootDirectory(configRoot);                                                           \
        }                                                                                                      \
        sw::node::detail::applyParams_(node, params, saveToDisk, publishToShm);                                 \
                                                                                                               \
        if (durationMs > 0) {                                                                                  \
            SwTimer::singleShot(durationMs, [&app]() { app.quit(); });                                          \
        }                                                                                                      \
        return app.exec();                                                                                     \
    }

#define SW_NODE_MAIN(NodeType) SW_NODE_MAIN_WITH_DEFAULTS(NodeType, "ns", #NodeType)

#define SW_REMOTE_OBJECT_NODE_WITH_DEFAULTS(NodeType, DefaultNsLiteral, DefaultNameLiteral) \
    SW_NODE_MAIN_WITH_DEFAULTS(NodeType, DefaultNsLiteral, DefaultNameLiteral)

#define SW_REMOTE_OBJECT_NODE_DEFAULT(NodeType) \
    SW_REMOTE_OBJECT_NODE_WITH_DEFAULTS(NodeType, "ns", #NodeType)

#define SW_REMOTE_OBJECT_NODE_INVALID(...) static_assert(false, "SW_REMOTE_OBJECT_NODE expects 1 or 3 arguments")

#define SW_REMOTE_OBJECT_NODE_SELECT(_1, _2, _3, NAME, ...) NAME

#define SW_REMOTE_OBJECT_NODE(...) \
    SW_REMOTE_OBJECT_NODE_SELECT(__VA_ARGS__, SW_REMOTE_OBJECT_NODE_WITH_DEFAULTS, SW_REMOTE_OBJECT_NODE_INVALID, SW_REMOTE_OBJECT_NODE_DEFAULT)(__VA_ARGS__)
