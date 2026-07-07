#ifndef SWTLS13MESSAGES_H
#define SWTLS13MESSAGES_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// TLS 1.3 handshake message (de)serialization helpers for a QUIC client.
//
// This module only parses/serializes the TLS handshake structures carried in the
// QUIC CRYPTO stream; all HKDF / SHA-256 / HMAC primitives live in
// SwQuicInitialSecrets and SwCrypto and are reused verbatim by callers.
class SwTls13Messages {
public:
    // A single TLS handshake message: 1-byte type + 3-byte length + body.
    // type: 1=ClientHello, 2=ServerHello, 8=EncryptedExtensions,
    //       11=Certificate, 15=CertificateVerify, 20=Finished.
    struct HandshakeMessage {
        std::uint8_t type;
        SwByteArray body;

        HandshakeMessage() : type(0) {}
    };

    // Parsed ServerHello view.
    struct ServerHello {
        std::uint16_t cipherSuite;
        SwByteArray serverX25519Public;
        bool selectedTls13;
        bool isHelloRetryRequest;
        bool selectedPreSharedKey; // pre_shared_key present => PSK accepted

        ServerHello()
            : cipherSuite(0),
              selectedTls13(false),
              isHelloRetryRequest(false),
              selectedPreSharedKey(false) {}
    };

    // Walk a CRYPTO stream splitting it into TLS handshake messages.
    static bool splitMessages(const SwByteArray& cryptoStream,
                              std::vector<HandshakeMessage>& out,
                              SwString* error = nullptr) {
        out.clear();
        const std::size_t total = cryptoStream.size();
        std::size_t pos = 0;
        while (pos < total) {
            if (pos + 4 > total) {
                setError_(error, "TLS handshake message header is truncated");
                return false;
            }
            HandshakeMessage message;
            message.type = readU8_(cryptoStream, pos);
            const std::uint32_t length = readU24_(cryptoStream, pos + 1);
            const std::size_t bodyStart = pos + 4;
            if (bodyStart + length > total) {
                setError_(error, "TLS handshake message body is truncated");
                return false;
            }
            if (length > 0) {
                message.body = cryptoStream.mid(static_cast<int>(bodyStart),
                                                static_cast<int>(length));
            } else {
                message.body = SwByteArray(static_cast<std::size_t>(0), '\0');
            }
            out.push_back(message);
            pos = bodyStart + length;
        }
        clearError_(error);
        return true;
    }

    // Parsed ClientHello view (server side).
    struct ClientHello {
        SwByteArray random;              // 32-byte client random
        SwByteArray legacySessionId;     // echoed verbatim in ServerHello
        SwByteArray clientX25519Public;  // client key_share for group x25519
        SwByteArray transportParameters; // extension 0x0039 body
        SwByteArray serverName;          // SNI host, if present
        std::vector<SwByteArray> alpnProtocols; // offered ALPN protocols
        SwByteArray pskIdentity;         // first pre_shared_key identity (ticket)
        SwByteArray pskBinder;           // first PSK binder
        std::size_t pskBindersTotalLength; // tail bytes to strip for binder calc
        std::vector<std::uint16_t> signatureSchemes; // signature_algorithms (0x000d)
        bool offersAes128GcmSha256;      // cipher suite 0x1301 offered
        bool offersX25519;               // supported group 0x001d offered
        bool hasTransportParameters;
        bool hasAlpn;
        bool hasPreSharedKey;            // pre_shared_key extension present
        bool offersEarlyData;            // early_data extension present

        ClientHello()
            : pskBindersTotalLength(0),
              offersAes128GcmSha256(false),
              offersX25519(false),
              hasTransportParameters(false),
              hasAlpn(false),
              hasPreSharedKey(false),
              offersEarlyData(false) {}
    };

