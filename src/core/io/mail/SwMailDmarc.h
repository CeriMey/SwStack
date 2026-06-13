#pragma once

#include "SwMailCommon.h"
#include "SwMailDnsClient.h"
#include "SwMailSpf.h"
#include "SwList.h"
#include "SwString.h"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

// RFC 7489 — DMARC. Consomme directement SwMailSpf::Result et la liste des
// domaines d= DKIM vérifiés. Limitation documentée : pas de Public Suffix List,
// le « domaine organisationnel » est approximé par les 2 derniers labels
// (faux positifs possibles sur les suffixes multi-labels type co.uk).
class SwMailDmarc {
public:
    enum class Policy { None = 0, Quarantine, Reject };
    enum class Alignment { Relaxed = 0, Strict };
    enum class Disposition { Pass = 0, Fail };
    enum class AlignedBy { None = 0, Dkim, Spf };
    enum class Action { Accept = 0, Flag, Reject };

    struct Record {
        bool present = false;
        bool valid = false;
        Policy p = Policy::None;
        Policy sp = Policy::None;
        bool hasSp = false;
        Alignment adkim = Alignment::Relaxed;
        Alignment aspf = Alignment::Relaxed;
        int pct = 100;
        SwString policyDomain;
        bool fromOrgApproximation = false;
    };

    struct Result {
        bool evaluated = false;
        SwString fromDomain;
        bool multipleFromRejected = false;
        Record record;
        Disposition disposition = Disposition::Fail;
        AlignedBy alignedBy = AlignedBy::None;
        Policy applicablePolicy = Policy::None;
        SwString authResultsFragment;  // "dmarc=pass header.from=..." (informatif)
        SwString diagnostic;
    };

    struct SpfInput {
        SwMailSpf::Result result = SwMailSpf::Result::None;
        SwString authenticatedDomain;  // domaine MAIL FROM authentifié par SPF
    };
    struct DkimInput {
        SwList<SwString> passingDomains;  // d= des signatures vérifiées (minuscule)
    };

    static Result evaluate(const SwByteArray& raw, const SpfInput& spf, const DkimInput& dkim,
                           SwMailDnsClient& dns) {
        return evaluateWith(raw, spf, dkim, [&dns](const SwString& host) -> SwString {
            const SwList<SwMailDnsTxtRecord> recs = dns.resolveTxt(host);
            for (std::size_t i = 0; i < recs.size(); ++i) {
                if (recs[i].value.toLower().contains("v=dmarc1")) {
                    return recs[i].value;
                }
            }
            return SwString();
        });
    }

    static Result evaluateWith(const SwByteArray& raw, const SpfInput& spf, const DkimInput& dkim,
                               const std::function<SwString(const SwString&)>& txtLookup) {
        Result res;
        bool multi = false;
        const SwString from = extractFromDomain_(raw, multi);
        res.fromDomain = from;
        res.multipleFromRejected = multi;
        res.evaluated = true;

        if (from.isEmpty()) {
            res.disposition = Disposition::Fail;
            res.diagnostic = "no usable From domain";
            res.authResultsFragment = "dmarc=fail header.from=unknown";
            return res;
        }

        res.record = discoverRecord_(from, txtLookup);

        bool dkimAligned = false;
        for (std::size_t i = 0; i < dkim.passingDomains.size(); ++i) {
            if (aligned_(dkim.passingDomains[i], from, res.record.adkim)) {
                dkimAligned = true;
                break;
            }
        }
        const bool spfAligned = (spf.result == SwMailSpf::Result::Pass) &&
                                !spf.authenticatedDomain.isEmpty() &&
                                aligned_(spf.authenticatedDomain, from, res.record.aspf);

        res.disposition = (dkimAligned || spfAligned) ? Disposition::Pass : Disposition::Fail;
        res.alignedBy = dkimAligned ? AlignedBy::Dkim : (spfAligned ? AlignedBy::Spf : AlignedBy::None);

        if (multi) {
            // Plusieurs identités From → on ne peut pas faire confiance à l'alignement.
            res.disposition = Disposition::Fail;
            res.alignedBy = AlignedBy::None;
            res.diagnostic = "multiple From identities";
        }

        res.applicablePolicy = res.record.present ? res.record.p : Policy::None;
        const SwString token = !res.record.present
                                   ? SwString("none")
                                   : (res.disposition == Disposition::Pass ? SwString("pass") : SwString("fail"));
        res.authResultsFragment = "dmarc=" + token + " header.from=" + from;
        if (res.record.fromOrgApproximation) {
            res.diagnostic += (res.diagnostic.isEmpty() ? SwString() : SwString("; ")) +
                              "policy via org-domain approximation (no PSL)";
        }
        return res;
    }

