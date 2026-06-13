#pragma once

#include "SwCrypto.h"
#include "SwDebug.h"
#include "SwMailCommon.h"
#include "SwMailDnsClient.h"
#include "SwMailStore.h"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Canonicalisation DKIM relaxed partagée entre le signeur et le vérifieur.
// Les corps sont déplacés verbatim depuis SwMailDkimSigner pour garantir que la
// vérification reproduit exactement ce que la signature a calculé (R1).
namespace swMailDkimCanon {

inline std::string canonicalizeBodyRelaxed_(const std::string& raw) {
    std::size_t bodyPos = raw.find("\r\n\r\n");
    std::string body = (bodyPos == std::string::npos) ? std::string() : raw.substr(bodyPos + 4);
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= body.size()) {
        std::size_t end = body.find("\r\n", pos);
        if (end == std::string::npos) {
            end = body.size();
        }
        std::string line = body.substr(pos, end - pos);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        std::string reduced;
        bool previousSpace = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (c == ' ' || c == '\t') {
                if (!previousSpace) {
                    reduced.push_back(' ');
                }
                previousSpace = true;
            } else {
                reduced.push_back(c);
                previousSpace = false;
            }
        }
        lines.push_back(reduced);
        if (end == body.size()) {
            break;
        }
        pos = end + 2;
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        out += "\r\n";
    }
    if (out.empty()) {
        out = "\r\n";
    }
    return out;
}

inline SwString canonicalizeHeaderLineRelaxed_(const SwString& name, const SwString& value) {
    std::string input = value.trimmed().toStdString();
    std::string reduced;
    bool previousSpace = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!previousSpace) {
                reduced.push_back(' ');
            }
            previousSpace = true;
        } else {
            reduced.push_back(c);
            previousSpace = false;
        }
    }
    while (!reduced.empty() && reduced.front() == ' ') {
        reduced.erase(reduced.begin());
    }
    while (!reduced.empty() && reduced.back() == ' ') {
        reduced.pop_back();
    }
    return name.toLower() + ":" + SwString(reduced) + "\r\n";
}

inline SwString computeBodyHashRelaxed_(const SwByteArray& rawMessage) {
    const std::string canonicalBody = canonicalizeBodyRelaxed_(rawMessage.toStdString());
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(canonicalBody.data()), canonicalBody.size(), hash);
    return SwString(SwCrypto::base64Encode(std::vector<unsigned char>(hash, hash + SHA256_DIGEST_LENGTH)));
}

} // namespace swMailDkimCanon

class SwMailDkimSigner {
public:
    static bool ensureDomainKey(SwMailStore& store,
                                const SwString& domain,
                                const SwString& selector,
                                SwMailDkimRecord& outRecord,
                                SwString& outError) {
        outError.clear();
        const SwDbStatus existing = store.getDkimRecord(domain, selector, &outRecord);
        if (existing.ok()) {
            return true;
        }
        if (existing.code() != SwDbStatus::NotFound) {
            outError = existing.message();
            return false;
        }

        SwMailDkimRecord record;
        record.domain = swMailDetail::normalizeDomain(domain);
        record.selector = selector.trimmed().isEmpty() ? SwString("swstack") : selector.trimmed().toLower();
        if (!generateRecord_(record, outError)) {
            return false;
        }
        const SwDbStatus writeStatus = store.upsertDkimRecord(record);
        if (!writeStatus.ok()) {
            outError = writeStatus.message();
            return false;
        }
        outRecord = record;
        return true;
    }

    static bool signMessage(const SwMailConfig& config,
                            SwMailStore& store,
                            SwByteArray& rawMessage,
                            SwString& outSelector,
                            SwString& outError) {
        outError.clear();
        outSelector = "swstack";

        SwMailDkimRecord record;
        if (!ensureDomainKey(store, config.domain, outSelector, record, outError)) {
            return false;
        }

        SwByteArray prepared = swMailDetail::ensureMessageEnvelopeHeaders(config, rawMessage, SwString(), SwList<SwString>());
        SwMap<SwString, SwString> headers = swMailDetail::parseHeaders(prepared);
        // RFC 6376 §5.4 : un en-tête de h= absent du message est traité comme chaîne
        // nulle (rien émis). Plutôt que de signer un "name:" vide (que les vérifieurs
        // stricts type Gmail interprètent comme nul → hash différent → fail), on ne
        // liste dans h= que les en-têtes réellement présents.
        SwList<SwString> headerNames;
        {
            const SwList<SwString> candidates = signedHeaderNames_();
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                if (headers.contains(candidates[i].toLower())) {
                    headerNames.append(candidates[i]);
                }
            }
        }
        const SwString bodyHash = computeBodyHash_(prepared);
        if (bodyHash.isEmpty()) {
            outError = "Unable to compute DKIM body hash";
            return false;
        }