    // Parse a ClientHello body (bytes after the 4-byte handshake header).
    static bool parseClientHello(const SwByteArray& chBody,
                                 ClientHello& out,
                                 SwString* error = nullptr) {
        out = ClientHello();
        const std::size_t total = chBody.size();
        std::size_t pos = 0;

        if (pos + 2 > total) {
            setError_(error, "ClientHello legacy_version is truncated");
            return false;
        }
        pos += 2;

        if (pos + 32 > total) {
            setError_(error, "ClientHello random is truncated");
            return false;
        }
        out.random = chBody.mid(static_cast<int>(pos), 32);
        pos += 32;

        if (pos + 1 > total) {
            setError_(error, "ClientHello legacy_session_id length is truncated");
            return false;
        }
        const std::uint8_t sessionIdLength = readU8_(chBody, pos);
        pos += 1;
        // RFC 8446 4.1.2 caps legacy_session_id at 32 bytes; a longer value
        // would be echoed into an illegal legacy_session_id_echo<0..32>.
        if (sessionIdLength > 32) {
            setError_(error, "ClientHello legacy_session_id exceeds 32 bytes");
            return false;
        }
        if (pos + sessionIdLength > total) {
            setError_(error, "ClientHello legacy_session_id is truncated");
            return false;
        }
        out.legacySessionId = chBody.mid(static_cast<int>(pos),
                                         static_cast<int>(sessionIdLength));
        pos += sessionIdLength;

        if (pos + 2 > total) {
            setError_(error, "ClientHello cipher_suites length is truncated");
            return false;
        }
        const std::uint16_t cipherSuitesLength = readU16_(chBody, pos);
        pos += 2;
        if (pos + cipherSuitesLength > total || (cipherSuitesLength % 2) != 0) {
            setError_(error, "ClientHello cipher_suites list is truncated");
            return false;
        }
        for (std::size_t i = 0; i + 1 < cipherSuitesLength; i += 2) {
            if (readU16_(chBody, pos + i) == 0x1301) {
                out.offersAes128GcmSha256 = true;
            }
        }
        pos += cipherSuitesLength;

        if (pos + 1 > total) {
            setError_(error, "ClientHello legacy_compression_methods length is truncated");
            return false;
        }
        const std::uint8_t compressionLength = readU8_(chBody, pos);
        pos += 1;
        if (pos + compressionLength > total) {
            setError_(error, "ClientHello legacy_compression_methods is truncated");
            return false;
        }
        pos += compressionLength;

        if (pos + 2 > total) {
            setError_(error, "ClientHello extensions length is truncated");
            return false;
        }
        const std::uint16_t extensionsLength = readU16_(chBody, pos);
        pos += 2;
        if (pos + extensionsLength > total) {
            setError_(error, "ClientHello extensions block is truncated");
            return false;
        }
        const std::size_t extensionsEnd = pos + extensionsLength;

        while (pos < extensionsEnd) {
            if (pos + 4 > extensionsEnd) {
                setError_(error, "ClientHello extension header is truncated");
                return false;
            }
            const std::uint16_t extType = readU16_(chBody, pos);
            const std::uint16_t extLen = readU16_(chBody, pos + 2);
            const std::size_t dataStart = pos + 4;
            if (dataStart + extLen > extensionsEnd) {
                setError_(error, "ClientHello extension body is truncated");
                return false;
            }

            if (extType == 0x0000) {
                parseServerNameExtension_(chBody, dataStart, extLen, out.serverName);
            } else if (extType == 0x000a) {
                parseSupportedGroupsExtension_(chBody, dataStart, extLen, out.offersX25519);
            } else if (extType == 0x0010) {
                parseAlpnOfferExtension_(chBody, dataStart, extLen, out.alpnProtocols);
                out.hasAlpn = true;
            } else if (extType == 0x0033) {
                parseClientKeyShareExtension_(chBody, dataStart, extLen, out.clientX25519Public);
            } else if (extType == 0x0039) {
                out.transportParameters =
                    chBody.mid(static_cast<int>(dataStart), static_cast<int>(extLen));
                out.hasTransportParameters = true;
            } else if (extType == 0x000d) {
                parseSignatureAlgorithmsExtension_(chBody, dataStart, extLen,
                                                   out.signatureSchemes);
            } else if (extType == 0x002a) {
                out.offersEarlyData = true;
            } else if (extType == 0x0029) {
                parsePreSharedKeyExtension_(chBody, dataStart, extLen,
                                            out.pskIdentity, out.pskBinder, out.hasPreSharedKey,
                                            out.pskBindersTotalLength);
            }

            pos = dataStart + extLen;
        }

        clearError_(error);
        return true;
    }

