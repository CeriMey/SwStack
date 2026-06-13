#pragma once

#include "SwMailCommon.h"
#include "SwMailDnsClient.h"
#include "SwList.h"
#include "SwString.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// RFC 7208 — évaluateur SPF du courrier entrant. Pur (aucun état), DNS injectable
// pour les tests. Mécanismes v1 : all, ip4, ip6, a, mx, include, exists, redirect=.
// SKIPPÉ et documenté : ptr (déprécié → no-match), exp= (ignoré), expansion de
// macros (% → terme ignoré). Limites RFC §4.6.4 appliquées : 10 lookups DNS, 2 void.
class SwMailSpf {
public:
    enum class Result {
        None = 0, Neutral, Pass, Fail, SoftFail, TempError, PermError
    };

    struct Evaluation {
        Result result = Result::None;
        SwString checkedDomain;     // domaine dont la policy a autorisé l'IP
        SwString mailFromDomain;    // domaine RFC5321.MailFrom (vide si fallback HELO) — alignement DMARC
        SwString explanation;
        int dnsLookups = 0;
    };

    // Sources DNS injectables (chaque fn renvoie la liste de chaînes pour le host).
    struct DnsFns {
        std::function<SwList<SwString>(const SwString&)> txt;
        std::function<SwList<SwString>(const SwString&)> a;
        std::function<SwList<SwString>(const SwString&)> aaaa;
        std::function<SwList<SwString>(const SwString&)> mx;
    };

    static Evaluation evaluate(const SwString& clientIp,
                               const SwString& mailFromDomain,
                               const SwString& heloDomain,
                               SwMailDnsClient& dns) {
        DnsFns fns;
        fns.txt = [&dns](const SwString& h) {
            SwList<SwString> out;
            const SwList<SwMailDnsTxtRecord> recs = dns.resolveTxt(h);
            for (std::size_t i = 0; i < recs.size(); ++i) {
                out.append(recs[i].value);
            }
            return out;
        };
        fns.a = [&dns](const SwString& h) { return dns.resolveA(h); };
        fns.aaaa = [&dns](const SwString& h) { return dns.resolveAaaa(h); };
        fns.mx = [&dns](const SwString& h) {
            SwList<SwString> out;
            const SwList<SwMailMxRecord> recs = dns.resolveMx(h);
            for (std::size_t i = 0; i < recs.size(); ++i) {
                out.append(recs[i].exchange);
            }
            return out;
        };
        return evaluateWith(clientIp, mailFromDomain, heloDomain, fns);
    }

    static Evaluation evaluateWith(const SwString& clientIp,
                                   const SwString& mailFromDomain,
                                   const SwString& heloDomain,
                                   const DnsFns& dns) {
        Evaluation eval;
        ClientIp_ ip;
        if (!parseClientIp_(clientIp, ip)) {
            eval.result = Result::None;
            eval.explanation = "unparseable client ip";
            return eval;
        }

        const SwString mailDomain = swMailDetail::normalizeDomain(mailFromDomain);
        const SwString heloNorm = swMailDetail::normalizeDomain(heloDomain);
        const SwString checkDomain = !mailDomain.isEmpty() ? mailDomain : heloNorm;
        if (checkDomain.isEmpty()) {
            eval.result = Result::None;
            return eval;
        }
        eval.checkedDomain = checkDomain;
        eval.mailFromDomain = mailDomain;  // vide si on est tombé en fallback HELO

        Budget_ budget;
        eval.result = checkHost_(checkDomain, ip, dns, budget, 0);
        eval.dnsLookups = budget.dnsLookups;
        return eval;
    }

    static SwString resultToken(Result r) {
        switch (r) {
            case Result::Pass: return "pass";
            case Result::Fail: return "fail";
            case Result::SoftFail: return "softfail";
            case Result::Neutral: return "neutral";
            case Result::None: return "none";
            case Result::TempError: return "temperror";
            case Result::PermError: return "permerror";
        }
        return "none";
    }

