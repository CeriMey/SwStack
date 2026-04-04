#pragma once

#include "SwCrypto.h"
#include "SwDebug.h"
#include "SwMailCommon.h"
#include "SwMailStore.h"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include <algorithm>
#include <vector>

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
        const SwList<SwString> headerNames = signedHeaderNames_();
        const SwString bodyHash = computeBodyHash_(prepared);
        if (bodyHash.isEmpty()) {
            outError = "Unable to compute DKIM body hash";
            return false;
        }

        const SwString signingHeader =
            buildSigningHeader_(record.domain, record.selector, headerNames, bodyHash);
        const SwString signingInput = canonicalizeHeaders_(headers, headerNames) +
                                      canonicalizeHeaderLine_("DKIM-Signature", signingHeader);

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
        EVP_PKEY* pkey = EVP_PKEY_new();
        RSA* rsa = RSA_new();
        BIGNUM* exponent = BN_new();
        BIO* privateBio = nullptr;
        BIO* publicBio = nullptr;
        SwString publicPem;

        if (!pkey || !rsa || !exponent) {
            outError = "OpenSSL key allocation failed";
            goto fail;
        }
        if (BN_set_word(exponent, RSA_F4) != 1 ||
            RSA_generate_key_ex(rsa, 2048, exponent, nullptr) != 1 ||
            EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
            outError = "OpenSSL RSA key generation failed";
            goto fail;
        }
        rsa = nullptr;

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
        BN_free(exponent);
        EVP_PKEY_free(pkey);
        return true;

    fail:
        if (privateBio) BIO_free(privateBio);
        if (publicBio) BIO_free(publicBio);
        if (exponent) BN_free(exponent);
        if (rsa) RSA_free(rsa);
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
        const std::string canonicalBody = canonicalizeBody_(rawMessage.toStdString());
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(canonicalBody.data()), canonicalBody.size(), hash);
        return SwString(SwCrypto::base64Encode(std::vector<unsigned char>(hash, hash + SHA256_DIGEST_LENGTH)));
    }

    static std::string canonicalizeBody_(const std::string& raw) {
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
            const SwString value = headers.value(lower);
            if (value.isEmpty()) {
                continue;
            }
            out += canonicalizeHeaderLine_(lower, value);
        }
        return out;
    }

    static SwString canonicalizeHeaderLine_(const SwString& name, const SwString& value) {
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
