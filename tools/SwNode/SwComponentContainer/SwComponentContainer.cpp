#include "SwRemoteObjectComponent.h"
#include "SwRemoteObjectComponentRegistry.h"
#include "SwLibrary.h"
#include "SwTimer.h"
#include "SwRemoteObject.h"
#include "SwThread.h"
#include "SwRemoteObjectNode.h"

#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>

class SwComponentContainer : public SwRemoteObject {
 public:
    struct ComponentInfo {
        SwString type;
        SwString pluginPath;
        SwRemoteObject* instance{nullptr};
        SwRemoteObjectComponentRegistry::DestroyFn destroy{nullptr};
        SwJsonObject params;
        bool publishParamsToShm{true};
    };

    SwComponentContainer(const SwString& sysName,
                         const SwString& nameSpace,
                         const SwString& objectName,
                         SwObject* parent = nullptr)
        : SwRemoteObject(sysName, nameSpace, objectName, parent) {
        ipcExposeRpc(status, this, &SwComponentContainer::rpcStatus_, /*fireInitial=*/true);

        ipcExposeRpc(loadPlugin, this, &SwComponentContainer::rpcLoadPlugin_, /*fireInitial=*/true);
        ipcExposeRpc(listPlugins, this, &SwComponentContainer::rpcListPlugins_, /*fireInitial=*/true);
        ipcExposeRpc(listPluginsInfo, this, &SwComponentContainer::rpcListPluginsInfo_, /*fireInitial=*/true);

        ipcExposeRpc(loadComponent, this, &SwComponentContainer::rpcLoadComponent_, /*fireInitial=*/true);
        ipcExposeRpc(unloadComponent, this, &SwComponentContainer::rpcUnloadComponent_, /*fireInitial=*/true);
        ipcExposeRpc(restartComponent, this, &SwComponentContainer::rpcRestartComponent_, /*fireInitial=*/true);
        ipcExposeRpc(stopPlugin, this, &SwComponentContainer::rpcStopPlugin_, /*fireInitial=*/true);
        ipcExposeRpc(restartPlugin, this, &SwComponentContainer::rpcRestartPlugin_, /*fireInitial=*/true);
        ipcExposeRpc(listComponents, this, &SwComponentContainer::rpcListComponents_, /*fireInitial=*/true);

        ipcRegisterConfig(SwStringList, pluginPaths_, "composition/plugins", SwStringList(),
                          [this](const SwStringList&) { applyComposition_(); });

        SwAny def = SwAny::from<SwJsonArray>(SwJsonArray());
        ipcRegisterConfig(SwAny, compositionSpec_, "composition/components", def,
                          [this](const SwAny&) { applyComposition_(); });

        ipcRegisterConfig(SwString, threadingMode_, "composition/threading", SwString("same_thread"),
                          [this](const SwString&) { onThreadingModeChanged_(); });

        applyComposition_();
        SwTimer::singleShot(0, this, &SwComponentContainer::postInit_);
    }

    ~SwComponentContainer() override { shutdown_(); }

    // Manual API (also exposed over RPC).
    bool loadPlugin(const SwString& path) {
        SwString err;
        const bool ok = loadPlugin_(path, &err);
        if (!ok) {
            std::cerr << "[SwComponentContainer] loadPlugin failed: " << err.toStdString() << "\n";
        }
        return ok;
    }

    SwStringList listPlugins() const {
        SwStringList out;
        out.reserve(plugins_.size());
        for (std::map<std::string, SwLibrary>::const_iterator it = plugins_.begin(); it != plugins_.end(); ++it) {
            out.append(SwString(it->first));
        }
        return out;
    }

    SwRemoteObject* loadComponent(const SwString& typeName,
                                  const SwString& nameSpace,
                                  const SwString& objectName,
                                  const SwJsonObject& params = SwJsonObject(),
                                  bool publishParamsToShm = true) {
        SwString err;
        SwRemoteObject* inst =
            loadComponent_(typeName, nameSpace, objectName, params, publishParamsToShm, &err);
        if (!inst) {
            std::cerr << "[SwComponentContainer] loadComponent failed: " << err.toStdString() << "\n";
        }
        return inst;
    }

