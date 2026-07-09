#ifndef SWQUICCERTIFICATEVERIFIER_H
#define SWQUICCERTIFICATEVERIFIER_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#endif

// Server authentication for the QUIC TLS 1.3 handshake (RFC 8446 sections
// 4.4.2/4.4.3), delegated entirely to the Windows platform crypto:
//
//   * verifyServerChain()      -- X.509 path building, revocation-less trust
//                                 evaluation against the system root store and
//                                 SSL server policy incl. hostname matching,
//                                 via CryptoAPI (crypt32).
//   * verifyCertificateVerify()-- the CertificateVerify signature over the
//                                 handshake transcript, via CNG (bcrypt).
//
// Supported signature schemes: rsa_pss_rsae_sha256/384/512 (0x0804..0x0806)
// and ecdsa_secp256r1_sha256 / ecdsa_secp384r1_sha384 (0x0403, 0x0503) --
// the set current HTTP/3 servers actually negotiate.
class SwQuicCertificateVerifier {
public:
    // Validate the server certificate chain (leaf first) for `hostName`.
    static bool verifyServerChain(const std::vector<SwByteArray>& chainDer,
                                  const SwString& hostName,
                                  SwString* error = nullptr) {
#if defined(_WIN32)
        if (chainDer.empty()) {
            setError_(error, "Server sent an empty certificate chain");
            return false;
        }

        HCERTSTORE extraStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0,
                                              CERT_STORE_CREATE_NEW_FLAG, nullptr);
        if (!extraStore) {
            setError_(error, "CertOpenStore(memory) failed");
            return false;
        }

        bool ok = false;
        PCCERT_CONTEXT leaf = nullptr;
        PCCERT_CHAIN_CONTEXT chain = nullptr;

        do {
            for (std::size_t i = 1; i < chainDer.size(); ++i) {
                CertAddEncodedCertificateToStore(
                    extraStore,
                    X509_ASN_ENCODING,
                    reinterpret_cast<const BYTE*>(chainDer[i].constData()),
                    static_cast<DWORD>(chainDer[i].size()),
                    CERT_STORE_ADD_REPLACE_EXISTING,
                    nullptr);
            }

            leaf = CertCreateCertificateContext(
                X509_ASN_ENCODING,
                reinterpret_cast<const BYTE*>(chainDer[0].constData()),
                static_cast<DWORD>(chainDer[0].size()));
            if (!leaf) {
                setError_(error, "Server leaf certificate is not valid DER");
                break;
            }

            const char* serverAuthUsage = szOID_PKIX_KP_SERVER_AUTH;
            CERT_CHAIN_PARA chainPara;
            memset(&chainPara, 0, sizeof(chainPara));
            chainPara.cbSize = sizeof(chainPara);
            chainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
            chainPara.RequestedUsage.Usage.cUsageIdentifier = 1;
            chainPara.RequestedUsage.Usage.rgpszUsageIdentifier =
                const_cast<LPSTR*>(&serverAuthUsage);

            if (!CertGetCertificateChain(nullptr, leaf, nullptr, extraStore,
                                         &chainPara, 0, nullptr, &chain)) {
                setError_(error, "CertGetCertificateChain failed");
                break;
            }

            std::vector<wchar_t> wideHost = toWide_(hostName);

            SSL_EXTRA_CERT_CHAIN_POLICY_PARA sslPara;
            memset(&sslPara, 0, sizeof(sslPara));
            sslPara.cbStruct = sizeof(sslPara);
            sslPara.dwAuthType = AUTHTYPE_SERVER;
            sslPara.pwszServerName = wideHost.data();

            CERT_CHAIN_POLICY_PARA policyPara;
            memset(&policyPara, 0, sizeof(policyPara));
            policyPara.cbSize = sizeof(policyPara);
            policyPara.pvExtraPolicyPara = &sslPara;

            CERT_CHAIN_POLICY_STATUS policyStatus;
            memset(&policyStatus, 0, sizeof(policyStatus));
            policyStatus.cbSize = sizeof(policyStatus);

            if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain,
                                                  &policyPara, &policyStatus)) {
                setError_(error, "CertVerifyCertificateChainPolicy failed");
                break;
            }
            if (policyStatus.dwError != 0) {
                SwString message = SwString("Server certificate rejected: ");
                message += policyErrorText_(policyStatus.dwError);
                if (error) {
                    *error = message;
                }
                break;
            }

            ok = true;
            clearError_(error);
        } while (false);

        if (chain) {
            CertFreeCertificateChain(chain);
        }
        if (leaf) {
            CertFreeCertificateContext(leaf);
        }
        CertCloseStore(extraStore, 0);
        return ok;