        const SwString signingHeader =
            buildSigningHeader_(record.domain, record.selector, headerNames, bodyHash);
        SwString dkimSigCanon = canonicalizeHeaderLine_("DKIM-Signature", signingHeader);
        // RFC 6376 §3.7 : l'en-tête DKIM-Signature entre dans le hash SANS son CRLF final.
        if (dkimSigCanon.endsWith("\r\n")) {
            dkimSigCanon = dkimSigCanon.left(static_cast<int>(dkimSigCanon.size()) - 2);
        }
        const SwString signingInput = canonicalizeHeaders_(headers, headerNames) + dkimSigCanon;

        SwString signatureValue;
        if (!signSha256_(record.privateKeyPem, signingInput.toStdString(), signatureValue, outError)) {
            return false;
        }

        SwString finalHeader = "DKIM-Signature: " + signingHeader + signatureValue + "\r\n";
        rawMessage = SwByteArray((finalHeader + SwString(prepared.toStdString())).toStdString());
        return true;
    }

private:
    static SwList<SwString> signedHeaderNames_() {
        SwList<SwString> names;
        names.append("from");
        names.append("to");
        names.append("subject");
        names.append("date");
        names.append("message-id");
        return names;
    }

    static bool generateRecord_(SwMailDkimRecord& record, SwString& outError) {
        EVP_PKEY* pkey = EVP_RSA_gen(2048);
        BIO* privateBio = nullptr;
        BIO* publicBio = nullptr;
        SwString publicPem;

        if (!pkey) {
            outError = "OpenSSL RSA key generation failed";
            goto fail;
        }

        privateBio = BIO_new(BIO_s_mem());
        publicBio = BIO_new(BIO_s_mem());
        if (!privateBio || !publicBio) {
            outError = "OpenSSL BIO allocation failed";
            goto fail;
        }
        if (PEM_write_bio_PrivateKey(privateBio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1 ||
            PEM_write_bio_PUBKEY(publicBio, pkey) != 1) {
            outError = "OpenSSL PEM export failed";
            goto fail;
        }

        record.privateKeyPem = bioToString_(privateBio);
        publicPem = bioToString_(publicBio);
        record.publicKeyTxt = buildPublicTxt_(publicPem, outError);
        if (record.publicKeyTxt.isEmpty()) {
            goto fail;
        }
        record.createdAt = swMailDetail::currentIsoTimestamp();
        record.updatedAt = record.createdAt;

        BIO_free(privateBio);
        BIO_free(publicBio);
        EVP_PKEY_free(pkey);
        return true;

    fail:
        if (privateBio) BIO_free(privateBio);
        if (publicBio) BIO_free(publicBio);
        if (pkey) EVP_PKEY_free(pkey);
        return false;
    }

    static SwString bioToString_(BIO* bio) {
        BUF_MEM* mem = nullptr;
        BIO_get_mem_ptr(bio, &mem);
        if (!mem || !mem->data || mem->length <= 0) {
            return SwString();
        }
        return SwString(std::string(mem->data, mem->length));
    }

    static SwString buildPublicTxt_(const SwString& publicPem, SwString& outError) {
        BIO* bio = BIO_new_mem_buf(publicPem.data(), static_cast<int>(publicPem.size()));
        if (!bio) {
            outError = "OpenSSL public key BIO failed";
            return SwString();
        }
        EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!pkey) {
            outError = "OpenSSL public key parse failed";
            return SwString();
        }

        int len = i2d_PUBKEY(pkey, nullptr);
        if (len <= 0) {
            EVP_PKEY_free(pkey);
            outError = "OpenSSL public key export failed";
            return SwString();
        }
        std::vector<unsigned char> der(static_cast<std::size_t>(len));
        unsigned char* cursor = der.data();
        if (i2d_PUBKEY(pkey, &cursor) <= 0) {
            EVP_PKEY_free(pkey);
            outError = "OpenSSL public key DER failed";
            return SwString();
        }
        EVP_PKEY_free(pkey);

        const SwString base64 = SwString(SwCrypto::base64Encode(der));
        return "v=DKIM1; k=rsa; p=" + base64;
    }

    static SwString computeBodyHash_(const SwByteArray& rawMessage) {
        return swMailDkimCanon::computeBodyHashRelaxed_(rawMessage);
    }

    static std::string canonicalizeBody_(const std::string& raw) {
        return swMailDkimCanon::canonicalizeBodyRelaxed_(raw);
    }

    static SwString buildSigningHeader_(const SwString& domain,
                                        const SwString& selector,
                                        const SwList<SwString>& signedHeaders,
                                        const SwString& bodyHash) {
        SwString headerList;
        for (std::size_t i = 0; i < signedHeaders.size(); ++i) {
            if (!headerList.isEmpty()) {
                headerList += ":";
            }
            headerList += signedHeaders[i].toLower();
        }
        return "v=1; a=rsa-sha256; c=relaxed/relaxed; d=" + domain.toLower() +
               "; s=" + selector.toLower() +
               "; t=" + SwString::number(swMailDetail::currentEpochMs() / 1000) +
               "; h=" + headerList +
               "; bh=" + bodyHash +
               "; b=";
    }

    static SwString canonicalizeHeaders_(const SwMap<SwString, SwString>& headers,
                                         const SwList<SwString>& names) {
        SwString out;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const SwString lower = names[i].toLower();
            // RFC 6376 : chaque en-tête listé dans h= entre dans les données signées,
            // même absent (valeur vide), pour que h= corresponde exactement au hash.
            out += canonicalizeHeaderLine_(lower, headers.value(lower));
        }
        return out;
    }

    static SwString canonicalizeHeaderLine_(const SwString& name, const SwString& value) {
        return swMailDkimCanon::canonicalizeHeaderLineRelaxed_(name, value);
    }

    static bool signSha256_(const SwString& privatePem,
                            const std::string& input,
                            SwString& outSignature,
                            SwString& outError) {
        BIO* bio = BIO_new_mem_buf(privatePem.data(), static_cast<int>(privatePem.size()));
        if (!bio) {
            outError = "OpenSSL private key BIO failed";
            return false;
        }
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!pkey) {
            outError = "OpenSSL private key parse failed";
            return false;
        }

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            EVP_PKEY_free(pkey);
            outError = "OpenSSL digest context allocation failed";
            return false;
        }
        std::vector<unsigned char> signature;
        std::size_t signatureLen = 0;
        bool ok = EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
                  EVP_DigestSignUpdate(ctx, input.data(), input.size()) == 1 &&
                  EVP_DigestSignFinal(ctx, nullptr, &signatureLen) == 1;
        if (ok) {
            signature.resize(signatureLen);
            ok = EVP_DigestSignFinal(ctx, signature.data(), &signatureLen) == 1;
        }
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        if (!ok) {
            outError = "OpenSSL DKIM signing failed";
            return false;
        }
        signature.resize(signatureLen);
        outSignature = SwString(SwCrypto::base64Encode(signature));
        return true;
    }
};