    bool unloadComponent(const SwString& targetObject) {
        SwString err;
        const bool ok = unloadComponent_(targetObject, &err);
        if (!ok) {
            std::cerr << "[SwComponentContainer] unloadComponent failed: " << err.toStdString() << "\n";
        }
        return ok;
    }

    SwJsonArray listComponentsJson() const {
        SwJsonArray out;
        for (auto it = components_.begin(); it != components_.end(); ++it) {
            const SwString objectFqn(it->first);
            const ComponentInfo& info = it->second;
            if (!info.instance) continue;

            SwJsonObject o;
            o["type"] = SwJsonValue(info.type.toStdString());
            o["plugin"] = SwJsonValue(info.pluginPath.toStdString());
            o["sys"] = SwJsonValue(sysName().toStdString());
            o["ns"] = SwJsonValue(info.instance->nameSpace().toStdString());
            o["name"] = SwJsonValue(info.instance->objectName().toStdString());
            o["objectFqn"] = SwJsonValue(objectFqn.toStdString());
            o["target"] = SwJsonValue(buildObjectFqn(sysName(), objectFqn).toStdString());
            out.append(SwJsonValue(o));
        }
        return out;
    }

    SwString listComponents() const {
        SwJsonDocument d;
        d.setArray(listComponentsJson());
        return d.toJson(SwJsonDocument::JsonFormat::Compact);
    }

  private:
    void postInit_() {
        const SwJsonArray desired = toArrayOrEmpty_(compositionSpec_);
        const bool havePlugins = (pluginPaths_.size() != 0);
        const bool haveComponents = (desired.size() != 0);
        if (!havePlugins && !haveComponents) {
            std::cout << "[SwComponentContainer] no composition provided (use config/RPC to load plugins and components)"
                      << std::endl;
        }
        logReady_();
    }

    void logReady_() const {
        std::cout << "[SwComponentContainer] ready: sys=" << sysName().toStdString()
                  << " container=" << buildObjectFqn(nameSpace(), objectName()).toStdString()
                  << " threading=" << threadingMode_.toStdString() << std::endl;
        std::cout << "[SwComponentContainer] loaded components:\n" << listComponents().toStdString() << std::endl;
    }

    SwString rpcStatus_() {
        SwJsonObject o;
        o["ok"] = SwJsonValue(true);
        o["sys"] = SwJsonValue(sysName().toStdString());
        o["container"] = SwJsonValue(buildObjectFqn(nameSpace(), objectName()).toStdString());
        o["threading"] = SwJsonValue(threadingMode_.toStdString());
        o["plugins"] = SwJsonValue(getArrayOrEmpty_(rpcListPlugins_()));
        o["pluginsInfo"] = SwJsonValue(getArrayOrEmpty_(rpcListPluginsInfo_()));
        o["components"] = SwJsonValue(listComponentsJson());
        return jsonObjectToString_(o);
    }

    bool isThreadPerPlugin_() const {
        const std::string m = threadingMode_.trimmed().toStdString();
        return (m == "thread_per_plugin") || (m == "thread-per-plugin") || (m == "per_plugin");
    }

    void onThreadingModeChanged_() {
        // Switching execution model requires recreating components so their thread affinity matches.
        shutdown_();
        applyComposition_();
    }

    bool splitTargetObjectFqn_(const SwString& targetObject, SwString& objectFqnOut) const {
        SwString x = targetObject.trimmed();
        x.replace("\\", "/");
        while (x.contains("//")) x.replace("//", "/");
        while (x.startsWith("/")) x = x.mid(1);
        while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
        if (x.isEmpty()) return false;

        // Accept either:
        //   - full: "sysName/nameSpace/objectName"
        //   - rel:  "nameSpace/objectName" (sysName taken from this container)
        const std::string sysPrefix = (sysName().isEmpty() ? std::string() : (sysName().toStdString() + "/"));
        const std::string s = x.toStdString();
        if (!sysPrefix.empty() && s.rfind(sysPrefix, 0) == 0) {
            objectFqnOut = SwString(s.substr(sysPrefix.size()));
            return !objectFqnOut.isEmpty();
        }

        objectFqnOut = x;
        return !objectFqnOut.isEmpty();
    }

    static SwString jsonObjectToString_(const SwJsonObject& o) {
        SwJsonDocument d;
        d.setObject(o);
        return d.toJson(SwJsonDocument::JsonFormat::Compact);
    }

