#include "DemoSubscriber.h"

#include <algorithm>
#include <iostream>
#include <vector>

static void printJsonObject(const SwJsonObject& obj) {
    SwJsonDocument d;
    d.setObject(obj);
    std::cout << d.toJson(SwJsonDocument::JsonFormat::Pretty).toStdString() << "\n";
}

static void printStringList(const SwStringList& v) {
    std::cout << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << v[i].toStdString();
    }
    std::cout << "]";
}

static void printIntList(const SwList<int>& v) {
    std::cout << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << v[i];
    }
    std::cout << "]";
}

static void printAnyList(const SwAnyList& v) {
    std::cout << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) std::cout << ", ";
        const SwJsonValue j = v[i].toJsonValue();
        if (!j.isNull()) {
            std::cout << j.toJsonString();
        } else {
            std::cout << "\"" << v[i].toString().toStdString() << "\"";
        }
    }
    std::cout << "]";
}

template <typename V>
static void printMap(const SwMap<SwString, V>& m) {
    std::cout << "{";
    bool first = true;
    for (auto it = m.begin(); it != m.end(); ++it) {
        if (!first) std::cout << ", ";
        first = false;
        std::cout << it.key().toStdString() << ": ";
        std::cout << it.value();
    }
    std::cout << "}";
}

static void printAnyMap(const SwMap<SwString, SwAny>& m) {
    std::cout << "{";
    bool first = true;
    for (auto it = m.begin(); it != m.end(); ++it) {
        if (!first) std::cout << ", ";
        first = false;
        std::cout << it.key().toStdString() << ": ";
        const SwJsonValue j = it.value().toJsonValue();
        if (!j.isNull()) std::cout << j.toJsonString();
        else std::cout << "\"" << it.value().toString().toStdString() << "\"";
    }
    std::cout << "}";
}

static void printTargetRegistry(const SwString& sysName, const SwString& objectFqn) {
    SwJsonArray all = sw::ipc::shmRegistrySnapshot(sysName);

    struct Entry {
        std::string signal;
        std::string typeName;
        std::string shmName;
        uint32_t pid;
        uint64_t lastSeenMs;
    };
    std::vector<Entry> entries;

    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject() || !v.toObject()) continue;
        const SwJsonObject o(*v.toObject());

        if (SwString(o["domain"].toString()) != sysName) continue;
        if (SwString(o["object"].toString()) != objectFqn) continue;

        Entry e;
        e.signal = SwString(o["signal"].toString()).toStdString();
        e.typeName = SwString(o["typeName"].toString()).toStdString();
        e.shmName = SwString(o["shmName"].toString()).toStdString();
        e.pid = static_cast<uint32_t>(o["pid"].toInt());
        e.lastSeenMs = static_cast<uint64_t>(o["lastSeenMs"].toDouble());
        entries.push_back(e);
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.signal != b.signal) return a.signal < b.signal;
        return a.pid < b.pid;
    });

    std::cout << "[SUB] IPC registry for target " << (sysName + "/" + objectFqn).toStdString()
              << " (" << entries.size() << " entries)\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        std::cout << "  - " << e.signal
                  << " (pid=" << e.pid
                  << ", shm=" << e.shmName
                  << ", type=" << e.typeName
                  << ")\n";
    }
}

int DemoSubscriber::add(int a, int b) const {
    return a + b;
}

SwString DemoSubscriber::who(const sw::ipc::RpcContext& ctx) const {
    return ctx.clientInfo;
}

SwString DemoSubscriber::hello(const sw::ipc::RpcContext& ctx, const SwString& name) const {
    return SwString("hello ") + name + " (from " + ctx.clientInfo + ")";
}