// Coquille de portée pour l'enum de résultat partagé par DMARC / l'orchestrateur.
class SwMailDkim {
public:
    enum class VerifyResult { None = 0, Pass, Fail, TempError, PermError };

    static SwString resultToken(VerifyResult r) {
        switch (r) {
            case VerifyResult::Pass: return "pass";
            case VerifyResult::Fail: return "fail";
            case VerifyResult::TempError: return "temperror";
            case VerifyResult::PermError: return "permerror";
            case VerifyResult::None: return "none";
        }
        return "none";
    }
};

struct SwDkimSignatureResult {
    SwString domain;    // d= (minuscule, point final retiré)
    SwString selector;  // s=
    SwMailDkim::VerifyResult result = SwMailDkim::VerifyResult::None;
    SwString reason;
};

// RFC 6376 — vérification DKIM du courrier entrant. relaxed/relaxed uniquement
// (le seul mode que notre signeur émet et le seul byte-exactement reproductible).
class SwMailDkimVerifier {
public:
    static SwList<SwDkimSignatureResult> verifyMessage(const SwByteArray& rawMessage,
                                                       SwMailDnsClient& dns) {
        return verifyMessage(rawMessage, [&dns](const SwString& host) -> SwString {
            const SwList<SwMailDnsTxtRecord> recs = dns.resolveTxt(host);
            for (std::size_t i = 0; i < recs.size(); ++i) {
                const std::string v = recs[i].value.toStdString();
                if (v.find("DKIM1") != std::string::npos || v.find("p=") != std::string::npos) {
                    return recs[i].value;
                }
            }
            return recs.isEmpty() ? SwString() : recs.first().value;
        });
    }