    static bool selfTest_();

private:
    struct ClientIp_ {
        int family = 0;  // 4, 6, ou 0
        uint8_t v4[4] = {0, 0, 0, 0};
        uint8_t v6[16] = {0};
    };

    struct Budget_ {
        int dnsLookups = 0;
        int voidLookups = 0;
    };

    static std::string lower_(const std::string& in) {
        std::string out = in;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
        }
        return out;
    }

    static int hexVal_(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    static bool parseIpv4Bytes_(const std::string& in, uint8_t out[4]) {
        int part = 0;
        int value = -1;
        std::size_t digits = 0;
        for (std::size_t i = 0; i <= in.size(); ++i) {
            if (i < in.size() && in[i] >= '0' && in[i] <= '9') {
                value = (value < 0 ? 0 : value) * 10 + (in[i] - '0');
                if (++digits > 3 || value > 255) return false;
                continue;
            }
            if (i < in.size() && in[i] != '.') return false;
            if (digits == 0 || part >= 4) return false;
            out[part++] = static_cast<uint8_t>(value);
            value = -1;
            digits = 0;
        }
        return part == 4;
    }

    static bool parseIpv6Bytes_(const std::string& in, uint8_t out[16]) {
        if (in.find(':') == std::string::npos) {
            return false;
        }
        auto splitGroups = [](const std::string& part, std::vector<std::string>& dst) {
            if (part.empty()) return;
            std::size_t p = 0;
            while (true) {
                std::size_t c = part.find(':', p);
                if (c == std::string::npos) {
                    dst.push_back(part.substr(p));
                    break;
                }
                dst.push_back(part.substr(p, c - p));
                p = c + 1;
            }
        };
        std::vector<std::string> left, right;
        const std::size_t dc = in.find("::");
        bool hasDoubleColon = (dc != std::string::npos);
        if (!hasDoubleColon) {
            splitGroups(in, left);
        } else {
            splitGroups(in.substr(0, dc), left);
            splitGroups(in.substr(dc + 2), right);
        }
        auto groupsToBytes = [&](const std::vector<std::string>& groups, std::vector<uint8_t>& bytes) -> bool {
            for (std::size_t i = 0; i < groups.size(); ++i) {
                const std::string& g = groups[i];
                if (g.find('.') != std::string::npos) {
                    if (i != groups.size() - 1) return false;  // v4 embarqué seulement en dernier
                    uint8_t v4[4];
                    if (!parseIpv4Bytes_(g, v4)) return false;
                    bytes.push_back(v4[0]); bytes.push_back(v4[1]);
                    bytes.push_back(v4[2]); bytes.push_back(v4[3]);
                } else {
                    if (g.empty() || g.size() > 4) return false;
                    uint32_t val = 0;
                    for (std::size_t k = 0; k < g.size(); ++k) {
                        const int h = hexVal_(g[k]);
                        if (h < 0) return false;
                        val = val * 16 + static_cast<uint32_t>(h);
                    }
                    bytes.push_back(static_cast<uint8_t>((val >> 8) & 0xff));
                    bytes.push_back(static_cast<uint8_t>(val & 0xff));
                }
            }
            return true;
        };
        std::vector<uint8_t> lb, rb;
        if (!groupsToBytes(left, lb) || !groupsToBytes(right, rb)) {
            return false;
        }
        if (!hasDoubleColon) {
            if (lb.size() != 16) return false;
            std::memcpy(out, lb.data(), 16);
            return true;
        }
        if (lb.size() + rb.size() >= 16) return false;  // "::" doit élider >= 1 groupe
        std::memset(out, 0, 16);
        if (!lb.empty()) std::memcpy(out, lb.data(), lb.size());
        if (!rb.empty()) std::memcpy(out + 16 - rb.size(), rb.data(), rb.size());
        return true;
    }

    static bool parseClientIp_(const SwString& raw, ClientIp_& out) {
        std::string s = raw.trimmed().toStdString();
        if (!s.empty() && s.front() == '[' && s.back() == ']') {
            s = s.substr(1, s.size() - 2);
        }
        const std::size_t pct = s.find('%');
        if (pct != std::string::npos) {
            s = s.substr(0, pct);
        }
        if (parseIpv4Bytes_(s, out.v4)) {
            out.family = 4;
            return true;
        }
        uint8_t v6[16];
        if (parseIpv6Bytes_(s, v6)) {
            // IPv4-mapped (::ffff:a.b.c.d) → traité comme IPv4 pour matcher les ip4:.
            bool mapped = true;
            for (int i = 0; i < 10; ++i) {
                if (v6[i] != 0) { mapped = false; break; }
            }
            if (mapped && v6[10] == 0xff && v6[11] == 0xff) {
                out.family = 4;
                out.v4[0] = v6[12]; out.v4[1] = v6[13];
                out.v4[2] = v6[14]; out.v4[3] = v6[15];
                return true;
            }
            out.family = 6;
            std::memcpy(out.v6, v6, 16);
            return true;
        }
        // "1.2.3.4:port" éventuel
        const std::size_t colon = s.find(':');
        if (colon != std::string::npos && s.find(':', colon + 1) == std::string::npos) {
            if (parseIpv4Bytes_(s.substr(0, colon), out.v4)) {
                out.family = 4;
                return true;
            }
        }
        return false;
    }

    static bool matchPrefix_(const uint8_t* ip, const uint8_t* net, int byteLen, int prefix) {
        if (prefix < 0) prefix = byteLen * 8;
        if (prefix > byteLen * 8) return false;
        const int full = prefix / 8;
        const int rem = prefix % 8;
        for (int i = 0; i < full; ++i) {
            if (ip[i] != net[i]) return false;
        }
        if (rem) {
            const uint8_t mask = static_cast<uint8_t>(0xff << (8 - rem));
            if (((ip[full] ^ net[full]) & mask) != 0) return false;
        }
        return true;
    }

    static int parseCidrNumber_(const std::string& s, bool& ok) {
        ok = false;
        if (s.empty()) return -1;
        int v = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return -1;
            v = v * 10 + (s[i] - '0');
            if (v > 128) return -1;
        }
        ok = true;
        return v;
    }

    // Découpe un terme-mécanisme en name/value/cidrs. Renvoie false si CIDR invalide.
    static bool splitMechanism_(const std::string& tok, std::string& name, std::string& value,
                                int& v4cidr, int& v6cidr) {
        v4cidr = -1;
        v6cidr = -1;
        const std::size_t slash = tok.find('/');
        const std::string head = (slash == std::string::npos) ? tok : tok.substr(0, slash);
        const std::string cidrPart = (slash == std::string::npos) ? std::string() : tok.substr(slash);
        const std::size_t colon = head.find(':');
        name = lower_(colon == std::string::npos ? head : head.substr(0, colon));
        value = (colon == std::string::npos) ? std::string() : head.substr(colon + 1);
        if (cidrPart.empty()) {
            return true;
        }
        bool ok = false;
        if (cidrPart.size() >= 2 && cidrPart[0] == '/' && cidrPart[1] == '/') {
            v6cidr = parseCidrNumber_(cidrPart.substr(2), ok);
            return ok;
        }
        const std::size_t dbl = cidrPart.find("//");
        if (dbl == std::string::npos) {
            v4cidr = parseCidrNumber_(cidrPart.substr(1), ok);
            return ok;
        }
        v4cidr = parseCidrNumber_(cidrPart.substr(1, dbl - 1), ok);
        if (!ok) return false;
        v6cidr = parseCidrNumber_(cidrPart.substr(dbl + 2), ok);
        return ok;
    }

    static Result qualifierResult_(char q) {
        switch (q) {
            case '-': return Result::Fail;
            case '~': return Result::SoftFail;
            case '?': return Result::Neutral;
            default: return Result::Pass;  // '+'
        }
    }

    static bool ipMatchesList_(const ClientIp_& ip, const SwList<SwString>& addrs, int v4cidr, int v6cidr) {
        for (std::size_t i = 0; i < addrs.size(); ++i) {
            const std::string a = addrs[i].trimmed().toStdString();
            if (ip.family == 4) {
                uint8_t net[4];
                if (parseIpv4Bytes_(a, net) && matchPrefix_(ip.v4, net, 4, v4cidr)) {
                    return true;
                }
            } else {
                uint8_t net[16];
                if (parseIpv6Bytes_(a, net) && matchPrefix_(ip.v6, net, 16, v6cidr)) {
                    return true;
                }
            }
        }
        return false;
    }

    static Result checkHost_(const SwString& domain, const ClientIp_& ip, const DnsFns& dns,
                             Budget_& budget, int depth) {
        if (depth > 10) {
            return Result::PermError;
        }
        const SwList<SwString> txts = dns.txt(domain);
        std::string record;
        int spfCount = 0;
        for (std::size_t i = 0; i < txts.size(); ++i) {
            const std::string t = txts[i].toStdString();
            const std::string low = lower_(t);
            // "v=spf1" suivi d'un séparateur (espace OU tabulation) ou de rien.
            const bool isSpf = (low == "v=spf1") ||
                               (low.size() > 6 && low.compare(0, 6, "v=spf1") == 0 &&
                                (low[6] == ' ' || low[6] == '\t'));
            if (isSpf) {
                record = t;
                ++spfCount;
            }
        }
        if (spfCount == 0) return Result::None;
        if (spfCount > 1) return Result::PermError;

        // Tokenisation par espaces.
        std::vector<std::string> terms;
        {
            std::string cur;
            for (std::size_t i = 0; i < record.size(); ++i) {
                const char c = record[i];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                    if (!cur.empty()) { terms.push_back(cur); cur.clear(); }
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) terms.push_back(cur);
        }

        SwString redirectDomain;
        for (std::size_t ti = 1; ti < terms.size(); ++ti) {  // ti=0 → "v=spf1"
            std::string term = terms[ti];
            if (term.empty()) continue;

            // Modifieur (name=value) sans qualificateur et sans ':' avant '='.
            const std::size_t eq = term.find('=');
            const std::size_t firstColon = term.find(':');
            const char head0 = term[0];
            const bool hasQual = (head0 == '+' || head0 == '-' || head0 == '~' || head0 == '?');
            if (!hasQual && eq != std::string::npos &&
                (firstColon == std::string::npos || eq < firstColon)) {
                const std::string mname = lower_(term.substr(0, eq));
                const std::string mval = term.substr(eq + 1);
                if (mname == "redirect") {
                    redirectDomain = SwString(mval.c_str());
                }
                // exp= et modifieurs inconnus : ignorés (RFC : un modifieur inconnu n'est pas fatal).
                continue;
            }

            char qualifier = '+';
            if (hasQual) {
                qualifier = head0;
                term = term.substr(1);
            }
            if (term.empty()) continue;

            // Macros non supportées : plutôt que d'ignorer le terme (ce qui ferait
            // tomber un expéditeur légitime sur le -all final → faux Fail), on défère
            // (TempError, deliverability-safe : jamais de rejet définitif sur ce motif).
            if (term.find('%') != std::string::npos) {
                return Result::TempError;
            }

            std::string name, value;
            int v4cidr = -1, v6cidr = -1;
            if (!splitMechanism_(term, name, value, v4cidr, v6cidr)) {
                return Result::PermError;  // CIDR invalide
            }
            const SwString valueDomain = value.empty() ? domain : SwString(value.c_str());

            if (name == "all") {
                return qualifierResult_(qualifier);
            }
            if (name == "ip4") {
                if (v4cidr > 32) return Result::PermError;  // CIDR ip4 hors borne = syntaxe invalide
                if (ip.family != 4) continue;
                uint8_t net[4];
                if (parseIpv4Bytes_(value, net) && matchPrefix_(ip.v4, net, 4, v4cidr)) {
                    return qualifierResult_(qualifier);
                }
                continue;
            }
            if (name == "ip6") {
                if (ip.family != 6) continue;
                // ip6: ne porte qu'un seul /n (préfixe v6) ; splitMechanism_ le range
                // dans v4cidr faute de contexte, on le récupère ici.
                const int cidr6 = (v6cidr >= 0) ? v6cidr : v4cidr;
                uint8_t net[16];
                if (parseIpv6Bytes_(value, net) && matchPrefix_(ip.v6, net, 16, cidr6)) {
                    return qualifierResult_(qualifier);
                }
                continue;
            }
            if (name == "a") {
                if (v4cidr > 32) return Result::PermError;  // dual-cidr v4 hors borne
                if (++budget.dnsLookups > 10) return Result::PermError;
                const SwList<SwString> addrs = (ip.family == 4) ? dns.a(valueDomain) : dns.aaaa(valueDomain);
                if (addrs.isEmpty() && ++budget.voidLookups > 2) return Result::PermError;
                if (ipMatchesList_(ip, addrs, v4cidr, v6cidr)) {
                    return qualifierResult_(qualifier);
                }
                continue;
            }
            if (name == "mx") {
                if (v4cidr > 32) return Result::PermError;  // dual-cidr v4 hors borne
                if (++budget.dnsLookups > 10) return Result::PermError;
                const SwList<SwString> exchanges = dns.mx(valueDomain);
                if (exchanges.isEmpty() && ++budget.voidLookups > 2) return Result::PermError;
                bool matched = false;
                const std::size_t maxMx = exchanges.size() < 10 ? exchanges.size() : 10;
                for (std::size_t mi = 0; mi < maxMx && !matched; ++mi) {
                    const SwList<SwString> addrs =
                        (ip.family == 4) ? dns.a(exchanges[mi]) : dns.aaaa(exchanges[mi]);
                    if (ipMatchesList_(ip, addrs, v4cidr, v6cidr)) {
                        matched = true;
                    }
                }
                if (matched) return qualifierResult_(qualifier);
                continue;
            }
            if (name == "include") {
                if (++budget.dnsLookups > 10) return Result::PermError;
                const Result r = checkHost_(valueDomain, ip, dns, budget, depth + 1);
                if (r == Result::Pass) return qualifierResult_(qualifier);
                if (r == Result::TempError) return Result::TempError;
                if (r == Result::PermError || r == Result::None) return Result::PermError;
                continue;  // Fail / SoftFail / Neutral → pas de match
            }
            if (name == "exists") {
                if (++budget.dnsLookups > 10) return Result::PermError;
                const SwList<SwString> addrs = dns.a(valueDomain);  // exists teste toujours un A
                if (addrs.isEmpty() && ++budget.voidLookups > 2) return Result::PermError;
                if (!addrs.isEmpty()) return qualifierResult_(qualifier);
                continue;
            }
            if (name == "ptr") {
                continue;  // déprécié → ignoré
            }
            return Result::PermError;  // mécanisme inconnu
        }

        if (!redirectDomain.isEmpty()) {
            if (++budget.dnsLookups > 10) return Result::PermError;
            const Result r = checkHost_(redirectDomain, ip, dns, budget, depth + 1);
            if (r == Result::None) return Result::PermError;  // redirect vers domaine sans policy
            return r;
        }
        return Result::Neutral;  // aucun mécanisme n'a matché, "?all" implicite
    }
};