    static SwJsonArray getArrayOrEmpty_(const SwString& jsonArray) {
        SwJsonDocument d;
        SwString err;
        if (!d.loadFromJson(jsonArray.trimmed().toStdString(), err)) return SwJsonArray();
        if (!d.isArray()) return SwJsonArray();
        return d.array();
    }

    static bool parseJsonObject_(const SwString& json, SwJsonObject& out, SwString* errOut) {
        out = SwJsonObject{};
        SwString s = json.trimmed();
        if (s.isEmpty()) {
            out = SwJsonObject{};
            return true;
        }

        SwJsonDocument d;
        SwString err;
        if (!d.loadFromJson(s.toStdString(), err)) {
            if (errOut) *errOut = err;
            return false;
        }
        if (!d.isObject()) {
            if (errOut) *errOut = SwString("expected a JSON object");
            return false;
        }
        out = d.object();
        return true;
    }

    static SwJsonArray toArrayOrEmpty_(const SwAny& v) {
        const SwJsonValue j = v.toJsonValue();
        if (j.isArray()) return j.toArray();
        if (v.typeName() == typeid(SwJsonArray).name()) return v.get<SwJsonArray>();
        return SwJsonArray();
    }

    static bool splitObjectFqnToNsName_(const SwString& objectFqn, SwString& nsOut, SwString& nameOut) {
        SwString x = objectFqn.trimmed();
        x.replace("\\", "/");
        while (x.contains("//")) x.replace("//", "/");
        while (x.startsWith("/")) x = x.mid(1);
        while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
        const size_t slash = x.lastIndexOf('/');
        if (slash == static_cast<size_t>(-1)) return false;
        nsOut = x.left(static_cast<int>(slash));
        nameOut = x.mid(static_cast<int>(slash + 1));
        return !nsOut.isEmpty() && !nameOut.isEmpty();
    }

    static SwString stripPluginLeafSuffix_(SwString leafLower) {
        leafLower = leafLower.toLower();
        if (leafLower.endsWith(".dll")) return leafLower.left(static_cast<int>(leafLower.size()) - 4);
        if (leafLower.endsWith(".dylib")) return leafLower.left(static_cast<int>(leafLower.size()) - 6);
        if (leafLower.endsWith(".so")) return leafLower.left(static_cast<int>(leafLower.size()) - 3);
        return leafLower;
    }

    SwString resolveLoadedPluginKey_(const SwString& pluginQuery) const {
        SwString q = pluginQuery.trimmed();
        q.replace("\\", "/");
        if (q.isEmpty()) return SwString();

        const std::string qStd = q.toStdString();
        if (plugins_.find(qStd) != plugins_.end()) {
            return q;
        }

        SwString qLeaf = q;
        const size_t qs = qLeaf.lastIndexOf('/');
        if (qs != static_cast<size_t>(-1)) qLeaf = qLeaf.mid(static_cast<int>(qs + 1));
        const SwString qLeafLower = qLeaf.toLower();
        const SwString qLeafNoExt = stripPluginLeafSuffix_(qLeafLower);

        SwString best;
        for (std::map<std::string, SwLibrary>::const_iterator it = plugins_.begin(); it != plugins_.end(); ++it) {
            SwString k(it->first);
            k.replace("\\", "/");
            SwString leaf = k;
            const size_t ks = leaf.lastIndexOf('/');
            if (ks != static_cast<size_t>(-1)) leaf = leaf.mid(static_cast<int>(ks + 1));
            const SwString leafLower = leaf.toLower();
            const SwString leafNoExt = stripPluginLeafSuffix_(leafLower);

            if (leafLower == qLeafLower || leafNoExt == qLeafNoExt) {
                if (!best.isEmpty() && best != k) {
                    return SwString(); // ambiguous
                }
                best = k;
            }
        }
        return best;
    }

    SwString rpcLoadPlugin_(const SwString& path) {
        SwString err;
        const bool ok = loadPlugin_(path, &err);
        SwJsonObject res;
        res["ok"] = SwJsonValue(ok);
        if (!ok) res["err"] = SwJsonValue(err.toStdString());
        return jsonObjectToString_(res);
    }