    // Parse a ServerHello body (the bytes following the 4-byte handshake header).
    static bool parseServerHello(const SwByteArray& shBody,
                                 ServerHello& out,
                                 SwString* error = nullptr) {
        out = ServerHello();
        const std::size_t total = shBody.size();
        std::size_t pos = 0;

        // legacy_version (2 bytes)
        if (pos + 2 > total) {
            setError_(error, "ServerHello legacy_version is truncated");
            return false;
        }
        pos += 2;

        // random (32 bytes) -- detect HelloRetryRequest via the special random.
        if (pos + 32 > total) {
            setError_(error, "ServerHello random is truncated");
            return false;
        }
        const SwByteArray random = shBody.mid(static_cast<int>(pos), 32);
        out.isHelloRetryRequest = (random == helloRetryRequestRandom_());
        pos += 32;

        // legacy_session_id: opaque<0..32>
        if (pos + 1 > total) {
            setError_(error, "ServerHello legacy_session_id length is truncated");
            return false;
        }
        const std::uint8_t sessionIdLength = readU8_(shBody, pos);
        pos += 1;
        if (pos + sessionIdLength > total) {
            setError_(error, "ServerHello legacy_session_id is truncated");
            return false;
        }
        pos += sessionIdLength;

        // cipher_suite (2 bytes)
        if (pos + 2 > total) {
            setError_(error, "ServerHello cipher_suite is truncated");
            return false;
        }
        out.cipherSuite = readU16_(shBody, pos);
        pos += 2;

        // legacy_compression_method (1 byte)
        if (pos + 1 > total) {
            setError_(error, "ServerHello legacy_compression_method is truncated");
            return false;
        }
        pos += 1;

        // extensions<6..2^16-1>
        if (pos + 2 > total) {
            setError_(error, "ServerHello extensions length is truncated");
            return false;
        }
        const std::uint16_t extensionsLength = readU16_(shBody, pos);
        pos += 2;
        if (pos + extensionsLength > total) {
            setError_(error, "ServerHello extensions block is truncated");
            return false;
        }
        const std::size_t extensionsEnd = pos + extensionsLength;

        while (pos < extensionsEnd) {
            if (pos + 4 > extensionsEnd) {
                setError_(error, "ServerHello extension header is truncated");
                return false;
            }
            const std::uint16_t extType = readU16_(shBody, pos);
            const std::uint16_t extLen = readU16_(shBody, pos + 2);
            const std::size_t dataStart = pos + 4;
            if (dataStart + extLen > extensionsEnd) {
                setError_(error, "ServerHello extension body is truncated");
                return false;
            }

            if (extType == 0x002b) {
                // supported_versions: server picks a single 2-byte version.
                if (extLen < 2) {
                    setError_(error, "ServerHello supported_versions is truncated");
                    return false;
                }
                const std::uint16_t version = readU16_(shBody, dataStart);
                if (version == 0x0304) {
                    out.selectedTls13 = true;
                }
            } else if (extType == 0x0033) {
                // key_share: KeyShareEntry { group(2), key_exchange<1..2^16-1> }.
                if (extLen < 4) {
                    setError_(error, "ServerHello key_share is truncated");
                    return false;
                }
                const std::uint16_t group = readU16_(shBody, dataStart);
                const std::uint16_t keyLen = readU16_(shBody, dataStart + 2);
                if (static_cast<std::size_t>(4) + keyLen > extLen) {
                    setError_(error, "ServerHello key_share key_exchange is truncated");
                    return false;
                }
                if (group == 0x001d) {
                    out.serverX25519Public =
                        shBody.mid(static_cast<int>(dataStart + 4), static_cast<int>(keyLen));
                }
            } else if (extType == 0x0029) {
                // pre_shared_key in ServerHello: the server accepted a PSK.
                out.selectedPreSharedKey = true;
            }

            pos = dataStart + extLen;
        }

        clearError_(error);
        return true;
    }