    static SwList<SwDkimSignatureResult> verifyMessage(
        const SwByteArray& rawMessage,
        const std::function<SwString(const SwString&)>& txtLookup) {
        SwList<SwDkimSignatureResult> out;
        const SwList<SwString> sigs = swMailDetail::headerOccurrences(rawMessage, "dkim-signature");
        // Plafond anti-amplification DNS : un attaquant peut empiler N signatures
        // forgées (avec le bon bh=, qui n'est pas secret) pour forcer N lookups TXT.
        const std::size_t maxSigs = sigs.size() < kMaxDkimSignatures_ ? sigs.size() : kMaxDkimSignatures_;
        for (std::size_t i = 0; i < maxSigs; ++i) {
            out.append(verifyOne_(rawMessage, sigs[i], txtLookup));
        }
        return out;
    }

    static bool anyPass(const SwList<SwDkimSignatureResult>& results,
                        SwList<SwString>* outPassDomains = nullptr) {
        bool any = false;
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (results[i].result == SwMailDkim::VerifyResult::Pass) {
                any = true;
                if (outPassDomains) {
                    outPassDomains->append(results[i].domain);
                }
            }
        }
        return any;
    }

    // Round-trip : signe via SwMailDkimSigner puis vérifie avec la clé publique
    // injectée. Valide la cohérence signeur↔vérifieur (R1). Nécessite un store ouvert.
    static bool roundTripSelfTest_(SwMailStore& store, const SwMailConfig& config);

