#ifndef SWQUICSERVERCREDENTIAL_H
#define SWQUICSERVERCREDENTIAL_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

// Server-side TLS 1.3 credential for the QUIC handshake: the certificate chain
// to present (leaf first) plus a signing callback that produces the
// CertificateVerify signature (RFC 8446 section 4.4.3) over the assembled
// signed content.
//
// SwQuicEcdsaCredential::createSelfSigned() builds a self-contained credential
// from a freshly generated ECDSA P-256 key (via CNG/BCrypt): it hand-encodes a
// minimal valid X.509 certificate carrying that public key and signs with
// ecdsa_secp256r1_sha256 (scheme 0x0403). The certificate is self-signed with a
// placeholder signature: it is meant for a peer that verifies the
// CertificateVerify signature but does not require the chain to reach a trusted
// root (SwQuicHandshakeClient::setVerifyCertificateChain(false)).
struct SwQuicServerCredential {
    std::vector<SwByteArray> certificateChain; // leaf first, DER
    std::uint16_t signatureScheme;             // TLS SignatureScheme
    // Signs the fully assembled CertificateVerify content and returns the
    // signature exactly as it must appear on the wire (DER for ECDSA).
    std::function<bool(const SwByteArray& signedContent,
                       SwByteArray& outSignature,
                       SwString* error)> sign;

    SwQuicServerCredential() : signatureScheme(0) {}

    bool isValid() const { return !certificateChain.empty() && sign != nullptr; }
};

class SwQuicEcdsaCredential {
public:
    static bool createSelfSigned(const SwString& hostName,
                                 SwQuicServerCredential& outCredential,
                                 SwString* error = nullptr) {
#if defined(_WIN32)
        std::shared_ptr<KeyState_> keyState(new KeyState_());
        if (!keyState->generate(error)) {
            return false;
        }

        SwByteArray publicX;
        SwByteArray publicY;
        if (!keyState->exportPublicPoint(publicX, publicY, error)) {
            return false;
        }

        SwByteArray certificate;
        if (!buildCertificate_(hostName, publicX, publicY, certificate, error)) {
            return false;
        }

        outCredential.certificateChain.clear();
        outCredential.certificateChain.push_back(certificate);
        outCredential.signatureScheme = 0x0403; // ecdsa_secp256r1_sha256

        std::shared_ptr<KeyState_> capturedKey = keyState;
        outCredential.sign = [capturedKey](const SwByteArray& content,
                                           SwByteArray& outSignature,
                                           SwString* signError) -> bool {
            return capturedKey->signContent(content, outSignature, signError);
        };

        clearError_(error);
        return true;
#else
        (void)hostName;
        (void)outCredential;
        setError_(error, "Self-signed credential generation is only implemented on Windows");
        return false;
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
    struct KeyState_ {
        BCRYPT_ALG_HANDLE algorithm;
        BCRYPT_KEY_HANDLE key;

        KeyState_() : algorithm(nullptr), key(nullptr) {}
        ~KeyState_() {
            if (key) {
                BCryptDestroyKey(key);
            }
            if (algorithm) {
                BCryptCloseAlgorithmProvider(algorithm, 0);
            }
        }

        bool generate(SwString* error) {
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM,
                                            nullptr, 0) != 0) {
                setError_(error, "BCryptOpenAlgorithmProvider(ECDSA P-256) failed");
                return false;
            }
            if (BCryptGenerateKeyPair(algorithm, &key, 256, 0) != 0) {
                setError_(error, "BCryptGenerateKeyPair(ECDSA P-256) failed");
                return false;
            }
            if (BCryptFinalizeKeyPair(key, 0) != 0) {
                setError_(error, "BCryptFinalizeKeyPair failed");
                return false;
            }
            return true;
        }

