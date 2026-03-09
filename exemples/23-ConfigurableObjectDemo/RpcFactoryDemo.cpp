#include "SwCoreApplication.h"
#include "SwDir.h"
#include "SwProxyObject.h"
#include "SwProxyObjectBrowser.h"
#include "SwProcess.h"
#include "SwRemoteObject.h"
#include "SwSharedMemorySignal.h"
#include "SwTimer.h"

#include <iostream>
#include <map>
#include <string>

SW_PROXY_OBJECT_CLASS_BEGIN(DemoRemote)
    SW_PROXY_OBJECT_RPC(int, add, int, int)
    SW_PROXY_OBJECT_RPC(int, sub, int, int)
    SW_PROXY_OBJECT_RPC(int, mul, int, int)
    SW_PROXY_OBJECT_RPC(SwString, who)
    SW_PROXY_OBJECT_RPC(SwString, whoLambda)
    SW_PROXY_OBJECT_RPC(SwString, hello, SwString)
    SW_PROXY_OBJECT_VOID(notify, SwString)
SW_PROXY_OBJECT_CLASS_END()

static bool listContains_(const SwStringList& list, const SwString& value) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == value) return true;
    }
    return false;
}

static SwStringList registryObjects_(const SwString& domain) {
    SwStringList out;
    if (domain.isEmpty()) return out;

    const SwJsonArray snap = sw::ipc::shmRegistrySnapshot(domain);
    std::map<std::string, bool> uniq;
    for (size_t i = 0; i < snap.size(); ++i) {
        const SwJsonValue v = snap[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        const SwString obj(o["object"].toString());
        if (obj.isEmpty()) continue;
        if (obj == SwString("__sw_ipc__")) continue;
        uniq[obj.toStdString()] = true;
    }

    out.reserve(uniq.size());
    for (auto it = uniq.begin(); it != uniq.end(); ++it) {
        out.append(SwString(it->first));
    }
    return out;
}

static void printList_(const char* label, const SwStringList& list) {
    std::cout << label << " (" << list.size() << ")\n";
    for (size_t i = 0; i < list.size(); ++i) {
        std::cout << "  - " << list[i].toStdString() << "\n";
    }
}

enum class ServerKind {
    Good,
    BadMissing,
    BadType,
};

static bool parseServerKind_(const SwString& s, ServerKind& out) {
    if (s == SwString("good")) {
        out = ServerKind::Good;
        return true;
    }
    if (s == SwString("bad-missing")) {
        out = ServerKind::BadMissing;
        return true;
    }
    if (s == SwString("bad-type")) {
        out = ServerKind::BadType;
        return true;
    }
    return false;
}

class DemoService : public SwRemoteObject {
public:
    DemoService(const SwString& sysName,
                const SwString& nameSpace,
                const SwString& objectName,
                ServerKind kind,
                SwObject* parent = nullptr)
        : SwRemoteObject(sysName, nameSpace, objectName, parent), kind_(kind) {
        if (kind_ == ServerKind::BadMissing) {
            ipcExposeRpc(add, this, &DemoService::add);
            ipcExposeRpc(who, [](const sw::ipc::RpcContext& ctx) { return ctx.clientInfo; });
            return;
        }

        if (kind_ == ServerKind::BadType) {
            ipcExposeRpc(add, [](double a, double b) { return static_cast<int>(a + b); });
        } else {
            ipcExposeRpc(add, this, &DemoService::add);
        }

        ipcExposeRpc(sub, this, &DemoService::sub);
        ipcExposeRpc(mul, this, &DemoService::mul);
        ipcExposeRpc(who, [](const sw::ipc::RpcContext& ctx) { return ctx.clientInfo; });
        ipcExposeRpc(whoLambda, []() { return SwString("whoLambda"); });
        ipcExposeRpc(hello, [](const SwString& name) { return SwString("hello ") + name; });
        ipcExposeRpc(notify, [](const SwString& msg) {
            std::cout << "[server] notify: " << msg.toStdString() << "\n";
        });
    }

    int add(int a, int b) { return a + b; }
    int sub(int a, int b) { return a - b; }
    int mul(int a, int b) { return a * b; }

private:
    ServerKind kind_;
};

static void usage_() {
    std::cout
        << "RpcFactoryDemo\n"
        << "\n"
        << "Server mode:\n"
        << "  RpcFactoryDemo.exe --server <good|bad-missing|bad-type> <sys> <namespace> <objectName>\n"
        << "\n"
        << "Monitor mode:\n"
        << "  RpcFactoryDemo.exe --monitor <sys> <filter>\n"
        << "    filter: '*', 'namespace/*', 'namespace/objectName', '*/objectName'\n"
        << "\n"
        << "Orchestrated multi-process test:\n"
        << "  RpcFactoryDemo.exe --orchestrate [sys]\n"
        << "    (default sys is unique per run: demo_factory_test_<pid>)\n";
}

class Orchestrator : public SwObject {
public:
    Orchestrator(SwCoreApplication* app, const SwString& exePath, const SwString& sys)
        : SwObject(nullptr),
          app_(app),
          exePath_(exePath),
          sys_(sys),
          startMs_(sw::ipc::detail::nowMs()),
          goodFqn_(SwString("nsOk/good")),
          nsMismatchFqn_(SwString("nsBad/nsMismatch")),
          badMissingFqn_(SwString("nsOk/badMissing")),
          badTypeFqn_(SwString("nsOk/badType")),
          factoryAll_(new SwProxyObjectBrowser<DemoRemote>(sys_, SwString("*"), SwString("factoryAll"), this)),
          factoryNsOk_(new SwProxyObjectBrowser<DemoRemote>(sys_, SwString("nsOk/*"), SwString("factoryNsOk"), this)) {
        SwObject::connect(factoryAll_, &SwProxyObjectBrowser<DemoRemote>::remoteAppeared, this, &Orchestrator::onAllAppeared_);
        SwObject::connect(factoryAll_, &SwProxyObjectBrowser<DemoRemote>::remoteDisappeared, this, &Orchestrator::onAllDisappeared_);
        SwObject::connect(factoryNsOk_, &SwProxyObjectBrowser<DemoRemote>::remoteAppeared, this, &Orchestrator::onNsOkAppeared_);
        SwObject::connect(factoryNsOk_, &SwProxyObjectBrowser<DemoRemote>::remoteDisappeared, this, &Orchestrator::onNsOkDisappeared_);

        SwTimer::singleShot(15000, [this]() {
            if (done_) return;
            fail_("global timeout");
            finish_();
        });

        SwTimer::singleShot(50, [this]() { step0_spawnGood_(); });
    }

private:
    typedef SwProxyObjectBrowser<DemoRemote> Factory;
    typedef Factory::Instance Instance;

    uint64_t elapsedMs_() const {
        const uint64_t now = sw::ipc::detail::nowMs();
        return (now >= startMs_) ? (now - startMs_) : 0;
    }

    void logFactory_(const char* tag, const SwPointer<Instance>& inst) {
        if (!inst) {
            std::cout << "t=" << elapsedMs_() << "ms " << tag << " <null>\n";
            return;
        }
        const DemoRemote& r = inst->remote();
        std::cout
            << "t=" << elapsedMs_() << "ms "
            << tag
            << " target=" << r.target().toStdString()
            << " pid=" << r.remotePid()
            << " alive=" << (r.isAlive() ? "true" : "false")
            << "\n";
    }

    void fail_(const std::string& msg) {
        ok_ = false;
        std::cerr << "[FAIL] " << msg << "\n";
    }

    SwProcess* spawn_(const SwString& kind, const SwString& ns, const SwString& obj) {
        SwProcess* p = new SwProcess(this);
        SwStringList args;
        args.append(SwString("--server"));
        args.append(kind);
        args.append(sys_);
        args.append(ns);
        args.append(obj);

        const SwString wd = SwDir::currentPath();
        const bool started = p->start(exePath_, args, ProcessFlags::CreateNoWindow, wd);
        if (!started) {
            fail_(std::string("cannot start server: ") + kind.toStdString());
            delete p;
            return nullptr;
        }
        return p;
    }

    void kill_(SwProcess*& p) {
        if (!p) return;
        if (p->isOpen()) {
            p->kill();
        }
        delete p;
        p = nullptr;
    }

    void step0_spawnGood_() {
        std::cout << "[STEP] spawn good\n";
        pGood_ = spawn_(SwString("good"), SwString("nsOk"), SwString("good"));
        if (!pGood_) {
            finish_();
            return;
        }

        // Validate that remoteAppeared is emitted (factory is event-driven, no polling here).
        SwTimer::singleShot(1200, [this]() {
            if (done_) return;
            if (!seenGoodAll_ || !seenGoodNsOk_) {
                fail_("remoteAppeared was not emitted for good within 1.2s (factoryAll and/or factoryNsOk)");
                finish_();
            }
        });
    }

    void step1_spawnNsMismatch_() {
        std::cout << "[STEP] spawn ns mismatch (good iface but nsBad/*)\n";
        pNsMismatch_ = spawn_(SwString("good"), SwString("nsBad"), SwString("nsMismatch"));
        if (!pNsMismatch_) {
            finish_();
            return;
        }

        SwTimer::singleShot(1200, [this]() {
            if (done_) return;
            if (!seenNsAll_) {
                fail_("remoteAppeared was not emitted for ns mismatch within 1.2s (factoryAll)");
                finish_();
            }
        });
    }

    void step2_spawnBadOnes_() {
        std::cout << "[STEP] spawn bad iface (missing) and bad type (type mismatch)\n";
        pBadMissing_ = spawn_(SwString("bad-missing"), SwString("nsOk"), SwString("badMissing"));
        pBadType_ = spawn_(SwString("bad-type"), SwString("nsOk"), SwString("badType"));

        SwTimer::singleShot(600, [this]() {
            if (done_) return;
            validatePresence_();
            step3_killGood_();
        });
    }

    void step3_killGood_() {
        std::cout << "[STEP] kill good\n";
        kill_(pGood_);

        SwTimer::singleShot(2500, [this]() {
            if (done_) return;
            if (!goneGoodAll_ || !goneGoodNsOk_) {
                fail_("remoteDisappeared was not emitted for good within 2.5s (factoryAll and/or factoryNsOk)");
                finish_();
            }
        });
    }

    void step4_killNsMismatch_() {
        std::cout << "[STEP] kill ns mismatch\n";
        kill_(pNsMismatch_);

        SwTimer::singleShot(2500, [this]() {
            if (done_) return;
            if (!goneNsAll_) {
                fail_("remoteDisappeared was not emitted for ns mismatch within 2.5s (factoryAll)");
                finish_();
            }
        });
    }

    void step5_killBadOnes_() {
        std::cout << "[STEP] kill bad ones\n";
        kill_(pBadMissing_);
        kill_(pBadType_);
    }

    void validatePresence_() {
        std::cout << "[CHECK] registry snapshot + candidates\n";

        const SwStringList objs = registryObjects_(sys_);
        printList_("  registry objects", objs);

        const SwStringList candStrict = DemoRemote::candidates(sys_, /*requireTypeMatch=*/true);
        printList_("  candidates requireTypeMatch=true", candStrict);

        const SwStringList candLoose = DemoRemote::candidates(sys_, /*requireTypeMatch=*/false);
        printList_("  candidates requireTypeMatch=false", candLoose);

        const SwString goodTarget = sys_ + "/" + goodFqn_;
        const SwString nsTarget = sys_ + "/" + nsMismatchFqn_;
        const SwString missingTarget = sys_ + "/" + badMissingFqn_;
        const SwString badTypeTarget = sys_ + "/" + badTypeFqn_;

        if (!listContains_(objs, goodFqn_)) fail_("good not present in registry snapshot");
        if (!listContains_(objs, nsMismatchFqn_)) fail_("ns mismatch not present in registry snapshot");
        if (!listContains_(objs, badMissingFqn_)) fail_("bad-missing not present in registry snapshot");
        if (!listContains_(objs, badTypeFqn_)) fail_("bad-type not present in registry snapshot");

        if (!listContains_(candStrict, goodTarget)) fail_("good not present in candidates(strict)");
        if (!listContains_(candStrict, nsTarget)) fail_("ns mismatch not present in candidates(strict)");
        if (listContains_(candStrict, missingTarget)) fail_("bad-missing unexpectedly present in candidates(strict)");
        if (listContains_(candStrict, badTypeTarget)) fail_("bad-type unexpectedly present in candidates(strict)");

        if (!listContains_(candLoose, goodTarget)) fail_("good not present in candidates(loose)");
        if (!listContains_(candLoose, nsTarget)) fail_("ns mismatch not present in candidates(loose)");
        if (listContains_(candLoose, missingTarget)) fail_("bad-missing unexpectedly present in candidates(loose)");
        if (!listContains_(candLoose, badTypeTarget)) fail_("bad-type not present in candidates(loose)");
    }

    void finish_() {
        if (done_) return;
        done_ = true;

        // Validate the factory signals/counts (the whole scenario is driven by these events).
        if (allAppearedCount_ != 2) fail_("factoryAll remoteAppeared count mismatch (expected 2)");
        if (nsOkAppearedCount_ != 1) fail_("factoryNsOk remoteAppeared count mismatch (expected 1)");
        if (allDisappearedCount_ != 2) fail_("factoryAll remoteDisappeared count mismatch (expected 2)");
        if (nsOkDisappearedCount_ != 1) fail_("factoryNsOk remoteDisappeared count mismatch (expected 1)");
        if (factoryAll_ && factoryAll_->size() != 0) fail_("factoryAll still has live instances at end");
        if (factoryNsOk_ && factoryNsOk_->size() != 0) fail_("factoryNsOk still has live instances at end");

        std::cout
            << "[SUMMARY] factoryAll +=" << allAppearedCount_ << " -=" << allDisappearedCount_
            << " size=" << (factoryAll_ ? factoryAll_->size() : 0) << "\n";
        std::cout
            << "[SUMMARY] factoryNsOk +=" << nsOkAppearedCount_ << " -=" << nsOkDisappearedCount_
            << " size=" << (factoryNsOk_ ? factoryNsOk_->size() : 0) << "\n";

        kill_(pGood_);
        kill_(pNsMismatch_);
        kill_(pBadMissing_);
        kill_(pBadType_);

        std::cout << (ok_ ? "[OK] RpcFactoryDemo orchestrate PASS\n" : "[FAIL] RpcFactoryDemo orchestrate FAIL\n");
        if (app_) app_->exit(ok_ ? 0 : 2);
    }

    // ---- factory callbacks

    void onAllAppeared_(SwPointer<Instance> inst) {
        ++allAppearedCount_;
        logFactory_("[factoryAll] +", inst);
        if (!inst) return;

        const SwString obj = inst->objectFqn();
        if (obj == goodFqn_) {
            seenGoodAll_ = true;
            lastGood_ = inst;
        } else if (obj == nsMismatchFqn_) {
            seenNsAll_ = true;
        } else if (obj == badMissingFqn_ || obj == badTypeFqn_) {
            fail_(std::string("factoryAll should NOT create instance for ") + obj.toStdString());
        }

        if (seenGoodAll_ && seenGoodNsOk_ && !startedNsMismatch_) {
            startedNsMismatch_ = true;
            SwTimer::singleShot(200, [this]() { step1_spawnNsMismatch_(); });
        }

        if (seenNsAll_ && !startedBad_) {
            startedBad_ = true;
            SwTimer::singleShot(200, [this]() { step2_spawnBadOnes_(); });
        }
    }

    void onNsOkAppeared_(SwPointer<Instance> inst) {
        ++nsOkAppearedCount_;
        logFactory_("[factoryNsOk] +", inst);
        if (!inst) return;

        const SwString obj = inst->objectFqn();
        if (obj == goodFqn_) {
            seenGoodNsOk_ = true;
            lastGood_ = inst;
        } else if (obj == nsMismatchFqn_) {
            fail_("factoryNsOk should NOT see ns mismatch");
        } else if (obj == badMissingFqn_ || obj == badTypeFqn_) {
            fail_(std::string("factoryNsOk should NOT create instance for ") + obj.toStdString());
        }

        if (seenGoodAll_ && seenGoodNsOk_ && !startedNsMismatch_) {
            startedNsMismatch_ = true;
            SwTimer::singleShot(200, [this]() { step1_spawnNsMismatch_(); });
        }
    }

    void onAllDisappeared_(SwPointer<Instance> inst) {
        ++allDisappearedCount_;
        logFactory_("[factoryAll] -", inst);
        if (!inst) return;

        const SwString obj = inst->objectFqn();
        if (obj == goodFqn_) {
            goneGoodAll_ = true;
        } else if (obj == nsMismatchFqn_) {
            goneNsAll_ = true;
        }

        if (goneGoodAll_ && goneGoodNsOk_ && !killedNsMismatch_) {
            killedNsMismatch_ = true;
            SwTimer::singleShot(200, [this]() { step4_killNsMismatch_(); });
        }

        if (goneNsAll_ && !finished_) {
            finished_ = true;
            SwTimer::singleShot(200, [this]() {
                step5_killBadOnes_();
                SwTimer::singleShot(200, [this]() { finish_(); });
            });
        }
    }

    void onNsOkDisappeared_(SwPointer<Instance> inst) {
        ++nsOkDisappearedCount_;
        logFactory_("[factoryNsOk] -", inst);
        if (!inst) return;

        const SwString obj = inst->objectFqn();
        if (obj == goodFqn_) {
            goneGoodNsOk_ = true;

            SwPointer<Instance> saved = lastGood_;
            SwTimer::singleShot(0, [this, saved]() mutable {
                if (done_) return;
                if (saved) {
                    fail_("SwPointer should become null after instance destruction");
                } else {
                    std::cout << "[CHECK] SwPointer nullified OK\n";
                }
            });
        } else if (obj == nsMismatchFqn_) {
            fail_("factoryNsOk should NOT emit disappear for ns mismatch");
        }

        if (goneGoodAll_ && goneGoodNsOk_ && !killedNsMismatch_) {
            killedNsMismatch_ = true;
            SwTimer::singleShot(200, [this]() { step4_killNsMismatch_(); });
        }
    }

    SwCoreApplication* app_{nullptr};
    SwString exePath_;
    SwString sys_;
    uint64_t startMs_{0};

    SwString goodFqn_;
    SwString nsMismatchFqn_;
    SwString badMissingFqn_;
    SwString badTypeFqn_;

    SwProxyObjectBrowser<DemoRemote>* factoryAll_{nullptr};
    SwProxyObjectBrowser<DemoRemote>* factoryNsOk_{nullptr};

    SwProcess* pGood_{nullptr};
    SwProcess* pNsMismatch_{nullptr};
    SwProcess* pBadMissing_{nullptr};
    SwProcess* pBadType_{nullptr};

    SwPointer<Instance> lastGood_{};

    bool ok_{true};
    bool done_{false};
    bool finished_{false};

    bool startedNsMismatch_{false};
    bool startedBad_{false};
    bool killedNsMismatch_{false};

    bool seenGoodAll_{false};
    bool seenGoodNsOk_{false};
    bool seenNsAll_{false};

    bool goneGoodAll_{false};
    bool goneGoodNsOk_{false};
    bool goneNsAll_{false};

    int allAppearedCount_{0};
    int nsOkAppearedCount_{0};
    int allDisappearedCount_{0};
    int nsOkDisappearedCount_{0};
};

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    if (argc < 2) {
        usage_();
        return 1;
    }

    const SwString arg1(argv[1]);

    if (arg1 == SwString("--server")) {
        if (argc < 6) {
            usage_();
            return 1;
        }
        ServerKind kind{};
        if (!parseServerKind_(SwString(argv[2]), kind)) {
            usage_();
            return 1;
        }
        const SwString sys(argv[3]);
        const SwString ns(argv[4]);
        const SwString obj(argv[5]);

        DemoService srv(sys, ns, obj, kind, nullptr);
        std::cout
            << "[server] kind=" << SwString(argv[2]).toStdString()
            << " sys=" << sys.toStdString()
            << " object=" << (ns + "/" + obj).toStdString()
            << " pid=" << sw::ipc::detail::currentPid()
            << "\n";
        return app.exec();
    }

    if (arg1 == SwString("--monitor")) {
        if (argc < 4) {
            usage_();
            return 1;
        }

        const SwString sys(argv[2]);
        const SwString filter(argv[3]);
        SwProxyObjectBrowser<DemoRemote> factory(sys, filter, SwString("monitor"), nullptr);

        SwObject::connect(&factory, &SwProxyObjectBrowser<DemoRemote>::remoteAppeared, [](SwPointer<SwProxyObjectBrowser<DemoRemote>::Instance> inst) {
            if (!inst) return;
            std::cout << "[monitor] + " << inst->remote().target().toStdString() << "\n";
        });
        SwObject::connect(&factory, &SwProxyObjectBrowser<DemoRemote>::remoteDisappeared, [](SwPointer<SwProxyObjectBrowser<DemoRemote>::Instance> inst) {
            if (!inst) return;
            std::cout << "[monitor] - " << inst->remote().target().toStdString() << "\n";
        });

        std::cout << "[monitor] sys=" << sys.toStdString() << " filter=" << filter.toStdString() << "\n";
        return app.exec();
    }

    if (arg1 == SwString("--orchestrate")) {
        const SwString sys = (argc >= 3)
                                 ? SwString(argv[2])
                                 : (SwString("demo_factory_test_") + SwString(std::to_string(sw::ipc::detail::currentPid())));
        const SwString exePath(argv[0]);

        std::cout << "[orchestrate] exe=" << exePath.toStdString() << "\n";
        std::cout << "[orchestrate] sys=" << sys.toStdString() << "\n";

        (void)new Orchestrator(&app, exePath, sys);
        return app.exec();
    }

    usage_();
    return 1;
}