    SwString rpcListPlugins_() const {
        SwJsonArray a;
        for (std::map<std::string, SwLibrary>::const_iterator it = plugins_.begin(); it != plugins_.end(); ++it) {
            a.append(SwJsonValue(it->first));
        }
        SwJsonDocument d;
        d.setArray(a);
        return d.toJson(SwJsonDocument::JsonFormat::Compact);
    }

    SwString rpcListPluginsInfo_() const {
        SwJsonArray out;

        for (std::map<std::string, SwLibrary>::const_iterator it = plugins_.begin(); it != plugins_.end(); ++it) {
            const std::string& key = it->first;
            const SwLibrary& lib = it->second;

            SwJsonObject o;
            o["path"] = SwJsonValue(key);
            o["library"] = SwJsonValue(lib.introspectionJson());

            SwJsonArray types;
            const std::map<std::string, SwStringList>::const_iterator pit = pluginTypes_.find(key);
            if (pit != pluginTypes_.end()) {
                const SwStringList& lst = pit->second;
                for (size_t i = 0; i < lst.size(); ++i) {
                    types.append(SwJsonValue(lst[i].toStdString()));
                }
            }
            o["components"] = SwJsonValue(types);

            out.append(SwJsonValue(o));
        }

        SwJsonDocument d;
        d.setArray(out);
        return d.toJson(SwJsonDocument::JsonFormat::Compact);
    }

    SwString rpcLoadComponent_(const SwString& typeName,
                              const SwString& nameSpace,
                              const SwString& objectName,
                              const SwString& paramsJson) {
        SwJsonObject params;
        SwString parseErr;
        if (!parseJsonObject_(paramsJson, params, &parseErr)) {
            SwJsonObject res;
            res["ok"] = SwJsonValue(false);
            res["err"] = SwJsonValue((SwString("invalid paramsJson: ") + parseErr).toStdString());
            return jsonObjectToString_(res);
        }

        SwString err;
        SwRemoteObject* inst =
            loadComponent_(typeName, nameSpace, objectName, params, /*publishParamsToShm=*/true, &err);

        SwJsonObject res;
        res["ok"] = SwJsonValue(inst != nullptr);
        if (inst) {
            const SwString objectFqn = buildObjectFqn(inst->nameSpace(), inst->objectName());
            res["target"] = SwJsonValue(buildObjectFqn(sysName(), objectFqn).toStdString());
        } else {
            res["err"] = SwJsonValue(err.toStdString());
        }
        return jsonObjectToString_(res);
    }

    SwString rpcUnloadComponent_(const SwString& targetObject) {
        SwString err;
        const bool ok = unloadComponent_(targetObject, &err);
        SwJsonObject res;
        res["ok"] = SwJsonValue(ok);
        if (!ok) res["err"] = SwJsonValue(err.toStdString());
        return jsonObjectToString_(res);
    }

    SwString rpcRestartComponent_(const SwString& targetObject) {
        SwString obj;
        if (!splitTargetObjectFqn_(targetObject, obj)) {
            SwJsonObject res;
            res["ok"] = SwJsonValue(false);
            res["err"] = SwJsonValue("invalid targetObject (expected sys/ns/name or ns/name)");
            return jsonObjectToString_(res);
        }

        SwString err;
        SwRemoteObject* inst = restartComponent_(obj, &err);
        SwJsonObject res;
        res["ok"] = SwJsonValue(inst != nullptr);
        if (inst) {
            res["target"] = SwJsonValue(buildObjectFqn(sysName(), buildObjectFqn(inst->nameSpace(), inst->objectName())).toStdString());
        } else {
            res["err"] = SwJsonValue(err.toStdString());
        }
        return jsonObjectToString_(res);
    }

