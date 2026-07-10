#ifndef SWQUICCERTIFICATEVERIFIER_H
#define SWQUICCERTIFICATEVERIFIER_H

#include "SwVector.h"
#include "SwByteArray.h"
#include "SwString.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#endif

// VIGIL links OpenSSL on every supported platform. Windows CryptoAPI remains
// the X.509 verifier; OpenSSL supplies Ed25519 because CNG exposes no stable
// Ed25519 CertificateVerify primitive in the supported SDK baseline.
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>

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
// X.509 supports RSA-PSS and ECDSA as before. RFC 7250 raw public keys use
// Ed25519 (0x0807) and are parsed/verified by the dedicated methods below.
class SwQuicCertificateVerifier {
public:
    // Parse one canonical RFC 8410 Ed25519 SubjectPublicKeyInfo and return the
    // exact 32-byte raw public key. Parameters, BER aliases and trailing data
    // are rejected by a byte-identical DER round-trip.
    static bool extractEd25519RawPublicKey(const SwByteArray& spkiDer,
                                           SwByteArray& outRawPublicKey,
                                           SwString* error = nullptr) {
        outRawPublicKey.clear();
        if (spkiDer.isEmpty() || !spkiDer.constData() ||
            spkiDer.size() > static_cast<std::size_t>((std::numeric_limits<long>::max)())) {
            setError_(error, "Ed25519 SPKI is empty or too large");
            return false;
        }
        const unsigned char* input =
            reinterpret_cast<const unsigned char*>(spkiDer.constData());
        const unsigned char* cursor = input;
        EVP_PKEY* key = d2i_PUBKEY(nullptr, &cursor, static_cast<long>(spkiDer.size()));
        if (!key || cursor != input + spkiDer.size() ||
            EVP_PKEY_base_id(key) != EVP_PKEY_ED25519) {
            if (key) EVP_PKEY_free(key);
            setError_(error, "Raw public key is not one canonical Ed25519 SPKI");
            return false;
        }
        const int encodedLength = i2d_PUBKEY(key, nullptr);
        SwVector<unsigned char> encoded(
            encodedLength > 0 ? static_cast<std::size_t>(encodedLength) : 0, 0);
        unsigned char* encodedCursor = encoded.empty() ? nullptr : encoded.data();
        const int encodedWritten = encodedCursor ? i2d_PUBKEY(key, &encodedCursor) : -1;
        unsigned char raw[32]{};
        std::size_t rawLength = sizeof(raw);
        const bool canonical = encodedLength == static_cast<int>(spkiDer.size()) &&
            encodedWritten == encodedLength &&
            std::memcmp(encoded.data(), input, spkiDer.size()) == 0 &&
            EVP_PKEY_get_raw_public_key(key, raw, &rawLength) == 1 &&
            rawLength == sizeof(raw);
        EVP_PKEY_free(key);
        if (!canonical) {
            setError_(error, "Ed25519 SPKI is not canonical RFC 8410 DER");
            return false;
        }
        outRawPublicKey = SwByteArray(
            reinterpret_cast<const char*>(raw), sizeof(raw));
        clearError_(error);
        return true;
    }

    static bool verifyRawPublicKeyCertificateVerify(
            const SwByteArray& spkiDer,
            std::uint16_t signatureScheme,
            const SwByteArray& signature,
            const SwByteArray& transcriptHash,
            SwString* error = nullptr,
            bool serverContext = true) {
        if (signatureScheme != 0x0807 || signature.size() != 64) {
            setError_(error, "RPK CertificateVerify must use ed25519 (0x0807)");
            return false;
        }
        SwByteArray raw;
        if (!extractEd25519RawPublicKey(spkiDer, raw, error)) return false;

        const unsigned char* cursor =
            reinterpret_cast<const unsigned char*>(spkiDer.constData());
        EVP_PKEY* key = d2i_PUBKEY(nullptr, &cursor, static_cast<long>(spkiDer.size()));
        if (!key) {
            setError_(error, "Cannot import Ed25519 raw public key");
            return false;
        }
        SwByteArray content;
        for (int i = 0; i < 64; ++i) content.append(static_cast<char>(0x20));
        content.append(serverContext ? "TLS 1.3, server CertificateVerify"
                                     : "TLS 1.3, client CertificateVerify");
        content.append(static_cast<char>(0));
        content.append(transcriptHash);

        EVP_MD_CTX* context = EVP_MD_CTX_new();
        bool ok = context &&
            EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1 &&
            EVP_DigestVerify(
                context,
                reinterpret_cast<const unsigned char*>(signature.constData()),
                static_cast<std::size_t>(signature.size()),
                reinterpret_cast<const unsigned char*>(content.constData()),
                static_cast<std::size_t>(content.size())) == 1;
        if (context) EVP_MD_CTX_free(context);
        EVP_PKEY_free(key);
        if (!ok) {
            setError_(error, "Ed25519 RPK CertificateVerify signature check failed");
            return false;
        }
        clearError_(error);
        return true;
    }