    // Parsed EncryptedExtensions view: the two extensions a QUIC/HTTP-3
    // client needs are the negotiated ALPN protocol and the peer's QUIC
    // transport parameters (extension 0x0039, RFC 9001 section 8.2).
    struct EncryptedExtensions {
        SwByteArray alpnProtocol;
        SwByteArray transportParameters;
        bool hasAlpn;
        bool hasTransportParameters;
        bool acceptedEarlyData; // early_data (0x2a) present => 0-RTT accepted

        EncryptedExtensions()
            : hasAlpn(false), hasTransportParameters(false), acceptedEarlyData(false) {}
    };

    static bool parseEncryptedExtensions(const SwByteArray& eeBody,
                                         EncryptedExtensions& out,
                                         SwString* error = nullptr) {
        out = EncryptedExtensions();
        const std::size_t total = eeBody.size();
        std::size_t pos = 0;

        if (pos + 2 > total) {
            setError_(error, "EncryptedExtensions length is truncated");
            return false;
        }
        const std::uint16_t extensionsLength = readU16_(eeBody, pos);
        pos += 2;
        if (pos + extensionsLength > total) {
            setError_(error, "EncryptedExtensions block is truncated");
            return false;
        }
        const std::size_t extensionsEnd = pos + extensionsLength;

        while (pos < extensionsEnd) {
            if (pos + 4 > extensionsEnd) {
                setError_(error, "EncryptedExtensions extension header is truncated");
                return false;
            }
            const std::uint16_t extType = readU16_(eeBody, pos);
            const std::uint16_t extLen = readU16_(eeBody, pos + 2);
            const std::size_t dataStart = pos + 4;
            if (dataStart + extLen > extensionsEnd) {
                setError_(error, "EncryptedExtensions extension body is truncated");
                return false;
            }

            if (extType == 0x002a) {
                out.acceptedEarlyData = true; // server accepted 0-RTT
            } else if (extType == 0x0039) {
                out.transportParameters =
                    eeBody.mid(static_cast<int>(dataStart), static_cast<int>(extLen));
                out.hasTransportParameters = true;
            } else if (extType == 0x0010) {
                // ALPN: protocol_name_list<2..2^16-1> of opaque<1..2^8-1>;
                // the server returns exactly one entry.
                if (extLen < 3) {
                    setError_(error, "EncryptedExtensions ALPN is truncated");
                    return false;
                }
                const std::uint8_t nameLength = readU8_(eeBody, dataStart + 2);
                if (static_cast<std::size_t>(3) + nameLength > extLen) {
                    setError_(error, "EncryptedExtensions ALPN name is truncated");
                    return false;
                }
                out.alpnProtocol = eeBody.mid(static_cast<int>(dataStart + 3),
                                              static_cast<int>(nameLength));
                out.hasAlpn = true;
            }

            pos = dataStart + extLen;
        }

        clearError_(error);
        return true;
    }

    // CertificateVerify body: SignatureScheme (2 bytes) + signature<0..2^16-1>.
    static bool parseCertificateVerify(const SwByteArray& cvBody,
                                       std::uint16_t& outSignatureScheme,
                                       SwByteArray& outSignature,
                                       SwString* error = nullptr) {
        const std::size_t total = cvBody.size();
        if (total < 4) {
            setError_(error, "CertificateVerify is truncated");
            return false;
        }
        outSignatureScheme = readU16_(cvBody, 0);
        const std::uint16_t signatureLength = readU16_(cvBody, 2);
        if (static_cast<std::size_t>(4) + signatureLength > total) {
            setError_(error, "CertificateVerify signature is truncated");
            return false;
        }
        outSignature = cvBody.mid(4, static_cast<int>(signatureLength));
        clearError_(error);
        return true;
    }

    // NewSessionTicket (RFC 8446 4.6.1) parsed view.
    struct NewSessionTicket {
        std::uint32_t ticketLifetime;
        std::uint32_t ticketAgeAdd;
        SwByteArray ticketNonce;
        SwByteArray ticket;
        std::uint32_t maxEarlyDataSize; // from the early_data extension (0x2a)
        bool hasEarlyData;