    SwString rpcStopPlugin_(const SwString& pluginQuery) {
        const SwString pluginKey = resolveLoadedPluginKey_(pluginQuery);
        if (pluginKey.isEmpty()) {
            SwJsonObject res;
            res["ok"] = SwJsonValue(false);
            res["err"] = SwJsonValue("plugin not found (use listPlugins to get keys)");
            return jsonObjectToString_(res);
        }

        SwJsonArray stopped;
        SwJsonArray failed;

        SwStringList keys;
        for (std::map<std::string, ComponentInfo>::const_iterator it = components_.begin(); it != components_.end(); ++it) {
            if (it->second.pluginPath != pluginKey) continue;
            keys.append(SwString(it->first));
        }

        for (size_t i = 0; i < keys.size(); ++i) {
            const SwString obj = keys[i];
            SwString e;
            const bool ok = unloadComponent_(obj, &e);
            if (ok) {
                stopped.append(SwJsonValue(obj.toStdString()));
            } else {
                SwJsonObject f;
                f["object"] = SwJsonValue(obj.toStdString());
                f["err"] = SwJsonValue(e.toStdString());
                failed.append(SwJsonValue(f));
            }
        }

        SwJsonObject res;
        res["ok"] = SwJsonValue(failed.size() == 0);
        res["plugin"] = SwJsonValue(pluginKey.toStdString());
        res["stopped"] = SwJsonValue(stopped);
        if (failed.size() != 0) res["failed"] = SwJsonValue(failed);
        return jsonObjectToString_(res);
    }

    SwString rpcRestartPlugin_(const SwString& pluginQuery) {
        const SwString pluginKey = resolveLoadedPluginKey_(pluginQuery);
        if (pluginKey.isEmpty()) {
            SwJsonObject res;
            res["ok"] = SwJsonValue(false);
            res["err"] = SwJsonValue("plugin not found (use listPlugins to get keys)");
            return jsonObjectToString_(res);
        }

        SwJsonArray restarted;
        SwJsonArray failed;

        SwStringList keys;
        for (std::map<std::string, ComponentInfo>::const_iterator it = components_.begin(); it != components_.end(); ++it) {
            if (it->second.pluginPath != pluginKey) continue;
            keys.append(SwString(it->first));
        }

        for (size_t i = 0; i < keys.size(); ++i) {
            const SwString obj = keys[i];
            SwString e;
            SwRemoteObject* inst = restartComponent_(obj, &e);
            if (inst) {
                restarted.append(SwJsonValue(obj.toStdString()));
            } else {
                SwJsonObject f;
                f["object"] = SwJsonValue(obj.toStdString());
                f["err"] = SwJsonValue(e.toStdString());
                failed.append(SwJsonValue(f));
            }
        }

        SwJsonObject res;
        res["ok"] = SwJsonValue(failed.size() == 0);
        res["plugin"] = SwJsonValue(pluginKey.toStdString());
        res["restarted"] = SwJsonValue(restarted);
        if (failed.size() != 0) res["failed"] = SwJsonValue(failed);
        return jsonObjectToString_(res);
    }

    SwString rpcListComponents_() const { return listComponents(); }

    ThreadHandle* ensurePluginThread_(const SwString& pluginPath) {
        if (pluginPath.isEmpty()) return nullptr;
        const std::string key = pluginPath.toStdString();

        auto it = pluginThreads_.find(key);
        if (it != pluginThreads_.end()) {
            return it->second ? it->second->handle() : nullptr;
        }

        std::string leaf = key;
        const size_t slash = leaf.find_last_of("/\\");
        if (slash != std::string::npos) leaf = leaf.substr(slash + 1);
        if (leaf.empty()) leaf = "PluginThread";

        std::unique_ptr<SwThread> th(new SwThread(SwString(leaf), this));
        if (!th->start()) return nullptr;

        ThreadHandle* handle = th->handle();
        pluginThreads_.insert(std::make_pair(key, std::move(th)));
        return handle;
    }

    bool loadPlugin_(const SwString& path, SwString* errOut) {
        if (errOut) *errOut = SwString();
        if (path.isEmpty()) {
            if (errOut) *errOut = SwString("plugin path is empty");
            return false;
        }

        // Load the library first; use the resolved path as canonical key.
        SwLibrary lib;
        if (!lib.load(path)) {
            if (errOut) *errOut = lib.lastError();
            return false;
        }

        const SwString resolved = lib.path();
        const std::string key = resolved.toStdString();
        if (plugins_.find(key) != plugins_.end()) {
            // Already loaded.
            lib.unload();
            return true;
        }

        void* sym = lib.resolve(sw::component::plugin::registerSymbolV1());
        if (!sym) {
            lib.unload();
            if (errOut) {
                *errOut = SwString("plugin missing symbol: ") + sw::component::plugin::registerSymbolV1();
            }
            return false;
        }

        sw::component::plugin::RegisterFnV1 fn = reinterpret_cast<sw::component::plugin::RegisterFnV1>(sym);
        if (!fn) {
            lib.unload();
            if (errOut) *errOut = SwString("invalid register function pointer");
            return false;
        }

        const SwStringList before = registry_.types();
        std::set<std::string> beforeSet;
        for (size_t i = 0; i < before.size(); ++i) beforeSet.insert(before[i].toStdString());

        const bool ok = fn(&registry_);
        if (!ok) {
            lib.unload();
            if (errOut) *errOut = SwString("plugin register returned false");
            return false;
        }

        const SwStringList after = registry_.types();
        SwStringList newTypes;
        for (size_t i = 0; i < after.size(); ++i) {
            const std::string t = after[i].toStdString();
            if (beforeSet.find(t) == beforeSet.end()) {
                registry_.setPluginPath(after[i], resolved);
                newTypes.append(after[i]);
            }
        }

        plugins_.insert(std::make_pair(key, std::move(lib)));
        pluginTypes_.insert(std::make_pair(key, newTypes));
        return true;
    }