    static Action applyPolicy(const Result& r, SwMailConfig::InboundAuthMode mode, int& outSmtpRejectCode) {
        outSmtpRejectCode = 550;
        if (mode != SwMailConfig::InboundAuthMode::Enforce) return Action::Accept;
        if (!r.record.present) return Action::Accept;
        if (r.disposition == Disposition::Pass) return Action::Accept;

        // Sous-domaine (record trouvé via org-domain) → policy sp= si présente.
        Policy pol = r.record.p;
        if (r.record.fromOrgApproximation && r.record.hasSp) {
            pol = r.record.sp;
        }
        // Échantillonnage pct : une fraction (100-pct)% des échecs est rétrogradée
        // d'un cran (reject→quarantine→none), de façon déterministe.
        if (r.record.pct < 100) {
            if (pctGate_(r.fromDomain) >= r.record.pct) {
                if (pol == Policy::Reject) pol = Policy::Quarantine;
                else if (pol == Policy::Quarantine) pol = Policy::None;
            }
        }
        switch (pol) {
            case Policy::Reject: return Action::Reject;
            case Policy::Quarantine: return Action::Flag;
            default: return Action::Accept;
        }
    }

    static SwString organizationalDomainApprox_(const SwString& domain) {
        std::string d = swMailDetail::normalizeDomain(domain).toStdString();
        while (!d.empty() && d.back() == '.') d.pop_back();
        std::vector<std::string> labels;
        std::size_t p = 0;
        while (true) {
            const std::size_t dot = d.find('.', p);
            labels.push_back(dot == std::string::npos ? d.substr(p) : d.substr(p, dot - p));
            if (dot == std::string::npos) break;
            p = dot + 1;
        }
        if (labels.size() <= 2) return SwString(d.c_str());
        const std::string org = labels[labels.size() - 2] + "." + labels[labels.size() - 1];
        return SwString(org.c_str());
    }

    // Retire les commentaires RFC 5322 (...) hors guillemets (parenthèses imbriquées).
    static std::string stripComments_(const std::string& in) {
        std::string out;
        int depth = 0;
        bool inQuote = false;
        for (std::size_t i = 0; i < in.size(); ++i) {
            const char c = in[i];
            if (inQuote) {
                out.push_back(c);
                if (c == '"') inQuote = false;
                continue;
            }
            if (c == '"') { inQuote = true; out.push_back(c); continue; }
            if (c == '(') { ++depth; continue; }
            if (c == ')') { if (depth > 0) --depth; continue; }
            if (depth == 0) out.push_back(c);
        }
        return out;
    }