    // Extract the canonical DER SubjectPublicKeyInfo carried by one strict DER
    // X.509 certificate. This is X.509/SPKI pinning support: it does not mean
    // that RFC 7250 RawPublicKey was negotiated on the TLS wire.
    static bool extractSubjectPublicKeyInfo(const SwByteArray& certificateDer,
                                            SwByteArray& outSpkiDer,
                                            SwString* error = nullptr) {
        outSpkiDer.clear();
        if (certificateDer.isEmpty() || !certificateDer.constData()) {
            setError_(error, "Cannot extract SPKI from an empty certificate");
            return false;
        }
        if (!isStrictDerCertificate_(certificateDer)) {
            setError_(error, "Certificate is not a single strict DER X.509 value");
            return false;
        }

#if defined(_WIN32)
        if (certificateDer.size() > static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
            setError_(error, "Certificate DER is too large for CryptoAPI");
            return false;
        }
        PCCERT_CONTEXT certificate = CertCreateCertificateContext(
            X509_ASN_ENCODING,
            reinterpret_cast<const BYTE*>(certificateDer.constData()),
            static_cast<DWORD>(certificateDer.size()));
        if (!certificate || !certificate->pCertInfo) {
            if (certificate) {
                CertFreeCertificateContext(certificate);
            }
            setError_(error, "Certificate is malformed X.509 DER");
            return false;
        }

        DWORD encodedLength = 0;
        const BOOL sized = CryptEncodeObjectEx(
            X509_ASN_ENCODING,
            X509_PUBLIC_KEY_INFO,
            &certificate->pCertInfo->SubjectPublicKeyInfo,
            0,
            nullptr,
            nullptr,
            &encodedLength);
        if (!sized || encodedLength == 0) {
            CertFreeCertificateContext(certificate);
            setError_(error, "CryptoAPI could not encode the certificate SPKI");
            return false;
        }

        SwVector<unsigned char> encoded(static_cast<std::size_t>(encodedLength), 0);
        DWORD written = encodedLength;
        const BOOL encodedOk = CryptEncodeObjectEx(
            X509_ASN_ENCODING,
            X509_PUBLIC_KEY_INFO,
            &certificate->pCertInfo->SubjectPublicKeyInfo,
            0,
            nullptr,
            encoded.data(),
            &written);
        CertFreeCertificateContext(certificate);
        if (!encodedOk || written == 0 || written != encodedLength) {
            setError_(error, "CryptoAPI could not extract a canonical certificate SPKI");
            return false;
        }
        outSpkiDer = SwByteArray(reinterpret_cast<const char*>(encoded.data()),
                                 static_cast<std::size_t>(written));
#else
        if (certificateDer.size() >
            static_cast<std::size_t>((std::numeric_limits<long>::max)())) {
            setError_(error, "Certificate DER is too large for OpenSSL");
            return false;
        }
        const unsigned char* input =
            reinterpret_cast<const unsigned char*>(certificateDer.constData());
        const unsigned char* cursor = input;
        X509* certificate = d2i_X509(nullptr, &cursor, static_cast<long>(certificateDer.size()));
        if (!certificate || cursor != input + certificateDer.size()) {
            if (certificate) {
                X509_free(certificate);
            }
            setError_(error, "Certificate is malformed X.509 DER");
            return false;
        }

        // d2i accepts a wider BER-like input surface on some OpenSSL versions.
        // Round-trip the whole certificate and demand byte equality so callers
        // cannot pin two encodings of the same parsed object.
        const int canonicalCertificateLength = i2d_X509(certificate, nullptr);
        if (canonicalCertificateLength <= 0 ||
            static_cast<std::size_t>(canonicalCertificateLength) != certificateDer.size()) {
            X509_free(certificate);
            setError_(error, "Certificate input is not canonical DER");
            return false;
        }
        SwVector<unsigned char> canonicalCertificate(
            static_cast<std::size_t>(canonicalCertificateLength), 0);
        unsigned char* canonicalCursor = canonicalCertificate.data();
        const int canonicalWritten = i2d_X509(certificate, &canonicalCursor);
        if (canonicalWritten != canonicalCertificateLength ||
            std::memcmp(canonicalCertificate.data(), input, certificateDer.size()) != 0) {
            X509_free(certificate);
            setError_(error, "Certificate input is not canonical DER");
            return false;
        }

        X509_PUBKEY* publicKeyInfo = X509_get_X509_PUBKEY(certificate);
        const int spkiLength = publicKeyInfo ? i2d_X509_PUBKEY(publicKeyInfo, nullptr) : -1;
        if (spkiLength <= 0) {
            X509_free(certificate);
            setError_(error, "OpenSSL could not extract the certificate SPKI");
            return false;
        }
        SwVector<unsigned char> encoded(static_cast<std::size_t>(spkiLength), 0);
        unsigned char* encodedCursor = encoded.data();
        const int spkiWritten = i2d_X509_PUBKEY(publicKeyInfo, &encodedCursor);
        X509_free(certificate);
        if (spkiWritten != spkiLength) {
            setError_(error, "OpenSSL could not encode a canonical certificate SPKI");
            return false;
        }
        outSpkiDer = SwByteArray(reinterpret_cast<const char*>(encoded.data()),
                                 static_cast<std::size_t>(spkiWritten));
#endif

        if (outSpkiDer.isEmpty()) {
            setError_(error, "Certificate SPKI extraction returned an empty value");
            return false;
        }
        clearError_(error);
        return true;
    }