    void applyComposition_() {
        // 1) Ensure plugins are loaded.
        for (size_t i = 0; i < pluginPaths_.size(); ++i) {
            SwString err;
            (void)loadPlugin_(pluginPaths_[i], &err);
            if (!err.isEmpty()) {
                std::cerr << "[SwComponentContainer] plugin load: " << pluginPaths_[i].toStdString()
                          << " err=" << err.toStdString() << "\n";
            }
        }

        // 2) Reconcile components from the composition spec.
        const SwJsonArray desired = toArrayOrEmpty_(compositionSpec_);
        reconcile_(desired);
    }

    struct Wanted {
        SwString type;
        SwString ns;
        SwString name;
        SwJsonObject params;
        bool activeWatchDog{false};
        bool reloadOnCrash{false};
    };

    void reconcile_(const SwJsonArray& desired) {
        std::map<std::string, Wanted> want;

        for (size_t i = 0; i < desired.size(); ++i) {
            const SwJsonValue v = desired[i];
            if (!v.isObject()) continue;
            SwJsonObject o(v.toObject());

            SwString type(o["type"].toString());
            if (type.isEmpty()) continue;

            SwString ns(o.contains("ns") ? SwString(o["ns"].toString()) : SwString());
            if (ns.isEmpty() && o.contains("namespace")) ns = SwString(o["namespace"].toString());
            if (ns.isEmpty()) ns = this->nameSpace();

            SwString name(o.contains("name") ? SwString(o["name"].toString()) : SwString());
            if (name.isEmpty() && o.contains("object")) name = SwString(o["object"].toString());
            if (name.isEmpty()) continue;

            SwJsonObject params;
            const SwJsonValue p = o["params"];
            if (p.isObject()) params = p.toObject();

            bool activeWatchDog = false;
            bool reloadOnCrash = false;
            if (o.contains("activeWatchDog")) activeWatchDog = o["activeWatchDog"].toBool();
            if (o.contains("reloadOnCrash")) reloadOnCrash = o["reloadOnCrash"].toBool();

            const SwJsonValue optv = o["options"];
            if (optv.isObject()) {
                const SwJsonObject opts(optv.toObject());
                if (opts.contains("watchdog")) activeWatchDog = opts["watchdog"].toBool();
                if (opts.contains("activeWatchDog")) activeWatchDog = opts["activeWatchDog"].toBool();
                if (opts.contains("reloadOnCrash")) reloadOnCrash = opts["reloadOnCrash"].toBool();
            }

            const SwString objectFqn = buildObjectFqn(ns, name);
            Wanted w;
            w.type = type;
            w.ns = ns;
            w.name = name;
            w.params = params;
            w.activeWatchDog = activeWatchDog;
            w.reloadOnCrash = reloadOnCrash;
            want[objectFqn.toStdString()] = w;
        }

        // Unload components no longer wanted.
        for (std::map<std::string, ComponentInfo>::iterator it = components_.begin(); it != components_.end();) {
            if (want.find(it->first) == want.end()) {
                ComponentInfo info = it->second;
                it = components_.erase(it);
                destroyComponent_(info);
            } else {
                ++it;
            }
        }

        // Load/patch wanted components.
        for (std::map<std::string, Wanted>::const_iterator it = want.begin(); it != want.end(); ++it) {
            const Wanted& w = it->second;
            SwRemoteObject* inst =
                loadComponent_(w.type, w.ns, w.name, w.params, /*publishParamsToShm=*/true, nullptr);
            applyNodeOptionsOnInstanceThread_(inst, w.activeWatchDog);
        }
    }

