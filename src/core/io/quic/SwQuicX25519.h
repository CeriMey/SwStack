#ifndef SWQUICX25519_H
#define SWQUICX25519_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstdint>
#include <cstddef>

// X25519 elliptic-curve Diffie-Hellman over Curve25519 (RFC 7748 section 5).
//
// All inputs and outputs are exactly 32 little-endian bytes. The field
// arithmetic uses a 16-limb representation (each limb holds ~16 bits of the
// 255-bit value) and a constant-time Montgomery ladder, following the classic
// reference implementation. scalarMult() performs clamping on the scalar as
// mandated by RFC 7748.
class SwQuicX25519 {
public:
    static bool scalarMult(const SwByteArray& scalar32,
                           const SwByteArray& uCoordinate32,
                           SwByteArray& out32,
                           SwString* error = nullptr) {
        if (scalar32.size() != 32) {
            setError_(error, "X25519 scalar must be 32 bytes");
            return false;
        }
        if (uCoordinate32.size() != 32) {
            setError_(error, "X25519 u-coordinate must be 32 bytes");
            return false;
        }

        std::uint8_t scalar[32];
        std::uint8_t point[32];
        std::uint8_t result[32];
        for (std::size_t i = 0; i < 32; ++i) {
            scalar[i] = static_cast<std::uint8_t>(scalar32.constData()[i]);
            point[i] = static_cast<std::uint8_t>(uCoordinate32.constData()[i]);
        }

        scalarMultRaw_(result, scalar, point);

        SwByteArray out(32, '\0');
        for (std::size_t i = 0; i < 32; ++i) {
            out[i] = static_cast<char>(result[i]);
        }
        out32 = out;

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool derivePublicKey(const SwByteArray& privateScalar32,
                                SwByteArray& outPublic32,
                                SwString* error = nullptr) {
        if (privateScalar32.size() != 32) {
            setError_(error, "X25519 private scalar must be 32 bytes");
            return false;
        }

        // Curve25519 base point: u = 9, encoded little-endian.
        SwByteArray basePoint(32, '\0');
        basePoint[0] = static_cast<char>(9);
        return scalarMult(privateScalar32, basePoint, outPublic32, error);
    }

    static bool computeSharedSecret(const SwByteArray& privateScalar32,
                                    const SwByteArray& peerPublic32,
                                    SwByteArray& outSecret32,
                                    SwString* error = nullptr) {
        if (privateScalar32.size() != 32) {
            setError_(error, "X25519 private scalar must be 32 bytes");
            return false;
        }
        if (peerPublic32.size() != 32) {
            setError_(error, "X25519 peer public key must be 32 bytes");
            return false;
        }
        if (!scalarMult(privateScalar32, peerPublic32, outSecret32, error)) {
            return false;
        }
        // RFC 8446 7.4.2 / RFC 7748 6.1: reject an all-zero shared secret (a
        // low-order peer public key), which would make the whole key schedule
        // attacker-predictable. Constant-time OR of all output bytes.
        std::uint8_t accumulator = 0;
        for (int i = 0; i < 32; ++i) {
            accumulator = static_cast<std::uint8_t>(
                accumulator | static_cast<std::uint8_t>(outSecret32[i]));
        }
        if (accumulator == 0) {
            setError_(error, "X25519 shared secret is all-zero (low-order peer public key)");
            return false;
        }
        return true;
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    // --- Field arithmetic mod p = 2^255 - 19, 16-limb representation. -------

    static void set0_(std::int64_t* o) {
        for (int i = 0; i < 16; ++i) {
            o[i] = 0;
        }
    }

    static void copy_(std::int64_t* o, const std::int64_t* a) {
        for (int i = 0; i < 16; ++i) {
            o[i] = a[i];
        }
    }

    static void unpack25519_(std::int64_t* o, const std::uint8_t* n) {
        for (int i = 0; i < 16; ++i) {
            o[i] = static_cast<std::int64_t>(n[2 * i]) +
                   (static_cast<std::int64_t>(n[2 * i + 1]) << 8);
        }
        o[15] &= 0x7fff;
    }

    static void car25519_(std::int64_t* o) {
        for (int i = 0; i < 16; ++i) {
            o[i] += (static_cast<std::int64_t>(1) << 16);
            const std::int64_t c = o[i] >> 16;
            o[(i + 1) * (i < 15)] +=
                c - 1 + 37 * (c - 1) * (i == 15);
            o[i] -= c << 16;
        }
    }

    // Constant-time conditional swap of p and q when b == 1.
    static void sel25519_(std::int64_t* p, std::int64_t* q, int b) {
        const std::int64_t c = ~(static_cast<std::int64_t>(b) - 1);
        for (int i = 0; i < 16; ++i) {
            const std::int64_t t = c & (p[i] ^ q[i]);
            p[i] ^= t;
            q[i] ^= t;
        }
    }

    static void pack25519_(std::uint8_t* o, const std::int64_t* n) {
        std::int64_t m[16];
        std::int64_t t[16];
        copy_(t, n);
        car25519_(t);
        car25519_(t);
        car25519_(t);
        for (int j = 0; j < 2; ++j) {
            m[0] = t[0] - 0xffed;
            for (int i = 1; i < 15; ++i) {
                m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
                m[i - 1] &= 0xffff;
            }
            m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
            const int b = static_cast<int>((m[15] >> 16) & 1);
            m[14] &= 0xffff;
            sel25519_(t, m, 1 - b);
        }
        for (int i = 0; i < 16; ++i) {
            o[2 * i] = static_cast<std::uint8_t>(t[i] & 0xff);
            o[2 * i + 1] = static_cast<std::uint8_t>((t[i] >> 8) & 0xff);
        }
    }

    static void add_(std::int64_t* o, const std::int64_t* a, const std::int64_t* b) {
        for (int i = 0; i < 16; ++i) {
            o[i] = a[i] + b[i];
        }
    }

    static void sub_(std::int64_t* o, const std::int64_t* a, const std::int64_t* b) {
        for (int i = 0; i < 16; ++i) {
            o[i] = a[i] - b[i];
        }
    }

    static void mul_(std::int64_t* o, const std::int64_t* a, const std::int64_t* b) {
        std::int64_t t[31];
        for (int i = 0; i < 31; ++i) {
            t[i] = 0;
        }
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 16; ++j) {
                t[i + j] += a[i] * b[j];
            }
        }
        for (int i = 0; i < 15; ++i) {
            t[i] += 38 * t[i + 16];
        }
        for (int i = 0; i < 16; ++i) {
            o[i] = t[i];
        }
        car25519_(o);
        car25519_(o);
    }