    // Extraction robuste du domaine du RFC5322.From. Gère les noms d'affichage avec
    // virgule ("Doe, John <a@b>") sans les confondre avec plusieurs adresses, et les
    // commentaires. Ne marque outMultipleFrom que pour un VRAI multi-identité.
    static SwString extractFromDomain_(const SwByteArray& raw, bool& outMultipleFrom) {
        outMultipleFrom = false;
        const SwList<SwString> froms = swMailDetail::headerOccurrences(raw, "from");
        if (froms.isEmpty()) return SwString();
        if (froms.size() > 1) outMultipleFrom = true;  // plusieurs en-têtes From:
        const std::string v = stripComments_(froms.first().toStdString());

        // Collecte des angle-addr <...> (hors guillemets).
        std::vector<std::string> angles;
        {
            bool inQuote = false;
            std::size_t start = std::string::npos;
            for (std::size_t i = 0; i < v.size(); ++i) {
                const char c = v[i];
                if (inQuote) { if (c == '"') inQuote = false; continue; }
                if (c == '"') { inQuote = true; continue; }
                if (c == '<') {
                    start = i + 1;
                } else if (c == '>' && start != std::string::npos) {
                    angles.push_back(v.substr(start, i - start));
                    start = std::string::npos;
                }
            }
        }

        std::string addr;
        if (angles.size() == 1) {
            addr = angles[0];
        } else if (angles.size() > 1) {
            outMultipleFrom = true;  // plusieurs mailbox dans le même From
            addr = angles[0];
        } else {
            // Pas d'angle-addr : addr-spec brut, potentiellement plusieurs séparés par
            // des virgules hors guillemets.
            std::vector<std::string> tokens;
            std::string cur;
            bool inQuote = false;
            for (std::size_t i = 0; i < v.size(); ++i) {
                const char c = v[i];
                if (inQuote) { cur.push_back(c); if (c == '"') inQuote = false; continue; }
                if (c == '"') { inQuote = true; cur.push_back(c); continue; }
                if (c == ',') { tokens.push_back(cur); cur.clear(); continue; }
                cur.push_back(c);
            }
            tokens.push_back(cur);
            std::vector<std::string> nonEmpty;
            for (std::size_t i = 0; i < tokens.size(); ++i) {
                if (!SwString(tokens[i].c_str()).trimmed().isEmpty()) {
                    nonEmpty.push_back(tokens[i]);
                }
            }
            if (nonEmpty.empty()) return SwString();
            if (nonEmpty.size() > 1) outMultipleFrom = true;
            addr = nonEmpty[0];
        }

        SwString local, domain;
        if (!swMailDetail::splitAddress(SwString(addr.c_str()), local, domain)) return SwString();
        return swMailDetail::normalizeDomain(domain);
    }

    static Record parseRecord_(const SwString& txt) {
        Record r;
        if (txt.trimmed().isEmpty()) return r;
        const std::map<std::string, std::string> tags = parseTags_(txt.toStdString());
        if (lower_(getTag_(tags, "v")) != "dmarc1") return r;
        if (getTag_(tags, "p").empty()) return r;  // p obligatoire
        r.present = true;
        r.valid = true;
        r.p = policyFromString_(getTag_(tags, "p"));
        const std::string sp = getTag_(tags, "sp");
        if (!sp.empty()) {
            r.hasSp = true;
            r.sp = policyFromString_(sp);
        }
        r.adkim = lower_(getTag_(tags, "adkim")) == "s" ? Alignment::Strict : Alignment::Relaxed;
        r.aspf = lower_(getTag_(tags, "aspf")) == "s" ? Alignment::Strict : Alignment::Relaxed;
        const std::string pct = getTag_(tags, "pct");
        if (!pct.empty()) {
            int v = static_cast<int>(std::strtol(pct.c_str(), nullptr, 10));
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            r.pct = v;
        }
        return r;
    }

