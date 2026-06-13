#pragma once

#include "SwMailDkim.h"
#include "SwMailDmarc.h"
#include "SwMailSpf.h"

// Orchestrateur de l'authentification entrante : enchaîne SPF → DKIM → DMARC sur
// un unique SwMailDnsClient et assemble l'en-tête Authentication-Results (RFC 8601).
// Les évaluateurs SwMailSpf / SwMailDkimVerifier / SwMailDmarc restent purs.
struct SwInboundAuthResult {
    SwMailSpf::Result spf = SwMailSpf::Result::None;
    SwString spfMailfrom;        // smtp.mailfrom= (adresse complète)
    SwString spfAuthedDomain;    // domaine MAIL FROM authentifié (vide si fallback HELO)
    SwMailDkim::VerifyResult dkim = SwMailDkim::VerifyResult::None;
    SwString dkimDomain;         // header.d= de la première signature Pass
    SwList<SwString> dkimPassDomains;
    SwMailDmarc::Disposition dmarcDisposition = SwMailDmarc::Disposition::Fail;
    bool dmarcEvaluated = false;
    SwString headerFromDomain;   // header.from=
    bool dmarcReject = false;
    SwMailDmarc::Result dmarc;
};

class SwMailInboundAuth {
public:
    static SwMailSpf::Result evaluateSpf(SwMailDnsClient& dns, const SwString& clientIp,
                                         const SwString& mailFrom, const SwString& helo,
                                         SwString* outMailfrom, SwString* outAuthedDomain) {
        SwString local, mailFromDomain;
        swMailDetail::splitAddress(mailFrom, local, mailFromDomain);
        const SwMailSpf::Evaluation e = SwMailSpf::evaluate(clientIp, mailFromDomain, helo, dns);
        if (outMailfrom) *outMailfrom = swMailDetail::canonicalAddress(mailFrom);
        if (outAuthedDomain) *outAuthedDomain = e.mailFromDomain;  // vide si fallback HELO
        return e.result;
    }

    static SwMailDkim::VerifyResult verifyDkim(SwMailDnsClient& dns, const SwByteArray& message,
                                               SwString* outFirstPassDomain,
                                               SwList<SwString>* outPassDomains) {
        const SwList<SwDkimSignatureResult> results = SwMailDkimVerifier::verifyMessage(message, dns);
        SwList<SwString> passing;
        SwMailDkimVerifier::anyPass(results, &passing);
        if (outPassDomains) *outPassDomains = passing;
        if (outFirstPassDomain && !passing.isEmpty()) *outFirstPassDomain = passing.first();

        if (!passing.isEmpty()) return SwMailDkim::VerifyResult::Pass;
        bool anyTemp = false, anyFail = false, anyPerm = false;
        for (std::size_t i = 0; i < results.size(); ++i) {
            switch (results[i].result) {
                case SwMailDkim::VerifyResult::TempError: anyTemp = true; break;
                case SwMailDkim::VerifyResult::Fail: anyFail = true; break;
                case SwMailDkim::VerifyResult::PermError: anyPerm = true; break;
                default: break;
            }
        }
        if (anyTemp) return SwMailDkim::VerifyResult::TempError;
        if (anyFail) return SwMailDkim::VerifyResult::Fail;
        if (anyPerm) return SwMailDkim::VerifyResult::PermError;
        return SwMailDkim::VerifyResult::None;
    }

    static SwMailDmarc::Disposition evaluateDmarc(SwMailDnsClient& dns, const SwByteArray& message,
                                                  SwInboundAuthResult& io,
                                                  SwMailConfig::InboundAuthMode mode,
                                                  int* outRejectCode) {
        SwMailDmarc::SpfInput spf;
        spf.result = io.spf;
        spf.authenticatedDomain = io.spfAuthedDomain;
        SwMailDmarc::DkimInput dkim;
        dkim.passingDomains = io.dkimPassDomains;

        const SwMailDmarc::Result r = SwMailDmarc::evaluate(message, spf, dkim, dns);
        io.dmarc = r;
        io.dmarcEvaluated = r.evaluated;
        io.dmarcDisposition = r.disposition;
        io.headerFromDomain = r.fromDomain;

        int code = 550;
        const SwMailDmarc::Action act = SwMailDmarc::applyPolicy(r, mode, code);
        io.dmarcReject = (act == SwMailDmarc::Action::Reject);
        if (outRejectCode) *outRejectCode = code;
        return r.disposition;
    }