    void destroyComponent_(ComponentInfo& info) {
        if (!info.instance) return;
        SwRemoteObject* inst = info.instance;
        info.instance = nullptr;

        const SwRemoteObjectComponentRegistry::DestroyFn destroy = info.destroy;
        ThreadHandle* targetThread = inst->threadHandle();
        ThreadHandle* currentThread = ThreadHandle::currentThread();

        auto doDestroy = [inst, destroy]() {
            if (destroy) {
                destroy(inst);
            } else {
                delete inst;
            }
        };

        if (!targetThread || targetThread == currentThread) {
            doDestroy();
        } else {
            executeBlockingOnThread(targetThread, doDestroy);
        }
    }

    SwRemoteObject* loadComponent_(const SwString& typeName,
                                  const SwString& nameSpace,
                                  const SwString& objectName,
                                  const SwJsonObject& params,
                                  bool publishParamsToShm,
                                  SwString* errOut) {
        if (errOut) *errOut = SwString();

        if (typeName.isEmpty()) {
            if (errOut) *errOut = SwString("typeName is empty");
            return nullptr;
        }
        if (objectName.isEmpty()) {
            if (errOut) *errOut = SwString("objectName is empty");
            return nullptr;
        }

        const SwString effectiveNs = nameSpace.isEmpty() ? this->nameSpace() : nameSpace;
        const SwString objectFqn = buildObjectFqn(effectiveNs, objectName);
        const std::string key = objectFqn.toStdString();

        std::map<std::string, ComponentInfo>::iterator existing = components_.find(key);
        if (existing != components_.end()) {
            ComponentInfo& info = existing->second;
            if (!info.instance) {
                components_.erase(existing);
            } else if (info.type != typeName) {
                ComponentInfo old = info;
                components_.erase(existing);
                destroyComponent_(old);
            } else {
                applyParamsOnInstanceThread_(info.instance, params, publishParamsToShm);
                return info.instance;
            }
        }

        const SwRemoteObjectComponentRegistry::Entry regEntry = registry_.entry(typeName);
        if (!regEntry.create) {
            if (errOut) *errOut = SwString("unknown component type (plugin not loaded?): ") + typeName;
            return nullptr;
        }

        SwRemoteObject* inst = nullptr;

        if (isThreadPerPlugin_() && !regEntry.pluginPath.isEmpty()) {
            ThreadHandle* th = ensurePluginThread_(regEntry.pluginPath);
            if (th) {
                const SwString sysCopy = sysName();
                const SwString nsCopy = effectiveNs;
                const SwString objCopy = objectName;
                const SwRemoteObjectComponentRegistry::CreateFn createFn = regEntry.create;
                executeBlockingOnThread(th, [&inst, sysCopy, nsCopy, objCopy, createFn]() {
                    inst = createFn(sysCopy, nsCopy, objCopy, nullptr);
                });
            }
        }

        if (!inst) {
            inst = regEntry.create(sysName(), effectiveNs, objectName, this);
        }
        if (!inst) {
            if (errOut) *errOut = SwString("component factory returned null");
            return nullptr;
        }

        applyParamsOnInstanceThread_(inst, params, publishParamsToShm);

        ComponentInfo info;
        info.type = typeName;
        info.pluginPath = regEntry.pluginPath;
        info.instance = inst;
        info.destroy = regEntry.destroy;
        info.params = params;
        info.publishParamsToShm = publishParamsToShm;
        components_.insert(std::make_pair(key, info));

        return inst;
    }

    bool unloadComponent_(const SwString& targetObject, SwString* errOut) {
        if (errOut) *errOut = SwString();
        if (targetObject.isEmpty()) {
            if (errOut) *errOut = SwString("targetObject is empty");
            return false;
        }

        SwString obj;
        if (!splitTargetObjectFqn_(targetObject, obj)) {
            if (errOut) *errOut = SwString("invalid targetObject (expected sys/ns/name or ns/name)");
            return false;
        }

        const std::string key = obj.toStdString();
        std::map<std::string, ComponentInfo>::iterator it = components_.find(key);
        if (it == components_.end()) {
            if (errOut) *errOut = SwString("not found: ") + obj;
            return false;
        }

        ComponentInfo info = it->second;
        components_.erase(it);
        destroyComponent_(info);
        return true;
    }

