#include "SwApiCli.h"

SwApiCli::SwApiCli(int argc, char** argv) { parse_(argc, argv); }

bool SwApiCli::startsWith_(const SwString& s, const SwString& prefix) { return s.startsWith(prefix); }

bool SwApiCli::isValueOption_(const SwString& key) {
    return key == "domain" || key == "sys" || key == "target" || key == "signal" || key == "path" || key == "kind" || key == "name" ||
           key == "value" || key == "type" || key == "ns" || key == "namespace" || key == "object" || key == "params" || key == "args" ||
           key == "config" || key == "file" || key == "clientInfo" || key == "timeout_ms" || key == "timeoutMs" || key == "duration_ms";
}

void SwApiCli::parse_(int argc, char** argv) {
    exe_.clear();
    positionals_.clear();
    options_.clear();
    flags_.clear();

    if (argc > 0 && argv && argv[0]) exe_ = SwString(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const SwString a = argv[i] ? SwString(argv[i]) : SwString();
        if (a.isEmpty()) continue;

        if (a == "--") {
            for (int k = i + 1; k < argc; ++k) {
                if (argv[k]) positionals_.append(SwString(argv[k]));
            }
            break;
        }

        if (startsWith_(a, "--")) {
            const int eq = a.indexOf("=");
            if (eq >= 0) {
                const SwString key = a.mid(2, eq - 2);
                const SwString val = a.mid(eq + 1);
                options_.insert(key, val);
                continue;
            }

            const SwString key = a.mid(2);
            if (isValueOption_(key) && i + 1 < argc) {
                const SwString next = argv[i + 1] ? SwString(argv[i + 1]) : SwString();
                if (!next.isEmpty() && !next.startsWith("-")) {
                    options_.insert(key, next);
                    ++i;
                    continue;
                }
            }

            flags_.insert(key, true);
            continue;
        }

        if (startsWith_(a, "-") && a.size() == 2) {
            const SwChar c = a.at(1);
            if (c == 'h') {
                flags_.insert("help", true);
                continue;
            }
            if (c == 'j') {
                flags_.insert("json", true);
                continue;
            }
            if (c == 'p') {
                flags_.insert("pretty", true);
                continue;
            }
            if (c == 'd') {
                if (i + 1 < argc) {
                    const SwString next = argv[i + 1] ? SwString(argv[i + 1]) : SwString();
                    options_.insert("domain", next);
                    ++i;
                    continue;
                }
            }
        }

        positionals_.append(a);
    }
}

bool SwApiCli::hasFlag(const SwString& name) const { return flags_.contains(name); }

SwString SwApiCli::value(const SwString& name, const SwString& defaultValue) const {
    if (!options_.contains(name)) return defaultValue;
    return options_.value(name, defaultValue);
}

int SwApiCli::intValue(const SwString& name, int defaultValue) const {
    const SwString v = value(name, SwString());
    if (v.isEmpty()) return defaultValue;
    bool ok = false;
    const int x = v.toInt(&ok);
    return ok ? x : defaultValue;
}
