#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwProxyObject.h"
#include "SwProxyObjectBrowser.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

SW_PROXY_OBJECT_CLASS_BEGIN(DemoRemote)
    SW_PROXY_OBJECT_RPC(int, add, int, int)
    SW_PROXY_OBJECT_RPC(int, sub, int, int)
    SW_PROXY_OBJECT_RPC(int, mul, int, int)
    SW_PROXY_OBJECT_RPC(SwString, who)
    SW_PROXY_OBJECT_RPC(SwString, whoLambda)
    SW_PROXY_OBJECT_RPC(SwString, hello, SwString)
    SW_PROXY_OBJECT_VOID(notify, SwString)
SW_PROXY_OBJECT_CLASS_END()

static void swRemoteFactoryCompileSmokeTest_() {
    if (false) {
        SwProxyObjectBrowser<DemoRemote> factory(SwString("demo"), SwString("*"), SwString("smoke"), nullptr);
        (void)factory;
    }
}

static uint64_t nowUs() {
    const auto tp = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(tp).count());
}

static uint64_t percentileUs(std::vector<uint64_t> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double x = (p / 100.0) * static_cast<double>(v.size() - 1);
    const size_t i0 = static_cast<size_t>(x);
    const size_t i1 = (i0 + 1 < v.size()) ? (i0 + 1) : i0;
    const double t = x - static_cast<double>(i0);
    const double y = (1.0 - t) * static_cast<double>(v[i0]) + t * static_cast<double>(v[i1]);
    return static_cast<uint64_t>(y);
}

static void printStats(const std::string& label, const std::vector<uint64_t>& rttUs, uint64_t totalUs, int errors) {
    if (rttUs.empty()) {
        std::cout << "[perf] " << label << " no samples\n";
        return;
    }
    const uint64_t mn = *std::min_element(rttUs.begin(), rttUs.end());
    const uint64_t mx = *std::max_element(rttUs.begin(), rttUs.end());
    const long double sum = std::accumulate(rttUs.begin(), rttUs.end(), static_cast<long double>(0.0));
    const uint64_t mean = static_cast<uint64_t>(sum / static_cast<long double>(rttUs.size()));
    const uint64_t p50 = percentileUs(rttUs, 50.0);
    const uint64_t p95 = percentileUs(rttUs, 95.0);
    const uint64_t p99 = percentileUs(rttUs, 99.0);
    const double secs = totalUs ? (static_cast<double>(totalUs) / 1e6) : 0.0;
    const double rate = secs > 0.0 ? (static_cast<double>(rttUs.size()) / secs) : 0.0;

    std::cout
        << "[perf] " << label
        << " samples=" << rttUs.size()
        << " errors=" << errors
        << " duration_s=" << secs
        << " rate_hz=" << rate
        << "\n"
        << "[perf] " << label
        << " rtt_us min=" << mn
        << " mean=" << mean
        << " p50=" << p50
        << " p95=" << p95
        << " p99=" << p99
        << " max=" << mx
        << "\n";
}

static void usage() {
    std::cout
        << "Usage:\n"
        << "  RpcClientDemo.exe <sys>/<namespace>/<objectName> <a> <b> [clientInfo] [count]\n"
        << "  RpcClientDemo.exe <sys>/<namespace> <a> <b> [clientInfo] [count]            (auto-resolve if unique)\n"
        << "  RpcClientDemo.exe <sys> <a> <b> [clientInfo] [count]                        (auto-discover)\n"
        << "  RpcClientDemo.exe --discover <sys>                                           (list targets)\n"
        << "\n"
        << "Calls the RPC methods exposed by the target SwRemoteObject:\n"
        << "  - add(a,b) -> int\n"
        << "  - sub(a,b) -> int\n"
        << "  - mul(a,b) -> int\n"
        << "  - who() -> SwString\n"
        << "  - whoLambda() -> SwString\n"
        << "  - hello(name) -> SwString\n"
        << "  - notify(msg) -> void\n"
        << "\n"
        << "Notes:\n"
        << "  - Runs multiple loops to observe stable timings.\n"
        << "  - Default count is 50.\n";
}