        NewSessionTicket()
            : ticketLifetime(0), ticketAgeAdd(0), maxEarlyDataSize(0),
              hasEarlyData(false) {}
    };

    // Build a NewSessionTicket handshake body (bytes after the 4-byte header).
    static void buildNewSessionTicketBody(std::uint32_t ticketLifetime,
                                          std::uint32_t ticketAgeAdd,
                                          const SwByteArray& ticketNonce,
                                          const SwByteArray& ticket,
                                          std::uint32_t maxEarlyDataSize,
                                          SwByteArray& outBody) {
        outBody.clear();
        appendU32_(outBody, ticketLifetime);
        appendU32_(outBody, ticketAgeAdd);
        outBody.append(static_cast<char>(ticketNonce.size()));
        outBody.append(ticketNonce);
        appendU16_(outBody, static_cast<std::uint16_t>(ticket.size()));
        outBody.append(ticket);

        SwByteArray extensions;
        if (maxEarlyDataSize > 0) {
            // early_data (0x2a) with a 4-byte max_early_data_size.
            appendU16_(extensions, 0x002a);
            appendU16_(extensions, 4);
            appendU32_(extensions, maxEarlyDataSize);
        }
        appendU16_(outBody, static_cast<std::uint16_t>(extensions.size()));
        outBody.append(extensions);
    }

    static bool parseNewSessionTicket(const SwByteArray& body,
                                      NewSessionTicket& out,
                                      SwString* error = nullptr) {
        out = NewSessionTicket();
        const std::size_t total = body.size();
        std::size_t pos = 0;

        if (pos + 8 > total) {
            setError_(error, "NewSessionTicket header is truncated");
            return false;
        }
        out.ticketLifetime = readU32_(body, pos);
        out.ticketAgeAdd = readU32_(body, pos + 4);
        pos += 8;

        if (pos + 1 > total) {
            setError_(error, "NewSessionTicket nonce length is truncated");
            return false;
        }
        const std::uint8_t nonceLength = readU8_(body, pos);
        pos += 1;
        if (pos + nonceLength > total) {
            setError_(error, "NewSessionTicket nonce is truncated");
            return false;
        }
        out.ticketNonce = body.mid(static_cast<int>(pos), static_cast<int>(nonceLength));
        pos += nonceLength;

        if (pos + 2 > total) {
            setError_(error, "NewSessionTicket ticket length is truncated");
            return false;
        }
        const std::uint16_t ticketLength = readU16_(body, pos);
        pos += 2;
        if (pos + ticketLength > total) {
            setError_(error, "NewSessionTicket ticket is truncated");
            return false;
        }
        out.ticket = body.mid(static_cast<int>(pos), static_cast<int>(ticketLength));
        pos += ticketLength;

        if (pos + 2 > total) {
            setError_(error, "NewSessionTicket extensions length is truncated");
            return false;
        }
        const std::uint16_t extensionsLength = readU16_(body, pos);
        pos += 2;
        if (pos + extensionsLength > total) {
            setError_(error, "NewSessionTicket extensions block is truncated");
            return false;
        }
        const std::size_t extensionsEnd = pos + extensionsLength;
        while (pos < extensionsEnd) {
            if (pos + 4 > extensionsEnd) {
                break;
            }
            const std::uint16_t extType = readU16_(body, pos);
            const std::uint16_t extLen = readU16_(body, pos + 2);
            const std::size_t dataStart = pos + 4;
            if (dataStart + extLen > extensionsEnd) {
                break;
            }
            if (extType == 0x002a && extLen >= 4) {
                out.maxEarlyDataSize = readU32_(body, dataStart);
                out.hasEarlyData = true;
            }
            pos = dataStart + extLen;
        }

        clearError_(error);
        return true;
    }

    // A Finished message body is exactly the verify_data.
    static bool parseFinishedVerifyData(const SwByteArray& finishedBody,
                                        SwByteArray& verifyData,
                                        SwString* error = nullptr) {
        if (finishedBody.isEmpty()) {
            setError_(error, "Finished verify_data is empty");
            return false;
        }
        verifyData = finishedBody;
        clearError_(error);
        return true;
    }