void DemoSubscriber::notify(const sw::ipc::RpcContext& ctx, const SwString& msg) const {
    std::cout << "[SUB::rpc notify] from=" << ctx.clientInfo.toStdString()
              << " msg=" << msg.toStdString() << "\n";
}

    DemoSubscriber::DemoSubscriber(const SwString& sysName,
                               const SwString& nameSpace,
                               const SwString& objectName,
                               SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent) {
    ipcRegisterConfig(int, exposure_, "exposure", 0, [](const int& v) {
        std::cout << "[SUB::cfg] exposure=" << v << "\n";
    });
    ipcRegisterConfig(int, brightness_, "image/brightness", 10, [](const int& v) {
        std::cout << "[SUB::cfg] image/brightness=" << v << "\n";
    });
    ipcRegisterConfig(bool, enabled_, "device/enabled", true, [](const bool& v) {
        std::cout << "[SUB::cfg] device/enabled=" << (v ? "true" : "false") << "\n";
    });
    ipcRegisterConfig(double, gain_, "image/gain", 1.0, [](const double& v) {
        std::cout << "[SUB::cfg] image/gain=" << v << "\n";
    });
    ipcRegisterConfig(int, imageWidth_, "image/width", 1920, [](const int& v) {
        std::cout << "[SUB::cfg] image/width=" << v << "\n";
    });
    ipcRegisterConfig(int, imageHeight_, "image/height", 1080, [](const int& v) {
        std::cout << "[SUB::cfg] image/height=" << v << "\n";
    });
    ipcRegisterConfig(SwString, modeName_, "mode/name", "auto", [](const SwString& v) {
        std::cout << "[SUB::cfg] mode/name=" << v.toStdString() << "\n";
    });
    ipcRegisterConfig(SwString, modeProfile_, "mode/profile", "standard", [](const SwString& v) {
        std::cout << "[SUB::cfg] mode/profile=" << v.toStdString() << "\n";
    });

    // Sw containers (arrays/objects) stored as JSON in the config.
    using IntList = SwList<int>;
    using ThresholdMap = SwMap<SwString, int>;
    using AnyMap = SwMap<SwString, SwAny>;

    SwStringList defTags;
    defTags.append(SwString("rgb"));
    defTags.append(SwString("hdr"));
    ipcRegisterConfig(SwStringList, tags_, "lists/tags",
                      defTags,
                      [](const SwStringList& v) {
                          std::cout << "[SUB::cfg] lists/tags=";
                          printStringList(v);
                          std::cout << "\n";
                      });

    IntList defHistogram;
    defHistogram.append(0);
    defHistogram.append(1);
    defHistogram.append(2);
    defHistogram.append(3);
    ipcRegisterConfig(IntList, histogram_, "lists/histogram",
                      defHistogram,
                      [](const IntList& v) {
                          std::cout << "[SUB::cfg] lists/histogram=";
                          printIntList(v);
                          std::cout << "\n";
                      });

    SwAnyList defAnyList;
    defAnyList.append(SwAny(1));
    defAnyList.append(SwAny(true));
    defAnyList.append(SwAny(SwString("hello")));
    ipcRegisterConfig(SwAnyList, anyList_, "lists/any",
                      defAnyList,
                      [](const SwAnyList& v) {
                          std::cout << "[SUB::cfg] lists/any=";
                          printAnyList(v);
                          std::cout << "\n";
                      });

    {
        ThresholdMap def;
        def.insert("low", 10);
        def.insert("high", 20);
        ipcRegisterConfig(ThresholdMap, thresholds_, "maps/thresholds", def,
                          [](const ThresholdMap& v) {
                              std::cout << "[SUB::cfg] maps/thresholds=";
                              printMap(v);
                              std::cout << "\n";
                          });
    }

    {
        AnyMap def;
        def.insert("mode", SwAny(SwString("auto")));
        def.insert("enabled", SwAny(true));
        def.insert("gain", SwAny(1.25));
        ipcRegisterConfig(AnyMap, anyMap_, "maps/any", def,
        [](const AnyMap& v) {
            std::cout << "[SUB::cfg] maps/any=";
            printAnyMap(v);
            std::cout << "\n";
        });
    }

    // RPC examples (multi-client, ringbuffer-based).
    // 1) Method-pointer style (name auto: &DemoSubscriber::add -> "add")
    ipcExposeRpc(&DemoSubscriber::add);
    ipcExposeRpc(&DemoSubscriber::who);
    ipcExposeRpc(&DemoSubscriber::hello);
    ipcExposeRpc(&DemoSubscriber::notify);

    // 2) Lambda style (identifier name + string-literal name)
    ipcExposeRpc(sub, [](int a, int b) { return a - b; });
    ipcExposeRpc(mul, [](int a, int b) { return a * b; });
    ipcExposeRpc(whoLambda, [](const sw::ipc::RpcContext& ctx) { return ctx.clientInfo; });
}