#else
        (void)chainDer;
        (void)hostName;
        setError_(error, "Certificate verification is only implemented on Windows");
        return false;
#endif
    }

    // Verify the CertificateVerify signature (RFC 8446 section 4.4.3): the
    // signed content is 64 spaces, the context string, a zero byte and the
    // transcript hash up to the Certificate message.
    static bool verifyCertificateVerify(const SwByteArray& leafCertificateDer,
                                        std::uint16_t signatureScheme,
                                        const SwByteArray& signature,
                                        const SwByteArray& transcriptHash,
                                        SwString* error = nullptr) {
#if defined(_WIN32)
        SwByteArray content;
        for (int i = 0; i < 64; ++i) {
            content.append(static_cast<char>(0x20));
        }
        content.append("TLS 1.3, server CertificateVerify");
        content.append(static_cast<char>(0));
        content.append(transcriptHash);

        const wchar_t* hashAlgorithm = nullptr;
        std::size_t hashLength = 0;
        bool isPss = false;
        bool isEcdsa = false;
        std::size_t curveCoordinateSize = 0;

        switch (signatureScheme) {
        case 0x0804: hashAlgorithm = BCRYPT_SHA256_ALGORITHM; hashLength = 32; isPss = true; break;
        case 0x0805: hashAlgorithm = BCRYPT_SHA384_ALGORITHM; hashLength = 48; isPss = true; break;
        case 0x0806: hashAlgorithm = BCRYPT_SHA512_ALGORITHM; hashLength = 64; isPss = true; break;
        case 0x0403: hashAlgorithm = BCRYPT_SHA256_ALGORITHM; hashLength = 32; isEcdsa = true;
                     curveCoordinateSize = 32; break;
        case 0x0503: hashAlgorithm = BCRYPT_SHA384_ALGORITHM; hashLength = 48; isEcdsa = true;
                     curveCoordinateSize = 48; break;
        default:
            setError_(error, "Unsupported TLS 1.3 signature scheme in CertificateVerify");
            return false;
        }

        SwByteArray digest;
        if (!hash_(hashAlgorithm, content, hashLength, digest, error)) {
            return false;
        }

        PCCERT_CONTEXT certificate = CertCreateCertificateContext(
            X509_ASN_ENCODING,
            reinterpret_cast<const BYTE*>(leafCertificateDer.constData()),
            static_cast<DWORD>(leafCertificateDer.size()));
        if (!certificate) {
            setError_(error, "Server leaf certificate is not valid DER");
            return false;
        }

        BCRYPT_KEY_HANDLE publicKey = nullptr;
        const BOOL imported = CryptImportPublicKeyInfoEx2(
            X509_ASN_ENCODING,
            &certificate->pCertInfo->SubjectPublicKeyInfo,
            0,
            nullptr,
            &publicKey);
        CertFreeCertificateContext(certificate);
        if (!imported || !publicKey) {
            setError_(error, "CryptImportPublicKeyInfoEx2 failed for the server public key");
            return false;
        }

        bool ok = false;
        if (isPss) {
            BCRYPT_PSS_PADDING_INFO padding;
            padding.pszAlgId = hashAlgorithm;
            padding.cbSalt = static_cast<ULONG>(hashLength);
            const NTSTATUS status = BCryptVerifySignature(
                publicKey,
                &padding,
                reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
                static_cast<ULONG>(digest.size()),
                reinterpret_cast<PUCHAR>(const_cast<char*>(signature.constData())),
                static_cast<ULONG>(signature.size()),
                BCRYPT_PAD_PSS);
            ok = (status == 0);
        } else if (isEcdsa) {
            SwByteArray rawSignature;
            if (ecdsaDerToRaw_(signature, curveCoordinateSize, rawSignature, error)) {
                const NTSTATUS status = BCryptVerifySignature(
                    publicKey,
                    nullptr,
                    reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
                    static_cast<ULONG>(digest.size()),
                    reinterpret_cast<PUCHAR>(const_cast<char*>(rawSignature.constData())),
                    static_cast<ULONG>(rawSignature.size()),
                    0);
                ok = (status == 0);
            }
        }

        BCryptDestroyKey(publicKey);
        if (!ok) {
            if (error && error->isEmpty()) {
                setError_(error, "CertificateVerify signature check failed");
            }
            return false;
        }

        clearError_(error);
        return true;
#else
        // Contenu signé RFC 8446 §4.4.3 : 64 espaces ‖ "TLS 1.3, server CertificateVerify" ‖ 0x00 ‖ hash.
        SwByteArray content;
        for (int i = 0; i < 64; ++i) content.append(static_cast<char>(0x20));
        content.append("TLS 1.3, server CertificateVerify");
        content.append(static_cast<char>(0));
        content.append(transcriptHash);

        const EVP_MD* md = nullptr;
        bool isPss = false;
        switch (signatureScheme) {
        case 0x0403: md = EVP_sha256(); break;                 // ecdsa_secp256r1_sha256
        case 0x0503: md = EVP_sha384(); break;                 // ecdsa_secp384r1_sha384
        case 0x0804: md = EVP_sha256(); isPss = true; break;   // rsa_pss_rsae_sha256
        case 0x0805: md = EVP_sha384(); isPss = true; break;
        case 0x0806: md = EVP_sha512(); isPss = true; break;
        default:
            setError_(error, "Unsupported TLS 1.3 signature scheme in CertificateVerify");
            return false;
        }

        const unsigned char* p = reinterpret_cast<const unsigned char*>(leafCertificateDer.constData());
        X509* cert = d2i_X509(nullptr, &p, static_cast<long>(leafCertificateDer.size()));
        if (!cert) {
            setError_(error, "Server leaf certificate is not valid DER");
            return false;
        }
        EVP_PKEY* pub = X509_get_pubkey(cert);
        X509_free(cert);
        if (!pub) {
            setError_(error, "Cannot extract server public key from certificate");
            return false;
        }

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_PKEY_CTX* pctx = nullptr;
        bool ok = ctx && EVP_DigestVerifyInit(ctx, &pctx, md, nullptr, pub) > 0;
        if (ok && isPss) {
            ok = EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) > 0
              && EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST) > 0;
        }
        if (ok) {
            // ECDSA : la signature est l'ECDSA-Sig-Value DER, consommée directement par EVP_DigestVerify.
            const int rc = EVP_DigestVerify(ctx,
                reinterpret_cast<const unsigned char*>(signature.constData()),
                static_cast<std::size_t>(signature.size()),
                reinterpret_cast<const unsigned char*>(content.constData()),
                static_cast<std::size_t>(content.size()));
            ok = (rc == 1);
        }
        if (ctx) EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pub);
        if (!ok) {
            setError_(error, "CertificateVerify signature check failed");
            return false;
        }
        clearError_(error);
        return true;