    static bool selfTest_();

private:
    static std::string lower_(const std::string& s) {
        std::string out = s;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
        }
        return out;
    }

    static std::string trim_(const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    static std::map<std::string, std::string> parseTags_(const std::string& v) {
        std::map<std::string, std::string> out;
        std::size_t i = 0;
        while (true) {
            const std::size_t semi = v.find(';', i);
            const std::string seg = v.substr(i, semi == std::string::npos ? std::string::npos : semi - i);
            const std::size_t eq = seg.find('=');
            if (eq != std::string::npos) {
                const std::string k = lower_(trim_(seg.substr(0, eq)));
                const std::string val = trim_(seg.substr(eq + 1));
                if (!k.empty()) out[k] = val;
            }
            if (semi == std::string::npos) break;
            i = semi + 1;
        }
        return out;
    }

    static std::string getTag_(const std::map<std::string, std::string>& tags, const std::string& key) {
        const std::map<std::string, std::string>::const_iterator it = tags.find(key);
        return it == tags.end() ? std::string() : it->second;
    }

    static Policy policyFromString_(const std::string& s) {
        const std::string p = lower_(trim_(s));
        if (p == "reject") return Policy::Reject;
        if (p == "quarantine") return Policy::Quarantine;
        return Policy::None;
    }

    static bool aligned_(const SwString& candidate, const SwString& fromDomain, Alignment mode) {
        const SwString c = swMailDetail::normalizeDomain(candidate);
        const SwString f = swMailDetail::normalizeDomain(fromDomain);
        if (c.isEmpty() || f.isEmpty()) return false;
        if (mode == Alignment::Strict) return c == f;
        return organizationalDomainApprox_(c) == organizationalDomainApprox_(f);
    }

    static Record discoverRecord_(const SwString& fromDomain,
                                  const std::function<SwString(const SwString&)>& txtLookup) {
        Record exact = parseRecord_(txtLookup("_dmarc." + fromDomain));
        if (exact.present) {
            exact.policyDomain = fromDomain;
            exact.fromOrgApproximation = false;
            return exact;
        }
        const SwString org = organizationalDomainApprox_(fromDomain);
        if (org != fromDomain) {
            Record orgRec = parseRecord_(txtLookup("_dmarc." + org));
            if (orgRec.present) {
                orgRec.policyDomain = org;
                orgRec.fromOrgApproximation = true;
                return orgRec;
            }
        }
        return Record();
    }

    static int pctGate_(const SwString& seed) {
        unsigned int h = 2166136261u;
        const std::string s = seed.toStdString();
        for (std::size_t i = 0; i < s.size(); ++i) {
            h ^= static_cast<unsigned char>(s[i]);
            h *= 16777619u;
        }
        return static_cast<int>(h % 100u);
    }
};