void DemoSubscriber::start() {
    const SwString objectFqn = buildObjectFqn(nameSpace(), objectName());
    const SwString target = buildObjectFqn(sysName(), objectFqn);
    std::cout << "[SUB] target=" << target.toStdString() << "\n";

    std::cout << "[SUB] initial config (loaded from disk):\n"
              << "  exposure=" << exposure_ << "\n"
              << "  image/brightness=" << brightness_ << "\n"
              << "  device/enabled=" << (enabled_ ? "true" : "false") << "\n"
              << "  image/gain=" << gain_ << "\n"
              << "  image/width=" << imageWidth_ << "\n"
              << "  image/height=" << imageHeight_ << "\n"
              << "  mode/name=" << modeName_.toStdString() << "\n"
              << "  mode/profile=" << modeProfile_.toStdString() << "\n";

    printTargetRegistry(sysName(), objectFqn);

    ipcConnect(target, "ping", this, [this](int n, SwString s) {
        std::cout << "[SUB::ping] n=" << n << " s=" << s.toStdString() << "\n";
        emit pong(n, SwString("ack"));
    }, /*fireInitial=*/false);

    ipcConnect(target, "alarm", this, [](bool on) {
        std::cout << "[SUB::alarm] " << (on ? "ON" : "OFF") << "\n";
    }, /*fireInitial=*/false);

    ipcConnect(target, "note", this, [](SwString s) {
        std::cout << "[SUB::note] " << s.toStdString() << "\n";
    }, /*fireInitial=*/false);

    ipcConnect(target, "setPoint", this, [](double v) {
        std::cout << "[SUB::setPoint] " << v << "\n";
    }, /*fireInitial=*/false);

    ipcConnect(target, "vec3", this, [](int x, int y, int z) {
        std::cout << "[SUB::vec3] [" << x << "," << y << "," << z << "]\n";
    }, /*fireInitial=*/false);

    ipcConnect(target, "telemetry", this, [](int seq, double v, SwString tag) {
        std::cout << "[SUB::telemetry] seq=" << seq << " v=" << v << " tag=" << tag.toStdString() << "\n";
    }, /*fireInitial=*/false);

    ipcConnect(target, "blob", this, [](SwByteArray b) {
        std::cout << "[SUB::blob] size=" << b.size() << " bytes\n";
    }, /*fireInitial=*/false);

    ipcConnect(target, "identity", this, [](uint64_t id, SwString a, SwString b) {
        std::cout << "[SUB::identity] id=" << static_cast<unsigned long long>(id)
                  << " a=" << a.toStdString() << " b=" << b.toStdString() << "\n";
    }, /*fireInitial=*/false);

    ipcConnect(target, "triple", this, [](bool flag, int code, SwString name) {
        std::cout << "[SUB::triple] flag=" << (flag ? "true" : "false")
                  << " code=" << code
                  << " name=" << name.toStdString() << "\n";
    }, /*fireInitial=*/false);

    SwObject::connect(this, &SwRemoteObject::configChanged, [](const SwJsonObject& cfg) {
        std::cout << "[SUB::configChanged]\n";
        printJsonObject(cfg);
    });

    SwObject::connect(this, &SwRemoteObject::remoteConfigValueReceived,
                      [this](uint64_t pubId, const SwString& cfgName) {
                          std::cout << "[SUB::remoteConfigValueReceived] from pubId=" << static_cast<unsigned long long>(pubId)
                                    << " cfg=" << cfgName.toStdString() << "\n";
                          emit configAck(pubId, cfgName);
                      });

    std::cout << "[SUB] ready (Ctrl+C to exit)\n";
}