inline bool SwMailSpf::selfTest_() {
    auto single = [](const SwString& v) {
        SwList<SwString> out;
        out.append(v);
        return out;
    };
    bool ok = true;
    auto check = [&](const char* label, Result got, Result want) {
        if (got != want) {
            swCWarning(kSwLogCategory_SwMail) << "[SpfSelfTest] " << label << " got="
                                              << resultToken(got).toStdString()
                                              << " want=" << resultToken(want).toStdString();
            ok = false;
        }
    };

    // ip4 /24 pass
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1 ip4:192.0.2.0/24 -all"); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a;
        d.mx = [&](const SwString&) { return SwList<SwString>(); };
        check("ip4-pass", evaluateWith("192.0.2.55", "ex.com", "", d).result, Result::Pass);
        check("ip4-fail", evaluateWith("198.51.100.1", "ex.com", "", d).result, Result::Fail);
    }
    // ~all softfail
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1 ip4:10.0.0.0/8 ~all"); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("softfail", evaluateWith("192.0.2.5", "ex.com", "", d).result, Result::SoftFail);
    }
    // ip6 /32 pass
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1 ip6:2001:db8::/32 -all"); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("ip6-pass", evaluateWith("2001:db8:1234::1", "ex.com", "", d).result, Result::Pass);
        check("ip6-fail", evaluateWith("2001:dead::1", "ex.com", "", d).result, Result::Fail);
    }
    // a mechanism
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1 a -all"); };
        d.a = [&](const SwString&) { return single("203.0.113.7"); };
        d.aaaa = [&](const SwString&) { return SwList<SwString>(); };
        d.mx = [&](const SwString&) { return SwList<SwString>(); };
        check("a-pass", evaluateWith("203.0.113.7", "ex.com", "", d).result, Result::Pass);
    }
    // include recursion
    {
        DnsFns d;
        d.txt = [&](const SwString& h) {
            if (h == "ex.com") return single("v=spf1 include:_spf.ex.com -all");
            if (h == "_spf.ex.com") return single("v=spf1 ip4:203.0.113.0/24 -all");
            return SwList<SwString>();
        };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("include-pass", evaluateWith("203.0.113.9", "ex.com", "", d).result, Result::Pass);
    }
    // lookup limit (chain of includes > 10)
    {
        DnsFns d;
        d.txt = [&](const SwString& h) {
            const std::string s = h.toStdString();
            if (s == "ex.com") return single("v=spf1 include:c1 -all");
            if (s.size() == 2 && s[0] == 'c') {
                const int n = s[1] - '0';
                return single(SwString(("v=spf1 include:c" + std::to_string(n + 1) + " -all").c_str()));
            }
            return single("v=spf1 include:c1 -all");
        };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("lookup-limit", evaluateWith("203.0.113.9", "ex.com", "", d).result, Result::PermError);
    }
    // no record
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return SwList<SwString>(); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("none", evaluateWith("203.0.113.9", "ex.com", "", d).result, Result::None);
    }
    // two spf records → permerror
    {
        DnsFns d;
        d.txt = [&](const SwString&) {
            SwList<SwString> out;
            out.append("v=spf1 -all");
            out.append("v=spf1 +all");
            return out;
        };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("two-records", evaluateWith("203.0.113.9", "ex.com", "", d).result, Result::PermError);
    }
    // ?all neutral when nothing matches
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1 ip4:10.0.0.0/8 ?all"); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("neutral", evaluateWith("203.0.113.9", "ex.com", "", d).result, Result::Neutral);
    }
    // ipv4-mapped ipv6 client matches ip4
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1 ip4:203.0.113.0/24 -all"); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("v4mapped", evaluateWith("::ffff:203.0.113.9", "ex.com", "", d).result, Result::Pass);
    }
    // out-of-range ip4 cidr → PermError (pas un no-match silencieux)
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1 ip4:10.0.0.0/33 -all"); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("cidr-oob", evaluateWith("10.0.0.5", "ex.com", "", d).result, Result::PermError);
    }
    // mécanisme avec macro → TempError (jamais de faux Fail)
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1 a:%{d} -all"); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("macro-temperror", evaluateWith("203.0.113.9", "ex.com", "", d).result, Result::TempError);
    }
    // v=spf1 suivi d'une TABULATION reconnu
    {
        DnsFns d;
        d.txt = [&](const SwString&) { return single("v=spf1\tip4:1.2.3.0/24 -all"); };
        d.a = [&](const SwString&) { return SwList<SwString>(); };
        d.aaaa = d.a; d.mx = d.a;
        check("spf1-tab", evaluateWith("1.2.3.4", "ex.com", "", d).result, Result::Pass);
    }
    return ok;
}