inline bool SwMailDmarc::selfTest_() {
    bool ok = true;
    auto fail = [&](const char* m) {
        swCWarning(kSwLogCategory_SwMail) << "[DmarcSelfTest] " << m;
        ok = false;
    };
    auto msg = [](const char* fromHeader) {
        const std::string s = std::string("From: ") + fromHeader + "\r\nTo: b@x.net\r\nSubject: t\r\n\r\nbody\r\n";
        return SwByteArray(s);
    };

    // 1. DKIM aligné → Pass (accepté même en Enforce p=reject).
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=reject"); };
        SpfInput spf;
        DkimInput dkim; dkim.passingDomains.append("foo.com");
        Result r = evaluateWith(msg("a@foo.com"), spf, dkim, txt);
        if (r.disposition != Disposition::Pass) fail("dkim-aligned should pass");
        int code = 0;
        if (applyPolicy(r, SwMailConfig::InboundAuthMode::Enforce, code) != Action::Accept) fail("aligned pass must accept");
    }
    // 2. SPF aligné → Pass.
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=reject"); };
        SpfInput spf; spf.result = SwMailSpf::Result::Pass; spf.authenticatedDomain = "foo.com";
        DkimInput dkim;
        Result r = evaluateWith(msg("a@foo.com"), spf, dkim, txt);
        if (r.disposition != Disposition::Pass) fail("spf-aligned should pass");
    }
    // 3. Les deux échouent, p=reject, Enforce → Reject 550.
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=reject"); };
        SpfInput spf; spf.result = SwMailSpf::Result::Fail;
        DkimInput dkim;
        Result r = evaluateWith(msg("a@foo.com"), spf, dkim, txt);
        if (r.disposition != Disposition::Fail) fail("both-fail should fail");
        int code = 0;
        if (applyPolicy(r, SwMailConfig::InboundAuthMode::Enforce, code) != Action::Reject) fail("p=reject enforce must reject");
        if (code != 550) fail("reject code should be 550");
    }
    // 4. Échec, p=reject, mode Tag → Accept (jamais de rejet en Tag).
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=reject"); };
        SpfInput spf;
        DkimInput dkim;
        Result r = evaluateWith(msg("a@foo.com"), spf, dkim, txt);
        int code = 0;
        if (applyPolicy(r, SwMailConfig::InboundAuthMode::Tag, code) != Action::Accept) fail("tag mode must never reject");
    }
    // 5. p=none → Accept.
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=none"); };
        SpfInput spf;
        DkimInput dkim;
        Result r = evaluateWith(msg("a@foo.com"), spf, dkim, txt);
        int code = 0;
        if (applyPolicy(r, SwMailConfig::InboundAuthMode::Enforce, code) != Action::Accept) fail("p=none must accept");
    }
    // 6. Pas de record → token none, Accept.
    {
        auto txt = [](const SwString&) { return SwString(); };
        SpfInput spf;
        DkimInput dkim;
        Result r = evaluateWith(msg("a@foo.com"), spf, dkim, txt);
        if (r.record.present) fail("no record should be absent");
        if (!r.authResultsFragment.contains("dmarc=none")) fail("no record should yield dmarc=none");
        int code = 0;
        if (applyPolicy(r, SwMailConfig::InboundAuthMode::Enforce, code) != Action::Accept) fail("no record must accept");
    }
    // 7. Multiple From → Fail.
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=reject"); };
        SpfInput spf; spf.result = SwMailSpf::Result::Pass; spf.authenticatedDomain = "foo.com";
        DkimInput dkim; dkim.passingDomains.append("foo.com");
        Result r = evaluateWith(msg("a@foo.com, b@bar.com"), spf, dkim, txt);
        if (!r.multipleFromRejected || r.disposition != Disposition::Fail) fail("multiple From must fail");
    }
    // 8. Org-domain approx : From mail.foo.com, DKIM d=foo.com, relaxed → aligné.
    {
        auto txt = [](const SwString& host) {
            if (host == "_dmarc.mail.foo.com") return SwString();
            if (host == "_dmarc.foo.com") return SwString("v=DMARC1; p=reject");
            return SwString();
        };
        SpfInput spf;
        DkimInput dkim; dkim.passingDomains.append("foo.com");
        Result r = evaluateWith(msg("a@mail.foo.com"), spf, dkim, txt);
        if (r.disposition != Disposition::Pass) fail("org-approx relaxed dkim should align");
        if (!r.record.fromOrgApproximation) fail("should mark org approximation");
        if (!r.diagnostic.contains("no PSL")) fail("org approx must warn about no PSL");
    }
    // 10. Nom d'affichage avec virgule ("Doe, John <a@foo.com>") : PAS multiple-From.
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=reject"); };
        SpfInput spf;
        DkimInput dkim; dkim.passingDomains.append("foo.com");
        Result r = evaluateWith(msg("\"Doe, John\" <a@foo.com>"), spf, dkim, txt);
        if (r.multipleFromRejected) fail("display-name comma must not be multiple-From");
        if (r.disposition != Disposition::Pass) fail("display-name comma should still align/pass");
    }
    // 11. Commentaire RFC5322 dans le From ne casse pas l'extraction.
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=reject"); };
        SpfInput spf; spf.result = SwMailSpf::Result::Pass; spf.authenticatedDomain = "foo.com";
        DkimInput dkim;
        Result r = evaluateWith(msg("a@foo.com (John Doe)"), spf, dkim, txt);
        if (r.fromDomain != "foo.com") fail("comment in From must be stripped");
    }
    // 9. pct=0 reject → rétrogradé en Flag.
    {
        auto txt = [](const SwString&) { return SwString("v=DMARC1; p=reject; pct=0"); };
        SpfInput spf; spf.result = SwMailSpf::Result::Fail;
        DkimInput dkim;
        Result r = evaluateWith(msg("a@foo.com"), spf, dkim, txt);
        int code = 0;
        if (applyPolicy(r, SwMailConfig::InboundAuthMode::Enforce, code) != Action::Flag) fail("pct=0 reject should downgrade to flag");
    }
    return ok;
}