    // RFC 8601 — valeur de l'en-tête uniquement (sans nom de champ ni CRLF). Les
    // valeurs issues de l'enveloppe/des en-têtes (mailfrom, d=, from) ne sont émises
    // que si propres : un MAIL FROM:<a@b; dmarc=pass> ne doit pas pouvoir injecter une
    // pseudo-propriété dans NOTRE en-tête (corruption de grammaire AR).
    static SwString buildAuthenticationResults_(const SwString& authservId, const SwInboundAuthResult& r) {
        SwString out = (authservId.isEmpty() ? SwString("localhost") : authservId) +
                       "; spf=" + SwMailSpf::resultToken(r.spf);
        if (isArSafe_(r.spfMailfrom)) {
            out += " smtp.mailfrom=" + r.spfMailfrom;
        }
        out += "; dkim=" + SwMailDkim::resultToken(r.dkim);
        if (isArSafe_(r.dkimDomain)) {
            out += " header.d=" + r.dkimDomain;
        }
        out += "; dmarc=" + dmarcToken_(r);
        if (isArSafe_(r.headerFromDomain)) {
            out += " header.from=" + r.headerFromDomain;
        }
        return out;
    }

    static bool selfTest_() {
        bool ok = true;
        SwInboundAuthResult r;
        r.spf = SwMailSpf::Result::Pass;
        r.spfMailfrom = "alice@foo.com";
        r.dkim = SwMailDkim::VerifyResult::Pass;
        r.dkimDomain = "foo.com";
        r.dmarcEvaluated = true;
        r.dmarcDisposition = SwMailDmarc::Disposition::Pass;
        r.dmarc.record.present = true;
        r.headerFromDomain = "foo.com";
        const SwString ar = buildAuthenticationResults_("mail.vigil.design", r);
        if (!ar.contains("spf=pass smtp.mailfrom=alice@foo.com") ||
            !ar.contains("dkim=pass header.d=foo.com") ||
            !ar.contains("dmarc=pass header.from=foo.com")) {
            swCWarning(kSwLogCategory_SwMail) << "[InboundAuthSelfTest] AR string wrong: " << ar.toStdString();
            ok = false;
        }

        SwInboundAuthResult none;
        const SwString ar2 = buildAuthenticationResults_("mail.vigil.design", none);
        if (!ar2.contains("spf=none") || !ar2.contains("dkim=none") || !ar2.contains("dmarc=none")) {
            swCWarning(kSwLogCategory_SwMail) << "[InboundAuthSelfTest] empty AR wrong: " << ar2.toStdString();
            ok = false;
        }
        return ok;
    }

private:
    // Une valeur sûre pour une propriété AR : non vide, sans espace/séparateur de
    // grammaire ni CR/LF (pas d'injection de propriété ni de pliage).
    static bool isArSafe_(const SwString& s) {
        const std::string v = s.toStdString();
        if (v.empty()) {
            return false;
        }
        for (std::size_t i = 0; i < v.size(); ++i) {
            const char c = v[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';' ||
                c == '(' || c == ')' || c == '"' || c == ',') {
                return false;
            }
        }
        return true;
    }

    static SwString dmarcToken_(const SwInboundAuthResult& r) {
        if (!r.dmarcEvaluated || !r.dmarc.record.present) return "none";
        return r.dmarcDisposition == SwMailDmarc::Disposition::Pass ? SwString("pass") : SwString("fail");
    }
};
