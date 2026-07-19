#ifndef SWQUICMLKEM768_H
#define SWQUICMLKEM768_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicRandom.h"

#include "mlkem_native.h"

#include <cstddef>
#include <cstdint>

// Portable ML-KEM-768 facade for the TLS X25519MLKEM768 key exchange. The
// underlying implementation is mlkem-native v1.2.0, built once for every
// SwStack consumer. Randomness crosses this boundary explicitly so the
// upstream library never owns a platform RNG.
class SwQuicMlKem768 {
public:
    static std::size_t publicKeySize() { return MLKEM768_PUBLICKEYBYTES; }
    static std::size_t privateKeySize() { return MLKEM768_SECRETKEYBYTES; }
    static std::size_t ciphertextSize() { return MLKEM768_CIPHERTEXTBYTES; }
    static std::size_t sharedSecretSize() { return MLKEM768_BYTES; }
    static std::size_t keyGenerationSeedSize() { return 2U * MLKEM768_SYMBYTES; }
    static std::size_t encapsulationSeedSize() { return MLKEM768_SYMBYTES; }

    static bool keyPair(SwByteArray& publicKey,
                        SwByteArray& privateKey,
                        SwString* error = nullptr) {
        SwByteArray seed;
        if (!SwQuicRandom::fill(seed, keyGenerationSeedSize(), error)) {
            publicKey.clear();
            privateKey.secureClear();
            return false;
        }
        const bool ok = keyPairDeterministic(seed, publicKey, privateKey, error);
        seed.secureClear();
        return ok;
    }

    static bool keyPairDeterministic(const SwByteArray& seed,
                                     SwByteArray& publicKey,
                                     SwByteArray& privateKey,
                                     SwString* error = nullptr) {
        if (seed.size() != keyGenerationSeedSize()) {
            publicKey.clear();
            privateKey.secureClear();
            return fail_(error, "ML-KEM-768 key generation seed must be 64 bytes");
        }

        SwByteArray candidatePublic(publicKeySize(), '\0');
        SwByteArray candidatePrivate(privateKeySize(), '\0');
        const int result = crypto_kem_keypair_derand(
            bytes_(candidatePublic), bytes_(candidatePrivate), constBytes_(seed));
        if (result != 0) {
            candidatePrivate.secureClear();
            publicKey.clear();
            privateKey.secureClear();
            return fail_(error, "ML-KEM-768 key generation failed");
        }

        publicKey = candidatePublic;
        privateKey.secureClear();
        privateKey = candidatePrivate;
        candidatePrivate.secureClear();
        clearError_(error);
        return true;
    }

    static bool encapsulate(const SwByteArray& publicKey,
                            SwByteArray& ciphertext,
                            SwByteArray& sharedSecret,
                            SwString* error = nullptr) {
        SwByteArray seed;
        if (!SwQuicRandom::fill(seed, encapsulationSeedSize(), error)) {
            ciphertext.clear();
            sharedSecret.secureClear();
            return false;
        }
        const bool ok = encapsulateDeterministic(
            publicKey, seed, ciphertext, sharedSecret, error);
        seed.secureClear();
        return ok;
    }

    static bool encapsulateDeterministic(const SwByteArray& publicKey,
                                         const SwByteArray& seed,
                                         SwByteArray& ciphertext,
                                         SwByteArray& sharedSecret,
                                         SwString* error = nullptr) {
        if (publicKey.size() != publicKeySize()) {
            ciphertext.clear();
            sharedSecret.secureClear();
            return fail_(error, "ML-KEM-768 public key must be 1184 bytes");
        }
        if (seed.size() != encapsulationSeedSize()) {
            ciphertext.clear();
            sharedSecret.secureClear();
            return fail_(error, "ML-KEM-768 encapsulation seed must be 32 bytes");
        }

        SwByteArray candidateCiphertext(ciphertextSize(), '\0');
        SwByteArray candidateSecret(sharedSecretSize(), '\0');
        const int result = crypto_kem_enc_derand(
            bytes_(candidateCiphertext), bytes_(candidateSecret),
            constBytes_(publicKey), constBytes_(seed));
        if (result != 0) {
            candidateSecret.secureClear();
            ciphertext.clear();
            sharedSecret.secureClear();
            return fail_(error, "ML-KEM-768 public key validation or encapsulation failed");
        }

        ciphertext = candidateCiphertext;
        sharedSecret.secureClear();
        sharedSecret = candidateSecret;
        candidateSecret.secureClear();
        clearError_(error);
        return true;
    }

    static bool decapsulate(const SwByteArray& privateKey,
                            const SwByteArray& ciphertext,
                            SwByteArray& sharedSecret,
                            SwString* error = nullptr) {
        if (privateKey.size() != privateKeySize()) {
            sharedSecret.secureClear();
            return fail_(error, "ML-KEM-768 private key must be 2400 bytes");
        }
        if (ciphertext.size() != ciphertextSize()) {
            sharedSecret.secureClear();
            return fail_(error, "ML-KEM-768 ciphertext must be 1088 bytes");
        }

        SwByteArray candidateSecret(sharedSecretSize(), '\0');
        const int result = crypto_kem_dec(bytes_(candidateSecret),
                                          constBytes_(ciphertext),
                                          constBytes_(privateKey));
        if (result != 0) {
            candidateSecret.secureClear();
            sharedSecret.secureClear();
            return fail_(error, "ML-KEM-768 secret-key validation or decapsulation failed");
        }

        sharedSecret.secureClear();
        sharedSecret = candidateSecret;
        candidateSecret.secureClear();
        clearError_(error);
        return true;
    }

    static bool validatePublicKey(const SwByteArray& publicKey,
                                  SwString* error = nullptr) {
        if (publicKey.size() != publicKeySize()) {
            return fail_(error, "ML-KEM-768 public key must be 1184 bytes");
        }
        if (crypto_kem_check_pk(constBytes_(publicKey)) != 0) {
            return fail_(error, "ML-KEM-768 public key modulus check failed");
        }
        clearError_(error);
        return true;
    }

private:
    static std::uint8_t* bytes_(SwByteArray& value) {
        return reinterpret_cast<std::uint8_t*>(value.data());
    }

    static const std::uint8_t* constBytes_(const SwByteArray& value) {
        return reinterpret_cast<const std::uint8_t*>(value.constData());
    }

    static bool fail_(SwString* error, const char* message) {
        if (error) *error = SwString(message);
        return false;
    }

    static void clearError_(SwString* error) {
        if (error) *error = SwString();
    }
};

#endif