    static void sq_(std::int64_t* o, const std::int64_t* a) {
        mul_(o, a, a);
    }

    static void inv25519_(std::int64_t* o, const std::int64_t* i) {
        std::int64_t c[16];
        copy_(c, i);
        for (int a = 253; a >= 0; --a) {
            sq_(c, c);
            if (a != 2 && a != 4) {
                mul_(c, c, i);
            }
        }
        copy_(o, c);
    }

    static void scalarMultRaw_(std::uint8_t* q,
                               const std::uint8_t* n,
                               const std::uint8_t* p) {
        static const std::int64_t k121665[16] = {
            0xdb41, 1, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0
        };

        std::uint8_t z[32];
        for (int i = 0; i < 31; ++i) {
            z[i] = n[i];
        }
        // Clamping per RFC 7748.
        z[31] = static_cast<std::uint8_t>((n[31] & 127) | 64);
        z[0] = static_cast<std::uint8_t>(z[0] & 248);

        std::int64_t x[16];
        unpack25519_(x, p);

        std::int64_t a[16];
        std::int64_t b[16];
        std::int64_t c[16];
        std::int64_t d[16];
        std::int64_t e[16];
        std::int64_t f[16];
        for (int i = 0; i < 16; ++i) {
            b[i] = x[i];
            a[i] = 0;
            c[i] = 0;
            d[i] = 0;
        }
        a[0] = 1;
        d[0] = 1;

        for (int i = 254; i >= 0; --i) {
            const int r = (z[i >> 3] >> (i & 7)) & 1;
            sel25519_(a, b, r);
            sel25519_(c, d, r);
            add_(e, a, c);
            sub_(a, a, c);
            add_(c, b, d);
            sub_(b, b, d);
            sq_(d, e);
            sq_(f, a);
            mul_(a, c, a);
            mul_(c, b, e);
            add_(e, a, c);
            sub_(a, a, c);
            sq_(b, a);
            sub_(c, d, f);
            mul_(a, c, k121665);
            add_(a, a, d);
            mul_(c, c, a);
            mul_(a, d, f);
            mul_(d, b, x);
            sq_(b, e);
            sel25519_(a, b, r);
            sel25519_(c, d, r);
        }

        // Compute x-coordinate = a / c.
        std::int64_t cInv[16];
        inv25519_(cInv, c);
        std::int64_t xr[16];
        mul_(xr, a, cInv);
        pack25519_(q, xr);
    }
};

#endif
