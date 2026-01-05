#include "SwCoreApplication.h"
#include "SwTcpServer.h"
#include "SwWebSocket.h"

#include <iostream>

static SwString computeAcceptKey(const SwString& clientKeyBase64) {
    static const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SwString input = clientKeyBase64 + kGuid;
    std::vector<unsigned char> sha1 = SwCrypto::generateHashSHA1(input.toStdString());
    return SwString(SwCrypto::base64Encode(sha1));
}

static SwByteArray buildServerFrame(uint8_t opcode, const SwByteArray& payload, bool fin = true) {
    const uint64_t len = static_cast<uint64_t>(payload.size());
    SwByteArray frame;
    frame.reserve(static_cast<size_t>(2 + (len <= 125 ? 0 : (len <= 65535 ? 2 : 8)) + len));

    frame.append(static_cast<char>((fin ? 0x80 : 0x00) | (opcode & 0x0F)));
    if (len <= 125) {
        frame.append(static_cast<char>(len & 0x7F));
    } else if (len <= 65535) {
        frame.append(static_cast<char>(126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.append(static_cast<char>((len >> (8 * i)) & 0xFF));
        }
    }
    if (!payload.isEmpty()) {
        frame.append(payload.constData(), payload.size());
    }
    return frame;
}

static bool parseClientFrame(SwByteArray& buffer,
                             uint8_t& outOpcode,
                             bool& outFin,
                             SwByteArray& outPayload,
                             bool& outNeedMoreData) {
    outNeedMoreData = false;
    if (buffer.size() < 2) {
        outNeedMoreData = true;
        return false;
    }

    const unsigned char* data = reinterpret_cast<const unsigned char*>(buffer.constData());
    const uint8_t b0 = data[0];
    const uint8_t b1 = data[1];

    outFin = (b0 & 0x80) != 0;
    outOpcode = static_cast<uint8_t>(b0 & 0x0F);

    if ((b0 & 0x70) != 0) {
        return false;
    }

    const bool masked = (b1 & 0x80) != 0;
    if (!masked) {
        // RFC: client->server frames MUST be masked.
        return false;
    }

    uint64_t payloadLen = static_cast<uint64_t>(b1 & 0x7F);
    size_t pos = 2;

    if (payloadLen == 126) {
        if (buffer.size() < pos + 2) {
            outNeedMoreData = true;
            return false;
        }
        payloadLen = (static_cast<uint64_t>(data[pos]) << 8) |
                     (static_cast<uint64_t>(data[pos + 1]));
        pos += 2;
    } else if (payloadLen == 127) {
        if (buffer.size() < pos + 8) {
            outNeedMoreData = true;
            return false;
        }
        payloadLen = 0;
        for (int i = 0; i < 8; ++i) {
            payloadLen = (payloadLen << 8) | static_cast<uint64_t>(data[pos + i]);
        }
        pos += 8;
    }

    if (buffer.size() < pos + 4) {
        outNeedMoreData = true;
        return false;
    }
    unsigned char maskKey[4] = { data[pos + 0], data[pos + 1], data[pos + 2], data[pos + 3] };
    pos += 4;

    if (payloadLen > static_cast<uint64_t>(buffer.size() - pos)) {
        outNeedMoreData = true;
        return false;
    }

    outPayload = payloadLen > 0
        ? buffer.mid(static_cast<int>(pos), static_cast<int>(payloadLen))
        : SwByteArray();

    for (size_t i = 0; i < outPayload.size(); ++i) {
        outPayload[i] = static_cast<char>(
            static_cast<unsigned char>(outPayload[i]) ^ maskKey[i % 4]);
    }

    buffer.remove(0, static_cast<int>(pos + payloadLen));
    return true;
}

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    SwTcpServer server;
    uint16_t port = 0;
    for (uint16_t p = 19000; p < 19100; ++p) {
        if (server.listen(p)) {
            port = p;
            break;
        }
    }
    if (port == 0) {
        std::cerr << "[WebSocketSelfTest] Failed to bind test server" << std::endl;
        return 1;
    }

    struct ConnState {
        SwTcpSocket* socket = nullptr;
        SwByteArray buffer;
        bool handshakeDone = false;
        bool gotHello = false;
        bool sentServerInit = false;
        bool sentPongOk = false;
    } conn;

    SwObject::connect(&server, SIGNAL(newConnection), [&]() {
        if (conn.socket) {
            return;
        }
        conn.socket = server.nextPendingConnection();
        if (!conn.socket) {
            return;
        }
        conn.socket->setParent(&server);

        SwObject::connect(conn.socket, SIGNAL(readyRead), [&]() {
            while (true) {
                SwString chunk = conn.socket->read();
                if (chunk.isEmpty()) {
                    break;
                }
                conn.buffer.append(chunk.data(), chunk.size());
            }

            if (!conn.handshakeDone) {
                int boundary = conn.buffer.indexOf("\r\n\r\n");
                if (boundary < 0) {
                    return;
                }
                SwString headers = SwString(conn.buffer.left(boundary));
                conn.buffer.remove(0, boundary + 4);

                SwString clientKey;
                SwList<SwString> lines = headers.split("\r\n");
                for (size_t i = 0; i < lines.size(); ++i) {
                    SwString line = lines[i];
                    int colon = line.indexOf(":");
                    if (colon < 0) {
                        continue;
                    }
                    SwString key = line.left(colon).trimmed().toLower();
                    SwString value = line.mid(colon + 1).trimmed();
                    if (key == "sec-websocket-key") {
                        clientKey = value;
                        break;
                    }
                }

                if (clientKey.isEmpty()) {
                    std::cerr << "[WebSocketSelfTest] Missing Sec-WebSocket-Key" << std::endl;
                    conn.socket->close();
                    return;
                }

                SwString accept = computeAcceptKey(clientKey);
                SwString response = "HTTP/1.1 101 Switching Protocols\r\n";
                response += "Upgrade: websocket\r\n";
                response += "Connection: Upgrade\r\n";
                response += "Sec-WebSocket-Accept: " + accept + "\r\n";
                response += "\r\n";
                conn.socket->write(response);
                conn.handshakeDone = true;
            }

            while (conn.handshakeDone && !conn.buffer.isEmpty()) {
                uint8_t opcode = 0;
                bool fin = false;
                bool needMore = false;
                SwByteArray payload;
                if (!parseClientFrame(conn.buffer, opcode, fin, payload, needMore)) {
                    if (needMore) {
                        return;
                    }
                    conn.socket->close();
                    return;
                }

                if (opcode == 0xA /* pong */) {
                    if (payload != SwByteArray("srvping")) {
                        conn.socket->close();
                        return;
                    }
                    if (!conn.sentPongOk) {
                        conn.sentPongOk = true;
                        conn.socket->write(buildServerFrame(0x1 /* text */, SwByteArray("pong-ok")));
                    }
                    continue;
                }

                if (opcode == 0x9 /* ping */) {
                    conn.socket->write(buildServerFrame(0xA /* pong */, payload));
                } else if (opcode == 0x8 /* close */) {
                    conn.socket->write(buildServerFrame(0x8 /* close */, payload));
                    SwTcpSocket* sock = conn.socket;
                    SwTimer::singleShot(200, [sock]() {
                        if (sock) {
                            sock->close();
                        }
                    });
                } else if (opcode == 0x1 /* text */) {
                    if (!conn.gotHello && payload == SwByteArray("hello")) {
                        conn.gotHello = true;
                        if (!conn.sentServerInit) {
                            conn.sentServerInit = true;
                            // Send a fragmented server->client text message ("server-frag")
                            conn.socket->write(buildServerFrame(0x1 /* text */, SwByteArray("server-"), false /* fin */));
                            conn.socket->write(buildServerFrame(0x0 /* continuation */, SwByteArray("frag"), true /* fin */));
                            // Send a server->client ping (client must answer with pong).
                            conn.socket->write(buildServerFrame(0x9 /* ping */, SwByteArray("srvping")));
                        }
                    }
                    conn.socket->write(buildServerFrame(opcode, payload, fin));
                } else {
                    conn.socket->write(buildServerFrame(opcode, payload, fin));
                }
            }
        });
    });

    SwWebSocket ws;

    bool gotEchoHello = false;
    bool gotServerFrag = false;
    bool gotPongOk = false;
    bool gotBinaryEcho = false;
    bool gotClientPong = false;
    bool binarySent = false;
    bool pingSent = false;
    bool closeRequested = false;

    const SwString expectedText = "hello";
    const SwString expectedServerFrag = "server-frag";
    const SwString expectedPongOk = "pong-ok";
    SwByteArray expectedBinary;
    expectedBinary.resize(66000, 'x'); // forces 64-bit length encoding (127)
    const SwByteArray expectedPongPayload("pong");

    SwTimer timeout(8000);
    timeout.setSingleShot(true);
    SwObject::connect(&timeout, SIGNAL(timeout), [&]() {
        std::cerr << "[WebSocketSelfTest] Timeout" << std::endl;
        app.exit(1);
    });
    timeout.start();

    auto maybeFinish = [&]() {
        if (closeRequested) {
            return;
        }
        if (gotEchoHello && gotServerFrag && gotPongOk && gotBinaryEcho && gotClientPong) {
            closeRequested = true;
            ws.close();
        }
    };

    SwObject::connect(&ws, SIGNAL(errorOccurred), [&](int err) {
        std::cerr << "[WebSocketSelfTest] errorOccurred: " << err << std::endl;
        app.exit(1);
    });

    SwObject::connect(&ws, SIGNAL(connected), [&]() {
        ws.sendTextMessage(expectedText);
    });

    SwObject::connect(&ws, SIGNAL(textMessageReceived), [&](const SwString& message) {
        if (message == expectedText) {
            gotEchoHello = true;
            if (!binarySent) {
                binarySent = true;
                ws.sendBinaryMessage(expectedBinary);
            }
            maybeFinish();
            return;
        }
        if (message == expectedServerFrag) {
            gotServerFrag = true;
            maybeFinish();
            return;
        }
        if (message == expectedPongOk) {
            gotPongOk = true;
            maybeFinish();
            return;
        }
        std::cerr << "[WebSocketSelfTest] Unexpected text: " << message.toStdString() << std::endl;
        app.exit(1);
    });

    SwObject::connect(&ws, SIGNAL(binaryMessageReceived), [&](const SwByteArray& data) {
        if (data != expectedBinary) {
            std::cerr << "[WebSocketSelfTest] Binary mismatch" << std::endl;
            app.exit(1);
            return;
        }
        gotBinaryEcho = true;
        if (!pingSent) {
            pingSent = true;
            ws.ping(expectedPongPayload);
        }
        maybeFinish();
    });

    SwObject::connect(&ws, SIGNAL(pong), [&](uint64_t /*elapsedMs*/, const SwByteArray& payload) {
        if (payload != expectedPongPayload) {
            std::cerr << "[WebSocketSelfTest] Pong mismatch" << std::endl;
            app.exit(1);
            return;
        }
        gotClientPong = true;
        maybeFinish();
    });

    SwObject::connect(&ws, SIGNAL(disconnected), [&]() {
        if (!closeRequested) {
            std::cerr << "[WebSocketSelfTest] Disconnected unexpectedly (code=" << ws.closeCode()
                      << " reason=\"" << ws.closeReason().toStdString() << "\")" << std::endl;
            app.exit(1);
            return;
        }
        if (ws.closeCode() != 1000) {
            std::cerr << "[WebSocketSelfTest] Close code mismatch: " << ws.closeCode() << std::endl;
            app.exit(1);
            return;
        }
        app.exit(0);
    });

    ws.open("ws://127.0.0.1:" + SwString::number(static_cast<int>(port)) + "/");
    return app.exec();
}