        bool exportPublicPoint(SwByteArray& outX, SwByteArray& outY, SwString* error) {
            ULONG size = 0;
            if (BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &size, 0) != 0) {
                setError_(error, "BCryptExportKey(size) failed");
                return false;
            }
            std::vector<unsigned char> blob(size, 0);
            if (BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), size, &size, 0) != 0) {
                setError_(error, "BCryptExportKey failed");
                return false;
            }
            // BCRYPT_ECCKEY_BLOB { ULONG dwMagic; ULONG cbKey; } X[cbKey] Y[cbKey].
            if (blob.size() < sizeof(BCRYPT_ECCKEY_BLOB)) {
                setError_(error, "ECDSA public blob is too small");
                return false;
            }
            const BCRYPT_ECCKEY_BLOB* header =
                reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(blob.data());
            const ULONG cbKey = header->cbKey;
            if (cbKey != 32 || blob.size() < sizeof(BCRYPT_ECCKEY_BLOB) + 2 * cbKey) {
                setError_(error, "Unexpected ECDSA P-256 public key size");
                return false;
            }
            const unsigned char* coordinates = blob.data() + sizeof(BCRYPT_ECCKEY_BLOB);
            outX = SwByteArray(reinterpret_cast<const char*>(coordinates), cbKey);
            outY = SwByteArray(reinterpret_cast<const char*>(coordinates + cbKey), cbKey);
            return true;
        }

        bool signContent(const SwByteArray& content, SwByteArray& outSignature, SwString* error) {
            SwByteArray digest;
            if (!sha256_(content, digest, error)) {
                return false;
            }

            ULONG size = 0;
            if (BCryptSignHash(key, nullptr,
                               reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
                               static_cast<ULONG>(digest.size()),
                               nullptr, 0, &size, 0) != 0) {
                setError_(error, "BCryptSignHash(size) failed");
                return false;
            }
            std::vector<unsigned char> raw(size, 0);
            if (BCryptSignHash(key, nullptr,
                               reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
                               static_cast<ULONG>(digest.size()),
                               raw.data(), size, &size, 0) != 0) {
                setError_(error, "BCryptSignHash failed");
                return false;
            }
            // CNG returns raw r||s; TLS carries the DER SEQUENCE{INTEGER r, INTEGER s}.
            if (raw.size() != 64) {
                setError_(error, "Unexpected ECDSA signature size");
                return false;
            }
            const SwByteArray r(reinterpret_cast<const char*>(raw.data()), 32);
            const SwByteArray s(reinterpret_cast<const char*>(raw.data() + 32), 32);
            outSignature = derEcdsaSignature_(r, s);
            return true;
        }

        // ECDSA-Sig-Value ::= SEQUENCE { r INTEGER, s INTEGER }, DER encoded.
        static SwByteArray derEcdsaSignature_(const SwByteArray& r, const SwByteArray& s) {
            SwByteArray body;
            body.append(derInteger_(r));
            body.append(derInteger_(s));
            SwByteArray out;
            out.append(static_cast<char>(0x30));
            out.append(derLength_(body.size()));
            out.append(body);
            return out;
        }

        static SwByteArray derLength_(std::size_t length) {
            SwByteArray out;
            if (length < 0x80) {
                out.append(static_cast<char>(length));
                return out;
            }
            SwByteArray lengthBytes;
            std::size_t value = length;
            while (value > 0) {
                lengthBytes.append(static_cast<char>(value & 0xffU));
                value >>= 8;
            }
            out.append(static_cast<char>(0x80U | lengthBytes.size()));
            for (std::size_t i = lengthBytes.size(); i > 0; --i) {
                out.append(lengthBytes[i - 1]);
            }
            return out;
        }

        static SwByteArray derInteger_(const SwByteArray& magnitude) {
            std::size_t start = 0;
            while (start + 1 < static_cast<std::size_t>(magnitude.size()) &&
                   static_cast<std::uint8_t>(magnitude[start]) == 0) {
                ++start;
            }
            SwByteArray value = magnitude.mid(static_cast<int>(start),
                                              static_cast<int>(magnitude.size() - start));
            SwByteArray body;
            if (!value.isEmpty() && (static_cast<std::uint8_t>(value[0]) & 0x80U) != 0) {
                body.append(static_cast<char>(0));
            }
            body.append(value);
            SwByteArray out;
            out.append(static_cast<char>(0x02));
            out.append(derLength_(body.size()));
            out.append(body);
            return out;
        }

        static bool sha256_(const SwByteArray& data, SwByteArray& outDigest, SwString* error) {
            BCRYPT_ALG_HANDLE provider = nullptr;
            if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
                setError_(error, "BCryptOpenAlgorithmProvider(SHA256) failed");
                return false;
            }
            std::vector<unsigned char> digest(32, 0);
            const NTSTATUS status = BCryptHash(provider, nullptr, 0,
                                               reinterpret_cast<PUCHAR>(
                                                   const_cast<char*>(data.constData())),
                                               static_cast<ULONG>(data.size()),
                                               digest.data(), 32);
            BCryptCloseAlgorithmProvider(provider, 0);
            if (status != 0) {
                setError_(error, "BCryptHash(SHA256) failed");
                return false;
            }
            outDigest = SwByteArray(reinterpret_cast<const char*>(digest.data()), digest.size());
            return true;
        }
    };

    // ---- minimal DER helpers ------------------------------------------------

    static SwByteArray derLength_(std::size_t length) {
        SwByteArray out;
        if (length < 0x80) {
            out.append(static_cast<char>(length));
            return out;
        }
        SwByteArray lengthBytes;
        std::size_t value = length;
        while (value > 0) {
            lengthBytes.append(static_cast<char>(value & 0xffU));
            value >>= 8;
        }
        out.append(static_cast<char>(0x80U | lengthBytes.size()));
        for (std::size_t i = lengthBytes.size(); i > 0; --i) {
            out.append(lengthBytes[i - 1]);
        }
        return out;
    }

    static SwByteArray derTlv_(std::uint8_t tag, const SwByteArray& value) {
        SwByteArray out;
        out.append(static_cast<char>(tag));
        out.append(derLength_(value.size()));
        out.append(value);
        return out;
    }

    // DER INTEGER from a big-endian magnitude, adding a leading zero when the
    // high bit is set so the value is not read as negative.
    static SwByteArray derInteger_(const SwByteArray& magnitude) {
        SwByteArray trimmed = magnitude;
        std::size_t start = 0;
        while (start + 1 < static_cast<std::size_t>(trimmed.size()) &&
               static_cast<std::uint8_t>(trimmed[start]) == 0) {
            ++start;
        }
        SwByteArray value = trimmed.mid(static_cast<int>(start),
                                        static_cast<int>(trimmed.size() - start));
        SwByteArray body;
        if (!value.isEmpty() && (static_cast<std::uint8_t>(value[0]) & 0x80U) != 0) {
            body.append(static_cast<char>(0));
        }
        body.append(value);
        return derTlv_(0x02, body);
    }

    static SwByteArray derOid_(const unsigned char* bytes, std::size_t length) {
        SwByteArray value(reinterpret_cast<const char*>(bytes), length);
        return derTlv_(0x06, value);
    }

    static bool buildCertificate_(const SwString& hostName,
                                  const SwByteArray& publicX,
                                  const SwByteArray& publicY,
                                  SwByteArray& outCertificate,
                                  SwString* error) {
        // OIDs.
        static const unsigned char oidEcdsaWithSha256[] =
            {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02};
        static const unsigned char oidEcPublicKey[] =
            {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
        static const unsigned char oidPrime256v1[] =
            {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
        static const unsigned char oidCommonName[] = {0x55, 0x04, 0x03};

        const SwByteArray sigAlgId =
            derTlv_(0x30, derOid_(oidEcdsaWithSha256, sizeof(oidEcdsaWithSha256)));

        // SubjectPublicKeyInfo.
        SwByteArray spkiAlg;
        spkiAlg.append(derOid_(oidEcPublicKey, sizeof(oidEcPublicKey)));
        spkiAlg.append(derOid_(oidPrime256v1, sizeof(oidPrime256v1)));
        const SwByteArray spkiAlgId = derTlv_(0x30, spkiAlg);

        SwByteArray point;
        point.append(static_cast<char>(0x04)); // uncompressed
        point.append(publicX);
        point.append(publicY);
        SwByteArray publicKeyBitString;
        publicKeyBitString.append(static_cast<char>(0)); // unused bits
        publicKeyBitString.append(point);
        SwByteArray spki;
        spki.append(spkiAlgId);
        spki.append(derTlv_(0x03, publicKeyBitString));
        const SwByteArray subjectPublicKeyInfo = derTlv_(0x30, spki);

        // Name = SEQUENCE { SET { SEQUENCE { OID cn, UTF8String host } } }.
        SwByteArray attribute;
        attribute.append(derOid_(oidCommonName, sizeof(oidCommonName)));
        attribute.append(derTlv_(0x0c, SwByteArray(hostName.toStdString())));
        const SwByteArray rdn = derTlv_(0x31, derTlv_(0x30, attribute));
        const SwByteArray name = derTlv_(0x30, rdn);

        // Validity: fixed wide window (UTCTime YYMMDDHHMMSSZ).
        SwByteArray validity;
        validity.append(derTlv_(0x17, SwByteArray("200101000000Z")));
        validity.append(derTlv_(0x17, SwByteArray("400101000000Z")));
        const SwByteArray validitySeq = derTlv_(0x30, validity);

        // version [0] EXPLICIT INTEGER v3(2).
        const SwByteArray version =
            derTlv_(0xA0, derInteger_(SwByteArray(std::string(1, '\x02'))));
        const SwByteArray serial = derInteger_(SwByteArray(std::string(1, '\x01')));

        SwByteArray tbs;
        tbs.append(version);
        tbs.append(serial);
        tbs.append(sigAlgId);
        tbs.append(name);          // issuer
        tbs.append(validitySeq);
        tbs.append(name);          // subject (self-signed)
        tbs.append(subjectPublicKeyInfo);
        const SwByteArray tbsCertificate = derTlv_(0x30, tbs);

        // Placeholder signature value (the chain is not validated by the test
        // peer; the CertificateVerify signature is what proves possession).
        SwByteArray placeholderSig;
        placeholderSig.append(derInteger_(SwByteArray(std::string(1, '\x01'))));
        placeholderSig.append(derInteger_(SwByteArray(std::string(1, '\x01'))));
        SwByteArray sigBitString;
        sigBitString.append(static_cast<char>(0));
        sigBitString.append(derTlv_(0x30, placeholderSig));

        SwByteArray certificate;
        certificate.append(tbsCertificate);
        certificate.append(sigAlgId);
        certificate.append(derTlv_(0x03, sigBitString));
        outCertificate = derTlv_(0x30, certificate);

        clearError_(error);
        return true;
    }
#endif
};

#endif