static bool splitTarget(const SwString& target, SwString& outDomain, SwString& outObject) {
    const std::string s = target.toStdString();
    const size_t slash = s.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= s.size()) return false;
    outDomain = SwString(s.substr(0, slash));
    outObject = SwString(s.substr(slash + 1));
    return true;
}

static void splitObjectFqn(const SwString& objectFqn, SwString& outNameSpace, SwString& outObjectName)
{
    SwString s = objectFqn;
    s.replace('\\', '/');

    const std::string path = s.toStdString();
    const size_t slash = path.rfind('/');
    if (slash == std::string::npos) {
        outNameSpace.clear();
        outObjectName = s;
        return;
    }

    outNameSpace = SwString(path.substr(0, slash));
    outObjectName = SwString(path.substr(slash + 1));
}


static std::string joinList(const SwStringList& list, const char* sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) oss << (sep ? sep : "");
        oss << list[i].toStdString();
    }
    return oss.str();
}

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    if (argc < 2) {
        usage();
        return 1;
    }

    const SwString arg1(argv[1]);
    if (arg1 == "--discover" || arg1 == "-d") {
        if (argc < 3) {
            usage();
            return 1;
        }
        const SwString domain(argv[2]);
        const SwStringList list = DemoRemote::candidates(domain);
        std::cout << "[RPC] candidates for domain=" << domain.toStdString() << " (" << list.size() << ")\n";
        for (size_t i = 0; i < list.size(); ++i) {
            SwString d, obj;
            if (!splitTarget(list[i], d, obj)) {
                std::cout << "  - " << list[i].toStdString() << " (invalid target)\n";
                continue;
            }

            DemoRemote remote(d, obj, SwString("RpcClientDemo"));

            SwString nameSpace, objectName;
            splitObjectFqn(obj, nameSpace, objectName);

            const uint32_t pid = remote.remotePid();
            const bool alive = remote.isAlive();

            std::cout
                << "  - " << remote.target().toStdString()
                << " pid=" << pid
                << " alive=" << (alive ? "true" : "false");
            if (!nameSpace.isEmpty()) std::cout << " nameSpace=" << nameSpace.toStdString();
            if (!objectName.isEmpty()) std::cout << " objectName=" << objectName.toStdString();
            std::cout << "\n";

            const SwStringList rf = remote.functions();
            std::cout << "    remote.functions(" << rf.size() << "): " << joinList(rf, ", ") << "\n";

            for (size_t k = 0; k < rf.size(); ++k) {
                const SwString fn = rf[k];
                const SwStringList at = remote.argType(fn);
                std::cout << "      - " << fn.toStdString() << "(" << joinList(at, ", ") << ")\n";
            }

            const bool match = remote.matchesInterface();
            std::cout << "    matchesInterface=" << (match ? "true" : "false") << "\n";

            const SwStringList missing = remote.missingFunctions();
            const SwStringList extra = remote.extraFunctions();
            if (!missing.isEmpty()) {
                std::cout << "    missingFunctions(" << missing.size() << "): " << joinList(missing, ", ") << "\n";
            }
            if (!extra.isEmpty()) {
                std::cout << "    extraFunctions(" << extra.size() << "): " << joinList(extra, ", ") << "\n";
            }
        }
        return 0;
    }

    if (argc < 4) {
        usage();
        return 1;
    }

    SwString domain;
    SwString object;
    if (!splitTarget(arg1, domain, object)) {
        // Backward-compatible: allow passing only <app> and auto-discover a single matching <app>/<device>.
        domain = arg1;
        const SwStringList candidates = DemoRemote::candidates(domain);
        if (candidates.isEmpty()) {
            std::cerr << "[RPC] no targets found for domain=" << domain.toStdString() << "\n";
            return 2;
        }
        if (candidates.size() != 1) {
            std::cout << "[RPC] multiple targets found for domain=" << domain.toStdString() << " (" << candidates.size() << ")\n";
            for (size_t i = 0; i < candidates.size(); ++i) {
                std::cout << "  - " << candidates[i].toStdString() << "\n";
            }
            std::cout << "[RPC] re-run with <app>/<device> or use --discover <app>\n";
            return 0;
        }
        if (!splitTarget(candidates[0], domain, object)) {
            std::cerr << "[RPC] discovery returned invalid target: " << candidates[0].toStdString() << "\n";
            return 3;
        }
    } else {
        // Backward-compatible: if the user passed only "<sys>/<namespace>" but the server exposes
        // "<sys>/<namespace>/<objectName>", auto-resolve when the match is unique.
        const std::string objS = object.toStdString();
        if (!objS.empty() && objS.find('/') == std::string::npos) {
            const SwStringList candidates = DemoRemote::candidates(domain);
            const SwString full = domain + "/" + object;

            bool exact = false;
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (candidates[i] == full) {
                    exact = true;
                    break;
                }
            }

            if (!exact) {
                const SwString prefix = full + "/";
                SwStringList matches;
                for (size_t i = 0; i < candidates.size(); ++i) {
                    if (candidates[i].startsWith(prefix)) {
                        matches.append(candidates[i]);
                    }
                }
                if (matches.size() == 1) {
                    splitTarget(matches[0], domain, object);
                } else if (matches.size() > 1) {
                    std::cout << "[RPC] multiple targets match " << full.toStdString() << " (" << matches.size() << ")\n";
                    for (size_t i = 0; i < matches.size(); ++i) {
                        std::cout << "  - " << matches[i].toStdString() << "\n";
                    }
                    std::cout << "[RPC] re-run with full <sys>/<namespace>/<objectName> or use --discover <sys>\n";
                    return 0;
                }
            }
        }
    }

    const int a = SwString(argv[2]).toInt();
    const int b = SwString(argv[3]).toInt();
    const SwString clientInfo = (argc >= 5) ? SwString(argv[4]) : SwString("RpcClientDemo");
    const int count = (argc >= 6) ? (std::max)(1, SwString(argv[5]).toInt()) : 50;

    app.postEvent([&app, domain, object, a, b, clientInfo, count]() mutable {
        DemoRemote remote(domain, object, clientInfo);

        const std::string target = (domain + "/" + object).toStdString();

        std::cout << "[RPC] target=" << target << " count=" << count << "\n";

        int sum = 0;
        int addErrors = 0;
        std::vector<uint64_t> addRtts;
        addRtts.reserve(static_cast<size_t>(count));
        const uint64_t addBegin = nowUs();
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            sum = remote.add(a, b);
            const uint64_t t1 = nowUs();
            addRtts.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.addLastError().isEmpty()) {
                ++addErrors;
            }
        }
        const uint64_t addEnd = nowUs();

        if (!remote.addLastError().isEmpty()) {
            std::cerr << "[RPC] add last error: " << remote.addLastError().toStdString() << "\n";
        }
        std::cout << "[RPC] add(" << a << "," << b << ")=" << sum << "\n";
        printStats(std::string("rpc ") + target + "#add", addRtts,
                   (addEnd >= addBegin) ? (addEnd - addBegin) : 0, addErrors);

        int diff = 0;
        int subErrors = 0;
        std::vector<uint64_t> subRtts;
        subRtts.reserve(static_cast<size_t>(count));
        const uint64_t subBegin = nowUs();
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            diff = remote.sub(a, b);
            const uint64_t t1 = nowUs();
            subRtts.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.subLastError().isEmpty()) {
                ++subErrors;
            }
        }
        const uint64_t subEnd = nowUs();

        if (!remote.subLastError().isEmpty()) {
            std::cerr << "[RPC] sub last error: " << remote.subLastError().toStdString() << "\n";
        }
        std::cout << "[RPC] sub(" << a << "," << b << ")=" << diff << "\n";
        printStats(std::string("rpc ") + target + "#sub", subRtts,
                   (subEnd >= subBegin) ? (subEnd - subBegin) : 0, subErrors);

        int prod = 0;
        int mulErrors = 0;
        std::vector<uint64_t> mulRtts;
        mulRtts.reserve(static_cast<size_t>(count));
        const uint64_t mulBegin = nowUs();
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            prod = remote.mul(a, b);
            const uint64_t t1 = nowUs();
            mulRtts.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.mulLastError().isEmpty()) {
                ++mulErrors;
            }
        }
        const uint64_t mulEnd = nowUs();

        if (!remote.mulLastError().isEmpty()) {
            std::cerr << "[RPC] mul last error: " << remote.mulLastError().toStdString() << "\n";
        }
        std::cout << "[RPC] mul(" << a << "," << b << ")=" << prod << "\n";
        printStats(std::string("rpc ") + target + "#mul", mulRtts,
                   (mulEnd >= mulBegin) ? (mulEnd - mulBegin) : 0, mulErrors);

        SwString me;
        int whoErrors = 0;
        std::vector<uint64_t> whoRtts;
        whoRtts.reserve(static_cast<size_t>(count));
        const uint64_t whoBegin = nowUs();
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            me = remote.who();
            const uint64_t t1 = nowUs();
            whoRtts.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.whoLastError().isEmpty()) {
                ++whoErrors;
            }
        }
        const uint64_t whoEnd = nowUs();

        if (!remote.whoLastError().isEmpty()) {
            std::cerr << "[RPC] who last error: " << remote.whoLastError().toStdString() << "\n";
        }
        std::cout << "[RPC] who()=" << me.toStdString() << "\n";
        printStats(std::string("rpc ") + target + "#who", whoRtts,
                   (whoEnd >= whoBegin) ? (whoEnd - whoBegin) : 0, whoErrors);

        SwString me2;
        int who2Errors = 0;
        std::vector<uint64_t> who2Rtts;
        who2Rtts.reserve(static_cast<size_t>(count));
        const uint64_t who2Begin = nowUs();
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            me2 = remote.whoLambda();
            const uint64_t t1 = nowUs();
            who2Rtts.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.whoLambdaLastError().isEmpty()) {
                ++who2Errors;
            }
        }
        const uint64_t who2End = nowUs();

        if (!remote.whoLambdaLastError().isEmpty()) {
            std::cerr << "[RPC] whoLambda last error: " << remote.whoLambdaLastError().toStdString() << "\n";
        }
        std::cout << "[RPC] whoLambda()=" << me2.toStdString() << "\n";
        printStats(std::string("rpc ") + target + "#whoLambda", who2Rtts,
                   (who2End >= who2Begin) ? (who2End - who2Begin) : 0, who2Errors);

        SwString helloReply;
        int helloErrors = 0;
        std::vector<uint64_t> helloRtts;
        helloRtts.reserve(static_cast<size_t>(count));
        const uint64_t helloBegin = nowUs();
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            helloReply = remote.hello(clientInfo);
            const uint64_t t1 = nowUs();
            helloRtts.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.helloLastError().isEmpty()) {
                ++helloErrors;
            }
        }
        const uint64_t helloEnd = nowUs();

        if (!remote.helloLastError().isEmpty()) {
            std::cerr << "[RPC] hello last error: " << remote.helloLastError().toStdString() << "\n";
        }
        std::cout << "[RPC] hello(\"" << clientInfo.toStdString() << "\")=" << helloReply.toStdString() << "\n";
        printStats(std::string("rpc ") + target + "#hello", helloRtts,
                   (helloEnd >= helloBegin) ? (helloEnd - helloBegin) : 0, helloErrors);

        bool notifyOk = true;
        int notifyErrors = 0;
        std::vector<uint64_t> notifyRtts;
        notifyRtts.reserve(static_cast<size_t>(count));
        const uint64_t notifyBegin = nowUs();
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            notifyOk = remote.notify(SwString("notify: ") + clientInfo);
            const uint64_t t1 = nowUs();
            notifyRtts.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.notifyLastError().isEmpty()) {
                ++notifyErrors;
            }
        }
        const uint64_t notifyEnd = nowUs();

        if (!remote.notifyLastError().isEmpty()) {
            std::cerr << "[RPC] notify last error: " << remote.notifyLastError().toStdString() << "\n";
        }
        std::cout << "[RPC] notify() ok=" << (notifyOk ? "true" : "false") << "\n";
        printStats(std::string("rpc ") + target + "#notify", notifyRtts,
                   (notifyEnd >= notifyBegin) ? (notifyEnd - notifyBegin) : 0, notifyErrors);

        app.quit();
    });

    return app.exec();
}
