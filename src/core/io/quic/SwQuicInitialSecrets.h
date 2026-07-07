#ifndef SWQUICINITIALSECRETS_H
#define SWQUICINITIALSECRETS_H

#include "SwByteArray.h"
#include "SwCrypto.h"
#include "SwString.h"
#include "quic/SwQuicConnectionId.h"

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <vector>

struct SwQuicInitialKeys {
    SwByteArray secret;
    SwByteArray key;
    SwByteArray iv;
    SwByteArray headerProtectionKey;
};

class SwQuicInitialSecrets {
public:
    static bool deriveV1(const SwQuicConnectionId& destinationConnectionId,
                         SwQuicInitialKeys& outClientKeys,
                         SwQuicInitialKeys& outServerKeys,
                         SwString* error = nullptr) {
        try {
            const SwByteArray salt = SwByteArray::fromHex(
                SwByteArray("38762cf7f55934b34d179ae6a4c80cadccbb7f0a"));

            SwByteArray initialSecret;
            if (!hkdfExtract_(salt, destinationConnectionId.bytes(), initialSecret, error)) {
                return false;
            }

            if (!hkdfExpandLabel(initialSecret, SwString("client in"), 32, outClientKeys.secret, error) ||
                !hkdfExpandLabel(initialSecret, SwString("server in"), 32, outServerKeys.secret, error) ||
                !deriveKeySet_(outClientKeys.secret, outClientKeys, error) ||
                !deriveKeySet_(outServerKeys.secret, outServerKeys, error)) {
                return false;
            }
        } catch (const std::exception& ex) {
            setError_(error, ex.what());
            return false;
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool hkdfExpandLabel(const SwByteArray& secret,
                                const SwString& label,
                                std::size_t outputLength,
                                SwByteArray& outBytes,
                                SwString* error = nullptr) {
        if (outputLength > 255 * 32) {
            setError_(error, "HKDF output length is too large");
            return false;
        }

        const SwString fullLabel = SwString("tls13 ") + label;
        if (fullLabel.size() > 255) {
            setError_(error, "HKDF label is too long");
            return false;
        }

        SwByteArray info;
        appendU16_(info, static_cast<std::uint16_t>(outputLength));
        info.append(static_cast<char>(fullLabel.size()));
        info.append(fullLabel.toStdString());
        info.append(static_cast<char>(0));

        return hkdfExpand_(secret, info, outputLength, outBytes, error);
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static SwByteArray hmacSha256_(const SwByteArray& key, const SwByteArray& data) {
        const std::vector<unsigned char> digest =
            SwCrypto::generateKeyedHashSHA256(data.toStdString(), key.toStdString());
        return SwByteArray(reinterpret_cast<const char*>(digest.data()), digest.size());
    }

    static void appendU16_(SwByteArray& out, std::uint16_t value) {
        out.append(static_cast<char>((value >> 8) & 0xffU));
        out.append(static_cast<char>(value & 0xffU));
    }

    static bool hkdfExtract_(const SwByteArray& salt,
                             const SwByteArray& inputKeyMaterial,
                             SwByteArray& outSecret,
                             SwString* error) {
        try {
            outSecret = hmacSha256_(salt, inputKeyMaterial);
        } catch (const std::exception& ex) {
            setError_(error, ex.what());
            return false;
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool hkdfExpand_(const SwByteArray& pseudoRandomKey,
                            const SwByteArray& info,
                            std::size_t outputLength,
                            SwByteArray& outBytes,
                            SwString* error) {
        if (outputLength > 255 * 32) {
            setError_(error, "HKDF output length is too large");
            return false;
        }

        try {
            outBytes.clear();
            SwByteArray previous;
            std::uint8_t counter = 1;

            while (outBytes.size() < outputLength) {
                SwByteArray input;
                input.append(previous);
                input.append(info);
                input.append(static_cast<char>(counter));

                previous = hmacSha256_(pseudoRandomKey, input);
                const std::size_t remaining = outputLength - outBytes.size();
                const std::size_t toCopy = std::min<std::size_t>(remaining, previous.size());
                outBytes.append(previous.constData(), toCopy);
                ++counter;
            }
        } catch (const std::exception& ex) {
            setError_(error, ex.what());
            return false;
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool deriveKeySet_(const SwByteArray& secret,
                              SwQuicInitialKeys& outKeys,
                              SwString* error) {
        outKeys.secret = secret;
        return hkdfExpandLabel(secret, SwString("quic key"), 16, outKeys.key, error) &&
               hkdfExpandLabel(secret, SwString("quic iv"), 12, outKeys.iv, error) &&
               hkdfExpandLabel(secret, SwString("quic hp"), 16, outKeys.headerProtectionKey, error);
    }
};

#endif
