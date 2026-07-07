#ifndef SWQUICRANDOM_H
#define SWQUICRANDOM_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#include "SwCrypto.h"   // brings in <windows.h> + <bcrypt.h>
#else
#include <random>
#endif

// Cryptographically strong random bytes, used for QUIC connection IDs, the
// ephemeral X25519 private scalar and the TLS 1.3 ClientHello random. On Windows
// this is BCryptGenRandom with the system-preferred RNG; elsewhere it falls back
// to std::random_device.
class SwQuicRandom {
public:
    static bool fill(SwByteArray& out, std::size_t length, SwString* error = nullptr) {
        SwByteArray buffer(length, '\0');
        if (length == 0) {
            out = buffer;
            clearError_(error);
            return true;
        }

#if defined(_WIN32)
        const NTSTATUS status = BCryptGenRandom(nullptr,
                                                reinterpret_cast<PUCHAR>(buffer.data()),
                                                static_cast<ULONG>(length),
                                                BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status != 0) {
            setError_(error, "BCryptGenRandom failed");
            return false;
        }
#else
        std::random_device device;
        std::size_t produced = 0;
        while (produced < length) {
            const std::uint32_t value = device();
            for (int i = 0; i < 4 && produced < length; ++i, ++produced) {
                buffer[produced] = static_cast<char>((value >> (i * 8)) & 0xffU);
            }
        }
#endif

        out = buffer;
        clearError_(error);
        return true;
    }

    static SwByteArray bytes(std::size_t length) {
        SwByteArray out;
        fill(out, length);
        return out;
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
};

#endif
