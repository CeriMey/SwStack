#ifndef SWQUICPACKETKEYS_H
#define SWQUICPACKETKEYS_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicInitialSecrets.h"

// Derives a QUIC packet-protection key set (key, iv, header-protection key) from
// a TLS 1.3 traffic secret, for cipher suite TLS_AES_128_GCM_SHA256 (RFC 9001
// section 5.1). The Initial keys use exactly this derivation from the Initial
// secret; the Handshake and 1-RTT levels use it from the TLS handshake and
// application traffic secrets respectively.
class SwQuicPacketKeys {
public:
    static bool deriveAes128(const SwByteArray& trafficSecret,
                             SwQuicInitialKeys& outKeys,
                             SwString* error = nullptr) {
        outKeys.secret = trafficSecret;
        if (!SwQuicInitialSecrets::hkdfExpandLabel(trafficSecret, SwString("quic key"), 16,
                                                   outKeys.key, error) ||
            !SwQuicInitialSecrets::hkdfExpandLabel(trafficSecret, SwString("quic iv"), 12,
                                                   outKeys.iv, error) ||
            !SwQuicInitialSecrets::hkdfExpandLabel(trafficSecret, SwString("quic hp"), 16,
                                                   outKeys.headerProtectionKey, error)) {
            return false;
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    // Key update for 1-RTT (RFC 9001 section 6.1): next generation secret =
    // HKDF-Expand-Label(secret, "quic ku", "", Hash.length).
    static bool nextGeneration(const SwByteArray& trafficSecret,
                               SwByteArray& outNextSecret,
                               SwString* error = nullptr) {
        return SwQuicInitialSecrets::hkdfExpandLabel(trafficSecret, SwString("quic ku"), 32,
                                                     outNextSecret, error);
    }
};

#endif