    // Extract the first (leaf) certificate DER from a Certificate message body.
    // Structure: certificate_request_context<0..2^8-1>,
    //            certificate_list<0..2^24-1> of { cert_data<1..2^24-1>, extensions<0..2^16-1> }.
    static bool extractLeafCertificate(const SwByteArray& certificateBody,
                                       SwByteArray& leafDer,
                                       SwString* error = nullptr) {
        const std::size_t total = certificateBody.size();
        std::size_t pos = 0;

        if (pos + 1 > total) {
            setError_(error, "Certificate request_context length is truncated");
            return false;
        }
        const std::uint8_t contextLength = readU8_(certificateBody, pos);
        pos += 1;
        if (pos + contextLength > total) {
            setError_(error, "Certificate request_context is truncated");
            return false;
        }
        pos += contextLength;

        if (pos + 3 > total) {
            setError_(error, "Certificate list length is truncated");
            return false;
        }
        const std::uint32_t listLength = readU24_(certificateBody, pos);
        pos += 3;
        if (pos + listLength > total) {
            setError_(error, "Certificate list is truncated");
            return false;
        }
        if (listLength == 0) {
            setError_(error, "Certificate list is empty");
            return false;
        }

        // First CertificateEntry.
        if (pos + 3 > total) {
            setError_(error, "Certificate entry length is truncated");
            return false;
        }
        const std::uint32_t certLength = readU24_(certificateBody, pos);
        pos += 3;
        if (certLength == 0) {
            setError_(error, "Certificate entry is empty");
            return false;
        }
        if (pos + certLength > total) {
            setError_(error, "Certificate entry data is truncated");
            return false;
        }
        leafDer = certificateBody.mid(static_cast<int>(pos), static_cast<int>(certLength));
        clearError_(error);
        return true;
    }