    SwRemoteObject* restartComponent_(const SwString& targetObject, SwString* errOut) {
        if (errOut) *errOut = SwString();
        SwString obj;
        if (!splitTargetObjectFqn_(targetObject, obj)) {
            if (errOut) *errOut = SwString("invalid targetObject (expected sys/ns/name or ns/name)");
            return nullptr;
        }

        const std::string key = obj.toStdString();
        std::map<std::string, ComponentInfo>::iterator it = components_.find(key);
        if (it == components_.end()) {
            if (errOut) *errOut = SwString("not found: ") + obj;
            return nullptr;
        }

        ComponentInfo info = it->second;
        components_.erase(it);
        destroyComponent_(info);

        SwString ns, name;
        if (!splitObjectFqnToNsName_(obj, ns, name)) {
            if (errOut) *errOut = SwString("invalid object fqn (expected ns/name): ") + obj;
            return nullptr;
        }

        SwString err;
        SwRemoteObject* inst = loadComponent_(info.type, ns, name, info.params, info.publishParamsToShm, &err);
        if (!inst && errOut) *errOut = err;
        return inst;
    }

    static void applyParams_(SwRemoteObject* inst, const SwJsonObject& params, bool publishToShm) {
        if (!inst) return;
        SwJsonObject::Container data = params.data();
        for (SwJsonObject::Container::const_iterator it = data.begin(); it != data.end(); ++it) {
            const SwString path(it->first);
            (void)inst->setConfigValue(path, it->second, /*saveToDisk=*/false, /*publishToShm=*/publishToShm);
        }
    }

    static void applyNodeOptions_(bool activeWatchDog) {
        if (!activeWatchDog) return;
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (app) {
            app->activeWatchDog();
        }
    }

    void applyParamsOnInstanceThread_(SwRemoteObject* inst, const SwJsonObject& params, bool publishToShm) {
        if (!inst) return;
        ThreadHandle* targetThread = inst->threadHandle();
        ThreadHandle* currentThread = ThreadHandle::currentThread();
        if (!targetThread || targetThread == currentThread) {
            applyParams_(inst, params, publishToShm);
            return;
        }

        const SwJsonObject paramsCopy = params;
        executeBlockingOnThread(targetThread, [inst, paramsCopy, publishToShm]() {
            applyParams_(inst, paramsCopy, publishToShm);
        });
    }

    void applyNodeOptionsOnInstanceThread_(SwRemoteObject* inst, bool activeWatchDog) {
        if (!inst || !activeWatchDog) return;
        ThreadHandle* targetThread = inst->threadHandle();
        ThreadHandle* currentThread = ThreadHandle::currentThread();
        if (!targetThread || isSameThreadHandle_(targetThread, currentThread)) {
            applyNodeOptions_(activeWatchDog);
            return;
        }

        executeBlockingOnThread(targetThread, [activeWatchDog]() {
            SwComponentContainer::applyNodeOptions_(activeWatchDog);
        });
    }

    void shutdown_() {
        // 1) Destroy components (in their owning threads).
        for (std::map<std::string, ComponentInfo>::iterator it = components_.begin(); it != components_.end(); ++it) {
            ComponentInfo info = it->second;
            destroyComponent_(info);
        }
        components_.clear();

        // 2) Stop plugin threads.
        for (auto it = pluginThreads_.begin(); it != pluginThreads_.end(); ++it) {
            if (!it->second) continue;
            it->second->quit();
            it->second->wait();
        }
        pluginThreads_.clear();
    }

    SwStringList pluginPaths_{};
    SwAny compositionSpec_{};
    SwString threadingMode_{};

    SwRemoteObjectComponentRegistry registry_{};
    std::map<std::string, SwLibrary> plugins_{};
    std::map<std::string, SwStringList> pluginTypes_{};
    std::map<std::string, std::unique_ptr<SwThread>> pluginThreads_{};
    std::map<std::string, ComponentInfo> components_{};
};

SW_REMOTE_OBJECT_NODE(SwComponentContainer)