    // Validate the server certificate chain (leaf first) for `hostName`.
    static bool verifyServerChain(const SwVector<SwByteArray>& chainDer,
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

            SwVector<wchar_t> wideHost = toWide_(hostName);

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
                                        SwString* error = nullptr,
                                        bool serverContext = true) {
#if defined(_WIN32)
        SwByteArray content;
        for (int i = 0; i < 64; ++i) {
            content.append(static_cast<char>(0x20));
        }
        content.append(serverContext ? "TLS 1.3, server CertificateVerify"
                                     : "TLS 1.3, client CertificateVerify");
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
        content.append(serverContext ? "TLS 1.3, server CertificateVerify"
                                     : "TLS 1.3, client CertificateVerify");
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
    static bool isStrictDerCertificate_(const SwByteArray& der) noexcept {
        if (der.isEmpty() || !der.constData() ||
            static_cast<unsigned char>(der.constData()[0]) != 0x30U) {
            return false;
        }
        std::size_t consumed = 0;
        return readStrictDerElement_(
                   reinterpret_cast<const unsigned char*>(der.constData()),
                   der.size(), 0, consumed) &&
               consumed == der.size();
    }

    // Validate the structural DER rules needed at this trust boundary: one
    // definite-length TLV tree, minimal tag/length encodings, canonical basic
    // primitive encodings, bounded nesting, and no trailing bytes.
    static bool readStrictDerElement_(const unsigned char* data,
                                      std::size_t available,
                                      std::size_t depth,
                                      std::size_t& consumed) noexcept {
        consumed = 0;
        if (!data || available < 2 || depth > 64) {
            return false;
        }

        std::size_t pos = 0;
        const unsigned char tag = data[pos++];
        if ((tag & 0x1fU) == 0x1fU) {
            if (pos >= available || (data[pos] & 0x7fU) == 0) {
                return false;
            }
            std::size_t tagNumber = 0;
            unsigned char part = 0;
            do {
                if (pos >= available ||
                    tagNumber > (static_cast<std::size_t>(-1) >> 7)) {
                    return false;
                }
                part = data[pos++];
                tagNumber = (tagNumber << 7) | (part & 0x7fU);
            } while ((part & 0x80U) != 0);
            if (tagNumber < 31) {
                return false;
            }
        }

        if (pos >= available) {
            return false;
        }
        const unsigned char firstLength = data[pos++];
        std::size_t contentLength = 0;
        if ((firstLength & 0x80U) == 0) {
            contentLength = firstLength;
        } else {
            const std::size_t lengthBytes = firstLength & 0x7fU;
            if (lengthBytes == 0 || lengthBytes > sizeof(std::size_t) ||
                pos + lengthBytes > available || data[pos] == 0) {
                return false;
            }
            for (std::size_t i = 0; i < lengthBytes; ++i) {
                if (contentLength > (static_cast<std::size_t>(-1) >> 8)) {
                    return false;
                }
                contentLength = (contentLength << 8) | data[pos++];
            }
            if (contentLength < 128) {
                return false;
            }
        }
        if (contentLength > available - pos) {
            return false;
        }

        const std::size_t contentStart = pos;
        const std::size_t contentEnd = contentStart + contentLength;
        // DER permits constructed encodings here only for the universal
        // SEQUENCE and SET types used by X.509. In particular, BER's
        // constructed BIT STRING/OCTET STRING forms (0x23/0x24) are rejected.
        if ((tag & 0xc0U) == 0 && (tag & 0x20U) != 0 &&
            tag != 0x30U && tag != 0x31U) {
            return false;
        }
        if ((tag == 0x10U || tag == 0x11U)) {
            return false;
        }
        if ((tag & 0x20U) != 0) {
            while (pos < contentEnd) {
                std::size_t childLength = 0;
                if (!readStrictDerElement_(data + pos, contentEnd - pos,
                                           depth + 1, childLength) ||
                    childLength == 0) {
                    return false;
                }
                pos += childLength;
            }
        } else {
            // Canonical DER forms for primitives that occur at the certificate
            // boundary. Opaque OCTET STRING payloads remain opaque by design.
            if (tag == 0x01U &&
                (contentLength != 1 || (data[contentStart] != 0x00U &&
                                        data[contentStart] != 0xffU))) {
                return false;
            }
            if (tag == 0x02U) {
                if (contentLength == 0 ||
                    (contentLength > 1 && data[contentStart] == 0x00U &&
                     (data[contentStart + 1] & 0x80U) == 0) ||
                    (contentLength > 1 && data[contentStart] == 0xffU &&
                     (data[contentStart + 1] & 0x80U) != 0)) {
                    return false;
                }
            }
            if (tag == 0x03U) {
                if (contentLength == 0 || data[contentStart] > 7 ||
                    (contentLength == 1 && data[contentStart] != 0) ||
                    (contentLength > 1 && data[contentStart] != 0 &&
                     (data[contentEnd - 1] &
                      static_cast<unsigned char>((1U << data[contentStart]) - 1U)) != 0)) {
                    return false;
                }
            }
            if (tag == 0x05U && contentLength != 0) {
                return false;
            }
            pos = contentEnd;
        }

        if (pos != contentEnd) {
            return false;
        }
        consumed = contentEnd;
        return true;
    }

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
    static SwVector<wchar_t> toWide_(const SwString& text) {
        SwVector<wchar_t> wide;
        const int needed = MultiByteToWideChar(CP_UTF8, 0, text.constData(), -1, nullptr, 0);
        wide.resize(needed > 0 ? static_cast<std::size_t>(needed) : 1, L'\0');
        if (needed > 0) {
            MultiByteToWideChar(CP_UTF8, 0, text.constData(), -1, wide.data(), needed);
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

        SwVector<unsigned char> digest(digestLength, 0);
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
