#ifndef SWQUICHYBRIDKEX_H
#define SWQUICHYBRIDKEX_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstddef>
#include <cstdint>

// Wire-format helpers for the TLS X25519MLKEM768 NamedGroup. The ordering is
// defined by draft-ietf-tls-ecdhe-mlkem: ML-KEM material first, X25519 second.
class SwQuicHybridKex {
public:
    static std::uint16_t x25519Group() { return 0x001d; }
    static std::uint16_t x25519MlKem768Group() { return 0x11ec; }

    static std::size_t mlKemPublicKeySize() { return 1184; }
    static std::size_t mlKemCiphertextSize() { return 1088; }
    static std::size_t x25519PublicKeySize() { return 32; }
    static std::size_t componentSecretSize() { return 32; }
    static std::size_t clientKeyShareSize() { return 1216; }
    static std::size_t serverKeyShareSize() { return 1120; }
    static std::size_t combinedSecretSize() { return 64; }

    static bool buildClientKeyShare(const SwByteArray& mlKemPublicKey,
                                    const SwByteArray& x25519PublicKey,
                                    SwByteArray& out,
                                    SwString* error = nullptr) {
        if (mlKemPublicKey.size() != mlKemPublicKeySize() ||
            x25519PublicKey.size() != x25519PublicKeySize()) {
            out.clear();
            return fail_(error, "Invalid X25519MLKEM768 client key-share component size");
        }
        out = mlKemPublicKey;
        out.append(x25519PublicKey);
        clearError_(error);
        return true;
    }

    static bool splitClientKeyShare(const SwByteArray& share,
                                    SwByteArray& mlKemPublicKey,
                                    SwByteArray& x25519PublicKey,
                                    SwString* error = nullptr) {
        if (share.size() != clientKeyShareSize()) {
            mlKemPublicKey.clear();
            x25519PublicKey.clear();
            return fail_(error, "X25519MLKEM768 client key share must be 1216 bytes");
        }
        mlKemPublicKey = share.mid(0, static_cast<int>(mlKemPublicKeySize()));
        x25519PublicKey = share.mid(static_cast<int>(mlKemPublicKeySize()),
                                   static_cast<int>(x25519PublicKeySize()));
        clearError_(error);
        return true;
    }

    static bool buildServerKeyShare(const SwByteArray& mlKemCiphertext,
                                    const SwByteArray& x25519PublicKey,
                                    SwByteArray& out,
                                    SwString* error = nullptr) {
        if (mlKemCiphertext.size() != mlKemCiphertextSize() ||
            x25519PublicKey.size() != x25519PublicKeySize()) {
            out.clear();
            return fail_(error, "Invalid X25519MLKEM768 server key-share component size");
        }
        out = mlKemCiphertext;
        out.append(x25519PublicKey);
        clearError_(error);
        return true;
    }

    static bool splitServerKeyShare(const SwByteArray& share,
                                    SwByteArray& mlKemCiphertext,
                                    SwByteArray& x25519PublicKey,
                                    SwString* error = nullptr) {
        if (share.size() != serverKeyShareSize()) {
            mlKemCiphertext.clear();
            x25519PublicKey.clear();
            return fail_(error, "X25519MLKEM768 server key share must be 1120 bytes");
        }
        mlKemCiphertext = share.mid(0, static_cast<int>(mlKemCiphertextSize()));
        x25519PublicKey = share.mid(static_cast<int>(mlKemCiphertextSize()),
                                   static_cast<int>(x25519PublicKeySize()));
        clearError_(error);
        return true;
    }

    static bool combineSecrets(const SwByteArray& mlKemSecret,
                               const SwByteArray& x25519Secret,
                               SwByteArray& out,
                               SwString* error = nullptr) {
        if (mlKemSecret.size() != componentSecretSize() ||
            x25519Secret.size() != componentSecretSize()) {
            out.secureClear();
            return fail_(error, "Invalid X25519MLKEM768 component secret size");
        }
        out.secureClear();
        out = mlKemSecret;
        out.append(x25519Secret);
        clearError_(error);
        return true;
    }

private:
    static bool fail_(SwString* error, const char* message) {
        if (error) *error = SwString(message);
        return false;
    }

    static void clearError_(SwString* error) {
        if (error) *error = SwString();
    }
};

#endif