private:
    static constexpr std::size_t kMaxDkimSignatures_ = 5;

    static std::string trim_(const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    static std::string stripWs_(const std::string& s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (!std::isspace(static_cast<unsigned char>(s[i]))) out.push_back(s[i]);
        }
        return out;
    }

    static std::string lower_(const std::string& s) {
        std::string out = s;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
        }
        return out;
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

    // Vide la valeur du tag b= en préservant la structure (pour le hash d'en-tête).
    static std::string emptyBTag_(const std::string& v) {
        std::string out;
        std::size_t i = 0;
        while (true) {
            const std::size_t semi = v.find(';', i);
            const std::string seg = v.substr(i, semi == std::string::npos ? std::string::npos : semi - i);
            const std::size_t eq = seg.find('=');
            if (eq != std::string::npos && lower_(trim_(seg.substr(0, eq))) == "b") {
                out += seg.substr(0, eq + 1);  // garde "...b=", supprime la valeur
            } else {
                out += seg;
            }
            if (semi == std::string::npos) break;
            out += ';';
            i = semi + 1;
        }
        return out;
    }

    static std::string buildSigningInput_(const SwByteArray& raw, const std::string& hTag,
                                          const SwString& sigRawValue) {
        std::vector<std::string> names;
        {
            std::size_t p = 0;
            while (true) {
                const std::size_t c = hTag.find(':', p);
                std::string n = lower_(trim_(c == std::string::npos ? hTag.substr(p) : hTag.substr(p, c - p)));
                if (!n.empty()) names.push_back(n);
                if (c == std::string::npos) break;
                p = c + 1;
            }
        }
        std::map<std::string, std::vector<SwString>> occ;
        std::map<std::string, std::size_t> remaining;
        std::string out;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const std::string& name = names[i];
            if (occ.find(name) == occ.end()) {
                const SwList<SwString> list = swMailDetail::headerOccurrences(raw, SwString(name.c_str()));
                std::vector<SwString> v;
                for (std::size_t k = 0; k < list.size(); ++k) v.push_back(list[k]);
                occ[name] = v;
                remaining[name] = v.size();
            }
            if (remaining[name] > 0) {
                const SwString value = occ[name][--remaining[name]];  // consommation bottom-up
                out += swMailDkimCanon::canonicalizeHeaderLineRelaxed_(SwString(name.c_str()), value).toStdString();
            }
            // RFC 6376 §5.4 : en-tête absent/sur-listé dans h= → chaîne nulle, on n'émet
            // RIEN (ni "name:" ni CRLF). Émettre "name:\r\n" ferait échouer la vérif des
            // mails externes sur-signés (ex. Gmail qui double "date" dans h=).
        }
        const std::string emptied = emptyBTag_(sigRawValue.toStdString());
        std::string sigCanon =
            swMailDkimCanon::canonicalizeHeaderLineRelaxed_("dkim-signature", SwString(emptied.c_str())).toStdString();
        if (sigCanon.size() >= 2 && sigCanon.compare(sigCanon.size() - 2, 2, "\r\n") == 0) {
            sigCanon.resize(sigCanon.size() - 2);  // RFC 6376 §3.7 : pas de CRLF final
        }
        out += sigCanon;
        return out;
    }

    static bool rsaVerify_(const std::string& pubKeyBase64, const std::string& input,
                           const std::string& sigBase64) {
        const SwByteArray der = SwByteArray::fromBase64(SwByteArray(pubKeyBase64.c_str()));
        if (der.size() == 0) return false;
        const unsigned char* dp = reinterpret_cast<const unsigned char*>(der.constData());
        EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &dp, static_cast<long>(der.size()));
        if (!pkey) return false;
        const SwByteArray sig = SwByteArray::fromBase64(SwByteArray(sigBase64.c_str()));
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        bool ok = false;
        if (ctx && EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
            const int rc = EVP_DigestVerify(ctx,
                                            reinterpret_cast<const unsigned char*>(sig.constData()),
                                            static_cast<std::size_t>(sig.size()),
                                            reinterpret_cast<const unsigned char*>(input.data()),
                                            input.size());
            ok = (rc == 1);
        }
        if (ctx) EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return ok;
    }

    static SwDkimSignatureResult verifyOne_(const SwByteArray& raw, const SwString& sigRawValue,
                                            const std::function<SwString(const SwString&)>& txtLookup) {
        using VR = SwMailDkim::VerifyResult;
        SwDkimSignatureResult res;
        const std::map<std::string, std::string> tags = parseTags_(sigRawValue.toStdString());

        if (getTag_(tags, "v") != "1") {
            res.result = VR::PermError; res.reason = "unsupported version"; return res;
        }
        if (lower_(getTag_(tags, "a")) != "rsa-sha256") {
            res.result = VR::PermError; res.reason = "unsupported algorithm"; return res;
        }
        std::string c = lower_(getTag_(tags, "c"));
        if (c.empty()) c = "simple/simple";  // défaut RFC
        std::string ch = c, cb = "simple";
        const std::size_t sl = c.find('/');
        if (sl != std::string::npos) { ch = c.substr(0, sl); cb = c.substr(sl + 1); }
        if (ch != "relaxed" || cb != "relaxed") {
            res.result = VR::PermError; res.reason = "unsupported canonicalization"; return res;
        }

        std::string domain = lower_(getTag_(tags, "d"));
        while (!domain.empty() && domain.back() == '.') domain.pop_back();
        res.domain = SwString(domain.c_str());
        res.selector = SwString(getTag_(tags, "s").c_str());
        const std::string bh = stripWs_(getTag_(tags, "bh"));
        const std::string b = stripWs_(getTag_(tags, "b"));
        const std::string h = getTag_(tags, "h");
        if (domain.empty() || res.selector.isEmpty() || bh.empty() || b.empty() || h.empty()) {
            res.result = VR::PermError; res.reason = "missing required tag"; return res;
        }

        const std::string x = getTag_(tags, "x");
        if (!x.empty()) {
            const long long expiry = std::strtoll(x.c_str(), nullptr, 10);
            if (expiry > 0 && swMailDetail::currentEpochMs() / 1000 > expiry) {
                res.result = VR::Fail; res.reason = "signature expired"; return res;
            }
        }

        // Body hash AVANT toute requête DNS (rejet précoce).
        if (swMailDkimCanon::computeBodyHashRelaxed_(raw).toStdString() != bh) {
            res.result = VR::Fail; res.reason = "body hash mismatch"; return res;
        }

        const std::string signingInput = buildSigningInput_(raw, h, sigRawValue);

        const SwString keyTxt = txtLookup(res.selector + "._domainkey." + SwString(domain.c_str()));
        if (keyTxt.isEmpty()) {
            res.result = VR::TempError; res.reason = "no DKIM key (dns)"; return res;
        }
        const std::map<std::string, std::string> keyTags = parseTags_(keyTxt.toStdString());
        const std::string p = stripWs_(getTag_(keyTags, "p"));
        if (p.empty()) {
            res.result = VR::PermError; res.reason = "key revoked"; return res;
        }
        const std::string k = lower_(getTag_(keyTags, "k"));
        if (!k.empty() && k != "rsa") {
            res.result = VR::PermError; res.reason = "unsupported key type"; return res;
        }

        const bool sigOk = rsaVerify_(p, signingInput, b);
        res.result = sigOk ? VR::Pass : VR::Fail;
        if (!sigOk) res.reason = "signature mismatch";
        return res;
    }
};