    // Extract every certificate DER of the Certificate message, leaf first.
    static bool extractCertificateChain(const SwByteArray& certificateBody,
                                        std::vector<SwByteArray>& outChain,
                                        SwString* error = nullptr) {
        outChain.clear();
        const std::size_t total = certificateBody.size();
        std::size_t pos = 0;

        if (pos + 1 > total) {
            setError_(error, "Certificate request_context length is truncated");
            return false;
        }
        const std::uint8_t contextLength = readU8_(certificateBody, pos);
        pos += 1;
        if (pos + contextLength > total) {
            setError_(error, "Certificate request_context is truncated");
            return false;
        }
        pos += contextLength;

        if (pos + 3 > total) {
            setError_(error, "Certificate list length is truncated");
            return false;
        }
        const std::uint32_t listLength = readU24_(certificateBody, pos);
        pos += 3;
        if (pos + listLength > total) {
            setError_(error, "Certificate list is truncated");
            return false;
        }
        const std::size_t listEnd = pos + listLength;

        while (pos < listEnd) {
            if (pos + 3 > listEnd) {
                setError_(error, "Certificate entry length is truncated");
                return false;
            }
            const std::uint32_t certLength = readU24_(certificateBody, pos);
            pos += 3;
            if (certLength == 0 || pos + certLength > listEnd) {
                setError_(error, "Certificate entry data is truncated");
                return false;
            }
            outChain.push_back(certificateBody.mid(static_cast<int>(pos),
                                                   static_cast<int>(certLength)));
            pos += certLength;

            // extensions<0..2^16-1> after each entry
            if (pos + 2 > listEnd) {
                setError_(error, "Certificate entry extensions length is truncated");
                return false;
            }
            const std::uint16_t extLength = readU16_(certificateBody, pos);
            pos += 2;
            if (pos + extLength > listEnd) {
                setError_(error, "Certificate entry extensions are truncated");
                return false;
            }
            pos += extLength;
        }

        if (outChain.empty()) {
            setError_(error, "Certificate list is empty");
            return false;
        }
        clearError_(error);
        return true;
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

    // ClientHello server_name extension: ServerNameList<1..2^16-1> of
    // ServerName { name_type(1)=host_name(0), HostName<1..2^16-1> }.
    static void parseServerNameExtension_(const SwByteArray& data,
                                          std::size_t start,
                                          std::uint16_t length,
                                          SwByteArray& outHost) {
        if (length < 5) {
            return;
        }
        const std::uint16_t listLength = readU16_(data, start);
        if (static_cast<std::size_t>(2) + listLength > length) {
            return;
        }
        std::size_t pos = start + 2;
        const std::size_t end = start + 2 + listLength;
        if (pos + 3 > end) {
            return;
        }
        const std::uint8_t nameType = readU8_(data, pos);
        const std::uint16_t nameLength = readU16_(data, pos + 1);
        pos += 3;
        if (nameType != 0 || pos + nameLength > end) {
            return;
        }
        outHost = data.mid(static_cast<int>(pos), static_cast<int>(nameLength));
    }

    static void parseSupportedGroupsExtension_(const SwByteArray& data,
                                               std::size_t start,
                                               std::uint16_t length,
                                               bool& outOffersX25519) {
        if (length < 2) {
            return;
        }
        const std::uint16_t listLength = readU16_(data, start);
        if (static_cast<std::size_t>(2) + listLength > length || (listLength % 2) != 0) {
            return;
        }
        for (std::size_t i = 0; i + 1 < listLength; i += 2) {
            if (readU16_(data, start + 2 + i) == 0x001d) {
                outOffersX25519 = true;
            }
        }
    }

    // signature_algorithms (RFC 8446 4.2.3): SignatureScheme<2..2^16-2>.
    static void parseSignatureAlgorithmsExtension_(const SwByteArray& data,
                                                   std::size_t start,
                                                   std::uint16_t length,
                                                   std::vector<std::uint16_t>& outSchemes) {
        if (length < 2) {
            return;
        }
        const std::uint16_t listLength = readU16_(data, start);
        if (static_cast<std::size_t>(2) + listLength > length || (listLength % 2) != 0) {
            return;
        }
        for (std::size_t i = 0; i + 1 < listLength; i += 2) {
            outSchemes.push_back(readU16_(data, start + 2 + i));
        }
    }

    // pre_shared_key (RFC 8446 4.2.11): identities<7..> of { identity<1..>,
    // obfuscated_ticket_age(4) }, then binders<33..> of opaque<32..255>.
    // Extracts the first identity (ticket) and the first binder.
    static void parsePreSharedKeyExtension_(const SwByteArray& data,
                                            std::size_t start,
                                            std::uint16_t length,
                                            SwByteArray& outIdentity,
                                            SwByteArray& outBinder,
                                            bool& outHasPsk,
                                            std::size_t& outBindersTotalLength) {
        outBindersTotalLength = 0;
        if (length < 2) {
            return;
        }
        const std::uint16_t identitiesLength = readU16_(data, start);
        std::size_t pos = start + 2;
        const std::size_t identitiesEnd = pos + identitiesLength;
        if (identitiesEnd > start + length || pos + 2 > identitiesEnd) {
            return;
        }
        const std::uint16_t idLen = readU16_(data, pos);
        pos += 2;
        if (pos + idLen + 4 > identitiesEnd) {
            return;
        }
        outIdentity = data.mid(static_cast<int>(pos), static_cast<int>(idLen));

        // Binders follow the identities list.
        std::size_t bpos = identitiesEnd;
        if (bpos + 2 > start + length) {
            return;
        }
        const std::uint16_t bindersLength = readU16_(data, bpos);
        bpos += 2;
        if (bpos + bindersLength > start + length || bpos + 1 > start + length) {
            return;
        }
        const std::uint8_t binderLen = readU8_(data, bpos);
        bpos += 1;
        if (bpos + binderLen > start + length) {
            return;
        }
        outBinder = data.mid(static_cast<int>(bpos), static_cast<int>(binderLen));
        outHasPsk = true;
        // The binder is computed over the ClientHello truncated to remove the
        // ENTIRE PskBinderEntry list, i.e. its 2-byte length prefix plus the list
        // body (RFC 8446 4.2.11.2). pre_shared_key is always the last extension,
        // so this tail length is what a verifier must strip — not a fixed 35,
        // which only holds for a single 32-byte binder.
        outBindersTotalLength = static_cast<std::size_t>(2) + bindersLength;
    }

    // ALPN offer: ProtocolNameList<2..2^16-1> of ProtocolName opaque<1..2^8-1>.
    static void parseAlpnOfferExtension_(const SwByteArray& data,
                                         std::size_t start,
                                         std::uint16_t length,
                                         std::vector<SwByteArray>& outProtocols) {
        if (length < 2) {
            return;
        }
        const std::uint16_t listLength = readU16_(data, start);
        if (static_cast<std::size_t>(2) + listLength > length) {
            return;
        }
        std::size_t pos = start + 2;
        const std::size_t end = start + 2 + listLength;
        while (pos < end) {
            const std::uint8_t nameLength = readU8_(data, pos);
            ++pos;
            if (pos + nameLength > end) {
                return;
            }
            outProtocols.push_back(data.mid(static_cast<int>(pos),
                                            static_cast<int>(nameLength)));
            pos += nameLength;
        }
    }

    // Client key_share: KeyShareClientHello { client_shares<0..2^16-1> } of
    // KeyShareEntry { group(2), key_exchange<1..2^16-1> }.
    static void parseClientKeyShareExtension_(const SwByteArray& data,
                                              std::size_t start,
                                              std::uint16_t length,
                                              SwByteArray& outX25519) {
        if (length < 2) {
            return;
        }
        const std::uint16_t sharesLength = readU16_(data, start);
        if (static_cast<std::size_t>(2) + sharesLength > length) {
            return;
        }
        std::size_t pos = start + 2;
        const std::size_t end = start + 2 + sharesLength;
        while (pos + 4 <= end) {
            const std::uint16_t group = readU16_(data, pos);
            const std::uint16_t keyLength = readU16_(data, pos + 2);
            const std::size_t keyStart = pos + 4;
            if (keyStart + keyLength > end) {
                return;
            }
            if (group == 0x001d && keyLength == 32) {
                outX25519 = data.mid(static_cast<int>(keyStart), 32);
            }
            pos = keyStart + keyLength;
        }
    }

    static std::uint8_t byteAt_(const SwByteArray& data, std::size_t index) {
        return static_cast<std::uint8_t>(static_cast<unsigned char>(data[index]));
    }

    static std::uint8_t readU8_(const SwByteArray& data, std::size_t offset) {
        return byteAt_(data, offset);
    }

    static std::uint16_t readU16_(const SwByteArray& data, std::size_t offset) {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(byteAt_(data, offset)) << 8) |
                                          static_cast<std::uint16_t>(byteAt_(data, offset + 1)));
    }

    static std::uint32_t readU24_(const SwByteArray& data, std::size_t offset) {
        return (static_cast<std::uint32_t>(byteAt_(data, offset)) << 16) |
               (static_cast<std::uint32_t>(byteAt_(data, offset + 1)) << 8) |
               static_cast<std::uint32_t>(byteAt_(data, offset + 2));
    }

    static std::uint32_t readU32_(const SwByteArray& data, std::size_t offset) {
        return (static_cast<std::uint32_t>(byteAt_(data, offset)) << 24) |
               (static_cast<std::uint32_t>(byteAt_(data, offset + 1)) << 16) |
               (static_cast<std::uint32_t>(byteAt_(data, offset + 2)) << 8) |
               static_cast<std::uint32_t>(byteAt_(data, offset + 3));
    }

    static void appendU16_(SwByteArray& out, std::uint16_t value) {
        out.append(static_cast<char>((value >> 8) & 0xffU));
        out.append(static_cast<char>(value & 0xffU));
    }

    static void appendU32_(SwByteArray& out, std::uint32_t value) {
        out.append(static_cast<char>((value >> 24) & 0xffU));
        out.append(static_cast<char>((value >> 16) & 0xffU));
        out.append(static_cast<char>((value >> 8) & 0xffU));
        out.append(static_cast<char>(value & 0xffU));
    }

    static const SwByteArray& helloRetryRequestRandom_() {
        static const SwByteArray random = SwByteArray::fromHex(
            SwByteArray("cf21ad74e59a6111be1d8c021e65b891"
                        "c2a211167abb8c5e079e09e2c8a8339c"));
        return random;
    }
};

#endif