#endif
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static void clearError_(SwString* error) {
        if (error) {
            *error = SwString();
        }
    }

#if defined(_WIN32)
    static std::vector<wchar_t> toWide_(const SwString& text) {
        const std::string utf8 = text.toStdString();
        std::vector<wchar_t> wide;
        const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        wide.resize(needed > 0 ? static_cast<std::size_t>(needed) : 1, L'\0');
        if (needed > 0) {
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), needed);
        }
        return wide;
    }

    static const char* policyErrorText_(DWORD policyError) {
        if (policyError == static_cast<DWORD>(CERT_E_EXPIRED)) {
            return "certificate is expired";
        }
        if (policyError == static_cast<DWORD>(CERT_E_CN_NO_MATCH)) {
            return "hostname does not match the certificate";
        }
        if (policyError == static_cast<DWORD>(CERT_E_UNTRUSTEDROOT)) {
            return "chain does not end at a trusted root";
        }
        if (policyError == static_cast<DWORD>(CERT_E_WRONG_USAGE)) {
            return "certificate is not valid for server authentication";
        }
        if (policyError == static_cast<DWORD>(TRUST_E_CERT_SIGNATURE)) {
            return "certificate signature is invalid";
        }
        if (policyError == static_cast<DWORD>(CERT_E_CHAINING)) {
            return "chain could not be built";
        }
        return "chain policy check failed";
    }

    static bool hash_(const wchar_t* algorithm,
                      const SwByteArray& data,
                      std::size_t digestLength,
                      SwByteArray& outDigest,
                      SwString* error) {
        BCRYPT_ALG_HANDLE provider = nullptr;
        if (BCryptOpenAlgorithmProvider(&provider, algorithm, nullptr, 0) != 0) {
            setError_(error, "BCryptOpenAlgorithmProvider(hash) failed");
            return false;
        }

        std::vector<unsigned char> digest(digestLength, 0);
        const NTSTATUS status = BCryptHash(provider,
                                           nullptr,
                                           0,
                                           reinterpret_cast<PUCHAR>(
                                               const_cast<char*>(data.constData())),
                                           static_cast<ULONG>(data.size()),
                                           digest.data(),
                                           static_cast<ULONG>(digest.size()));
        BCryptCloseAlgorithmProvider(provider, 0);
        if (status != 0) {
            setError_(error, "BCryptHash failed");
            return false;
        }

        outDigest = SwByteArray(reinterpret_cast<const char*>(digest.data()), digest.size());
        return true;
    }

    // TLS carries ECDSA signatures DER encoded (SEQUENCE { INTEGER r, INTEGER s });
    // CNG expects the raw fixed-size r||s concatenation.
    static bool ecdsaDerToRaw_(const SwByteArray& der,
                               std::size_t coordinateSize,
                               SwByteArray& outRaw,
                               SwString* error) {
        std::size_t pos = 0;
        std::uint8_t tag = 0;
        std::size_t length = 0;
        if (!readDerHeader_(der, pos, tag, length, error) || tag != 0x30) {
            setError_(error, "ECDSA signature is not a DER sequence");
            return false;
        }

        SwByteArray r;
        SwByteArray s;
        if (!readDerInteger_(der, pos, r, error) ||
            !readDerInteger_(der, pos, s, error)) {
            return false;
        }
        if (r.size() > coordinateSize || s.size() > coordinateSize) {
            setError_(error, "ECDSA signature coordinate is too large for the curve");
            return false;
        }

        outRaw.clear();
        for (std::size_t i = r.size(); i < coordinateSize; ++i) {
            outRaw.append(static_cast<char>(0));
        }
        outRaw.append(r);
        for (std::size_t i = s.size(); i < coordinateSize; ++i) {
            outRaw.append(static_cast<char>(0));
        }
        outRaw.append(s);
        return true;
    }

    static bool readDerHeader_(const SwByteArray& der,
                               std::size_t& pos,
                               std::uint8_t& outTag,
                               std::size_t& outLength,
                               SwString* error) {
        if (pos + 2 > der.size()) {
            setError_(error, "DER structure is truncated");
            return false;
        }
        outTag = static_cast<std::uint8_t>(der.constData()[pos]);
        std::uint8_t first = static_cast<std::uint8_t>(der.constData()[pos + 1]);
        pos += 2;

        if ((first & 0x80U) == 0) {
            outLength = first;
            return true;
        }
        const std::size_t lengthBytes = first & 0x7fU;
        if (lengthBytes == 0 || lengthBytes > 4 || pos + lengthBytes > der.size()) {
            setError_(error, "DER length encoding is invalid");
            return false;
        }
        std::size_t length = 0;
        for (std::size_t i = 0; i < lengthBytes; ++i) {
            length = (length << 8) | static_cast<std::uint8_t>(der.constData()[pos + i]);
        }
        pos += lengthBytes;
        outLength = length;
        return true;
    }

    static bool readDerInteger_(const SwByteArray& der,
                                std::size_t& pos,
                                SwByteArray& outValue,
                                SwString* error) {
        std::uint8_t tag = 0;
        std::size_t length = 0;
        if (!readDerHeader_(der, pos, tag, length, error) || tag != 0x02) {
            setError_(error, "DER integer expected in ECDSA signature");
            return false;
        }
        if (pos + length > der.size()) {
            setError_(error, "DER integer is truncated");
            return false;
        }

        std::size_t start = pos;
        std::size_t remaining = length;
        while (remaining > 1 &&
               static_cast<std::uint8_t>(der.constData()[start]) == 0) {
            ++start;
            --remaining;
        }
        outValue = der.mid(static_cast<int>(start), static_cast<int>(remaining));
        pos += length;
        return true;
    }
#endif
};

#endif