inline bool SwMailDkimVerifier::roundTripSelfTest_(SwMailStore& store, const SwMailConfig& config) {
    bool ok = true;
    auto warn = [&](const char* m) {
        swCWarning(kSwLogCategory_SwMail) << "[DkimVerifySelfTest] " << m;
        ok = false;
    };

    const SwString body =
        "From: Alice <alice@" + config.domain + ">\r\n"
        "To: Bob <bob@example.net>\r\n"
        "Subject: DKIM round trip\r\n"
        "Date: " + swMailDetail::smtpDateNow() + "\r\n"
        "Message-Id: <rt-1@" + config.domain + ">\r\n"
        "\r\n"
        "Hello DKIM body line one.\r\n"
        "Second line.\r\n";
    SwByteArray message(body.toUtf8());

    SwString selector;
    SwString signError;
    SwByteArray signed_ = message;
    if (!SwMailDkimSigner::signMessage(config, store, signed_, selector, signError)) {
        warn("signMessage failed");
        return ok;
    }

    SwMailDkimRecord record;
    if (!store.getDkimRecord(config.domain, "swstack", &record).ok()) {
        warn("getDkimRecord failed");
        return ok;
    }
    auto keyLookup = [&](const SwString&) { return record.publicKeyTxt; };

    SwList<SwDkimSignatureResult> good = verifyMessage(signed_, keyLookup);
    if (!anyPass(good)) warn("self-signed message did not verify (R1 — signer/verifier canon mismatch)");

    // Tamper du corps → Fail.
    {
        std::string t = signed_.toStdString();
        const std::size_t bodyPos = t.find("Hello DKIM");
        if (bodyPos != std::string::npos) t[bodyPos] = 'J';
        SwList<SwDkimSignatureResult> r = verifyMessage(SwByteArray(t), keyLookup);
        if (anyPass(r)) warn("tampered body still passed");
    }
    // Tamper d'un en-tête signé → Fail.
    {
        std::string t = signed_.toStdString();
        const std::size_t sub = t.find("DKIM round trip");
        if (sub != std::string::npos) t.replace(sub, 4, "XXXX");
        SwList<SwDkimSignatureResult> r = verifyMessage(SwByteArray(t), keyLookup);
        if (anyPass(r)) warn("tampered subject still passed");
    }
    // Clé révoquée (p=) → PermError, pas de Pass.
    {
        auto revoked = [&](const SwString&) { return SwString("v=DKIM1; k=rsa; p="); };
        SwList<SwDkimSignatureResult> r = verifyMessage(signed_, revoked);
        if (anyPass(r)) warn("revoked key still passed");
    }
    return ok;
}
