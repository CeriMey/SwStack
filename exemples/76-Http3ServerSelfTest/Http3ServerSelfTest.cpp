// End-to-end HTTP/3 request/response over two loopback SwQuicConnections.
//
// A client SwHttp3Connection helper builds a GET request on a QUIC stream; the
// server SwHttp3Server routes it and answers; the client parses the response.
// This exercises the whole HTTP/3 session layer -- control stream + SETTINGS,
// unidirectional stream demux, QPACK static-table HEADERS, DATA framing, and
// the SwHttpRequest/SwHttpResponse bridge -- on top of the real, protected
// QUIC transport.

#include "core/io/http3/SwHttp3Connection.h"
#include "core/io/http3/SwHttp3Server.h"
#include "core/io/quic/SwQuicConnection.h"
#include "core/io/quic/SwQuicConnectionId.h"
#include "core/io/quic/SwQuicInitialSecrets.h"
#include "core/types/SwVector.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool requireTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

// Move every datagram the sender produced into the receiver.
bool transfer(SwQuicConnection& from, SwQuicConnection& to, std::uint64_t nowMs,
              SwString* error) {
    SwVector<SwByteArray> datagrams;
    if (!from.buildDatagrams(nowMs, datagrams, error)) {
        return false;
    }
    for (std::size_t i = 0; i < datagrams.size(); ++i) {
        if (!to.receiveDatagram(datagrams[i], nowMs, error)) {
            return false;
        }
    }
    return true;
}

bool testHttp3RequestResponse() {
    SwString error;

    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("h3client"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("h3server"), serverCid, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);
    SwQuicTransportParameters clientParameters;
    clientParameters.initialMaxData = 262144;
    clientParameters.initialMaxStreamDataBidiLocal = 262144;
    clientParameters.initialMaxStreamDataBidiRemote = 262144;
    clientParameters.initialMaxStreamDataUni = 262144;
    clientParameters.initialMaxStreamsBidi = 100;
    clientParameters.initialMaxStreamsUni = 100;
    clientParameters.maxDatagramFrameSize = 1400;
    clientParameters.resetStreamAt = true;
    SwQuicTransportParameters serverParameters = clientParameters;
    client.applyLocalTransportParameters(clientParameters);
    client.applyPeerTransportParameters(serverParameters);
    server.applyLocalTransportParameters(serverParameters);
    server.applyPeerTransportParameters(clientParameters);

    SwHttp3Server h3Server(&server);
    bool handlerCalled = false;
    SwString seenPath;
    SwString seenMethod;
    h3Server.setRequestHandler([&](const SwHttpRequest& request) -> SwHttpResponse {
        handlerCalled = true;
        seenPath = request.path;
        seenMethod = request.method;
        SwHttpResponse response;
        response.status = 200;
        response.headers[SwString("content-type")] = SwString("text/plain");
        response.headers[SwString("content-length")] = SwString("999");
        response.headers[SwString("bad field")] = SwString("must-not-be-sent");
        response.headers[SwString("x-injected")] =
            SwString("safe\r\ninjected: no");
        response.body = SwByteArray("hello from h3");
        return response;
    });

    std::uint64_t now = 500;
    if (!requireTrue(h3Server.start(&error), "h3 server start failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // Client control stream + SETTINGS (stream type 0x00), then a GET request
    // on client-initiated bidi stream 0.
    SwHttp3Frame::SettingList clientSettings;
    clientSettings.push_back(std::make_pair(SwHttp3Frame::settingH3Datagram(), std::uint64_t(1)));
    clientSettings.push_back(std::make_pair(SwHttp3Frame::settingEnableWebTransport(),
                                            std::uint64_t(1)));
    SwByteArray controlStream;
    if (!requireTrue(SwHttp3Connection::buildControlStream(clientSettings, controlStream, &error),
                     "client control stream build failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(client.sendStreamData(2, controlStream, false, &error),
                     "client control stream send failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwHttp3Connection::Request request;
    request.method = SwByteArray("GET");
    request.scheme = SwByteArray("https");
    request.authority = SwByteArray("example.test");
    request.path = SwByteArray("/api/state");
    SwByteArray requestStream;
    if (!requireTrue(SwHttp3Connection::buildRequest(request, requestStream, &error),
                     "client request build failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(client.sendStreamData(0, requestStream, true, &error),
                     "client request send failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // Ferry client -> server and let the server route + answer.
    if (!requireTrue(transfer(client, server, now, &error), "client->server transfer failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(h3Server.pump(&error), "h3 server pump failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(handlerCalled, "server request handler was not called") ||
        !requireTrue(seenMethod == SwString("GET"), "request method mismatch") ||
        !requireTrue(seenPath == SwString("/api/state"), "request path mismatch")) {
        return false;
    }

    // Ferry the server response back to the client.
    now += 10;
    if (!requireTrue(transfer(server, client, now, &error), "server->client transfer failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // Reassemble the response stream (stream 0) on the client and parse it.
    const SwByteArray responseStream = client.readStream(0);
    SwHttp3Connection::Response response;
    if (!requireTrue(SwHttp3Connection::parseResponse(responseStream, response, &error),
                     "client response parse failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwByteArray contentLength;
    bool invalidHeaderObserved = false;
    for (std::size_t i = 0; i < response.headers.size(); ++i) {
        if (response.headers[i].first == SwByteArray("content-length")) {
            contentLength = response.headers[i].second;
        }
        if (response.headers[i].first == SwByteArray("bad field") ||
            response.headers[i].first == SwByteArray("x-injected")) {
            invalidHeaderObserved = true;
        }
    }

    return requireTrue(response.status == SwByteArray("200"), "response status mismatch") &&
           requireTrue(response.body == SwByteArray("hello from h3"), "response body mismatch") &&
           requireTrue(contentLength == SwByteArray("13"),
                       "HTTP/3 did not recalculate Content-Length") &&
           requireTrue(!invalidHeaderObserved,
                       "HTTP/3 emitted an invalid application response header") &&
           requireTrue(h3Server.peerSettingsReceived(), "server did not receive client SETTINGS") &&
           requireTrue(h3Server.peerEnableWebTransport(),
                       "server did not see client WebTransport setting");
}

// WebTransport Extended CONNECT is accepted by the server session.
bool testWebTransportConnect() {
    SwString error;

    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("wtclien"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("wtserve"), serverCid, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);
    SwQuicTransportParameters clientParameters;
    clientParameters.initialMaxData = 262144;
    clientParameters.initialMaxStreamDataBidiLocal = 262144;
    clientParameters.initialMaxStreamDataBidiRemote = 262144;
    clientParameters.initialMaxStreamDataUni = 262144;
    clientParameters.initialMaxStreamsBidi = 100;
    clientParameters.initialMaxStreamsUni = 100;
    clientParameters.maxDatagramFrameSize = 1400;
    clientParameters.resetStreamAt = true;
    SwQuicTransportParameters serverParameters = clientParameters;
    client.applyLocalTransportParameters(clientParameters);
    client.applyPeerTransportParameters(serverParameters);
    server.applyLocalTransportParameters(serverParameters);
    server.applyPeerTransportParameters(clientParameters);

    SwHttp3Server h3Server(&server);
    bool sessionRequested = false;
    std::uint64_t sessionStreamId = 0;
    SwByteArray receivedWebTransportStream;
    SwByteArray receivedWebTransportDatagram;
    h3Server.setWebTransportHandler([&](const SwHttpRequest& request,
                                        std::uint64_t sessionId) -> bool {
        (void)request;
        sessionRequested = true;
        sessionStreamId = sessionId;
        return true;
    });
    h3Server.setWebTransportStreamHandler(
        [&](std::uint64_t sessionId, std::uint64_t streamId,
            const SwByteArray& payload, bool bidirectional, bool fin) {
            (void)fin;
            if (sessionId == 0 && streamId == 4 && bidirectional) {
                receivedWebTransportStream.append(payload);
            }
        });
    h3Server.setWebTransportDatagramHandler(
        [&](std::uint64_t sessionId, const SwByteArray& payload) {
            if (sessionId == 0) receivedWebTransportDatagram = payload;
        });

    std::uint64_t now = 800;
    if (!requireTrue(h3Server.start(&error), "wt server start failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwByteArray clientControl;
    SwHttp3Frame::SettingList clientSettings =
        SwWebTransportSession::requiredClientSettings();
    if (!requireTrue(SwHttp3Connection::buildControlStream(
                         clientSettings, clientControl, &error),
                     "wt client SETTINGS build failed") ||
        !requireTrue(client.sendStreamData(2, clientControl, false, &error),
                     "wt client SETTINGS send failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwWebTransportSession::ConnectRequest connect;
    connect.authority = SwByteArray("example.test:443");
    connect.path = SwByteArray("/mesh");
    SwByteArray connectStream;
    if (!requireTrue(SwWebTransportSession::buildConnect(connect, connectStream, &error),
                     "wt connect build failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    // CONNECT remains open for the lifetime of the WebTransport session.
    if (!requireTrue(client.sendStreamData(0, connectStream, false, &error),
                     "wt connect send failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    if (!requireTrue(transfer(client, server, now, &error), "wt client->server failed") ||
        !requireTrue(h3Server.pump(&error), "wt server pump failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(sessionRequested, "WebTransport handler was not called") ||
        !requireTrue(sessionStreamId == 0, "WebTransport session id mismatch")) {
        return false;
    }

    SwByteArray webTransportStream;
    if (!requireTrue(SwWebTransportSession::buildBidiStreamHeader(
                         0, webTransportStream, &error),
                     "wt stream header build failed")) return false;
    webTransportStream.append("control", 7);
    if (!requireTrue(client.sendStreamData(
                         4, webTransportStream, false, &error),
                     "wt stream send failed") ||
        !requireTrue(transfer(client, server, now, &error),
                     "wt stream transfer failed") ||
        !requireTrue(h3Server.pump(&error), "wt stream pump failed") ||
        !requireTrue(receivedWebTransportStream == SwByteArray("control"),
                     "wt stream payload mismatch")) return false;

    SwByteArray encodedDatagram;
    if (!requireTrue(SwWebTransportSession::encodeDatagram(
                         0, SwByteArray("probe"), encodedDatagram, &error),
                     "wt datagram encode failed") ||
        !requireTrue(client.queueDatagramFrame(encodedDatagram, &error),
                     "wt datagram queue failed") ||
        !requireTrue(transfer(client, server, now, &error),
                     "wt datagram transfer failed") ||
        !requireTrue(h3Server.handleWebTransportDatagram(
                         server.takeDatagram(), &error),
                     "wt datagram dispatch failed") ||
        !requireTrue(receivedWebTransportDatagram == SwByteArray("probe"),
                     "wt datagram payload mismatch")) return false;

    now += 10;
    if (!requireTrue(transfer(server, client, now, &error), "wt server->client failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    const SwByteArray responseStream = client.readStream(0);
    std::size_t offset = 0;
    SwHttp3Frame frame;
    if (!requireTrue(SwHttp3FrameCodec::decodeFrame(responseStream, offset, frame, &error),
                     "wt response decode failed") ||
        !requireTrue(frame.type() == SwHttp3Frame::Type::Headers,
                     "wt response is not a HEADERS frame")) {
        return false;
    }
    bool accepted = false;
    if (!requireTrue(SwWebTransportSession::isSessionAccepted(frame.payload(), accepted, &error),
                     "wt accept check failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    return requireTrue(accepted, "WebTransport session was not accepted (status not 2xx)");
}

bool testReliableResetPrimitives() {
    SwString error;
    SwQuicTransportParameters parameters;
    parameters.resetStreamAt = true;
    SwByteArray encodedParameters;
    SwQuicTransportParameters decodedParameters;
    if (!requireTrue(parameters.encode(encodedParameters, &error),
                     "reset_stream_at parameter encode failed") ||
        !requireTrue(SwQuicTransportParameters::decode(
                         encodedParameters, decodedParameters, &error),
                     "reset_stream_at parameter decode failed") ||
        !requireTrue(decodedParameters.resetStreamAt,
                     "reset_stream_at parameter was not preserved")) return false;

    const SwQuicFrame reset = SwQuicFrame::resetStreamAt(4, 17, 23, 7);
    SwByteArray encodedFrame;
    std::size_t offset = 0;
    SwQuicFrame decoded = SwQuicFrame::ping();
    if (!requireTrue(SwQuicFrameCodec::encodeFrame(
                           reset, encodedFrame, &error),
                       "RESET_STREAM_AT encode failed") ||
        !requireTrue(SwQuicFrameCodec::decodeFrame(
                           encodedFrame, offset, decoded, &error),
                       "RESET_STREAM_AT decode failed")) return false;
    if (!requireTrue(decoded.type() == SwQuicFrame::Type::ResetStreamAt &&
                           decoded.streamId() == 4 && decoded.errorCode() == 17 &&
                           decoded.finalSize() == 23 && decoded.reliableSize() == 7,
                       "RESET_STREAM_AT fields mismatch")) return false;

    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("rrclient"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("rrserver"), serverCid, &error) ||
        !SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        return false;
    }
    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);
    SwQuicTransportParameters negotiated;
    negotiated.initialMaxData = 262144;
    negotiated.initialMaxStreamDataBidiLocal = 262144;
    negotiated.initialMaxStreamDataBidiRemote = 262144;
    negotiated.initialMaxStreamsBidi = 10;
    negotiated.resetStreamAt = true;
    client.applyLocalTransportParameters(negotiated);
    client.applyPeerTransportParameters(negotiated);
    server.applyLocalTransportParameters(negotiated);
    server.applyPeerTransportParameters(negotiated);

    if (!client.sendStreamData(0, SwByteArray("headerbody"), false, &error)) return false;
    SwVector<SwByteArray> dataPackets;
    if (!client.buildDatagrams(1000, dataPackets, &error) || dataPackets.empty() ||
        !client.resetStreamAt(0, 99, 6, &error)) return false;
    SwVector<SwByteArray> resetPackets;
    if (!client.buildDatagrams(1001, resetPackets, &error) || resetPackets.empty()) return false;

    // Reorder reset ahead of the reliable prefix. The receive-side reset must
    // remain withheld until the application consumes that prefix.
    if (!server.receiveDatagram(resetPackets.front(), 1002, &error) ||
        !requireTrue(!server.isStreamReceiveReset(0),
                     "RESET_STREAM_AT surfaced before its reliable prefix") ||
        !server.receiveDatagram(dataPackets.front(), 1003, &error)) return false;
    const SwByteArray delivered = server.readStream(0);
    return requireTrue(delivered == SwByteArray("headerbody"),
                       "RESET_STREAM_AT reliable prefix was not delivered") &&
           requireTrue(server.isStreamReceiveReset(0),
                       "RESET_STREAM_AT was not surfaced after prefix delivery");
}

// Regression for the split-FIN case: the request HEADERS arrive with FIN=0 and
// the stream is half-closed with a separate payload-less STREAM frame. The
// server must still dispatch and answer the buffered request.
bool testHttp3SplitFinRequest() {
    SwString error;

    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("finclie1"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("finserv1"), serverCid, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    SwHttp3Server h3Server(&server);
    bool handlerCalled = false;
    h3Server.setRequestHandler([&](const SwHttpRequest& request) -> SwHttpResponse {
        handlerCalled = true;
        (void)request;
        return swHttpTextResponse(200, SwString("ok"));
    });

    std::uint64_t now = 700;
    if (!requireTrue(h3Server.start(&error), "split-fin server start failed")) {
        return false;
    }

    SwHttp3Connection::Request request;
    request.method = SwByteArray("GET");
    request.scheme = SwByteArray("https");
    request.authority = SwByteArray("example.test");
    request.path = SwByteArray("/split");
    SwByteArray requestStream;
    if (!requireTrue(SwHttp3Connection::buildRequest(request, requestStream, &error),
                     "split-fin request build failed")) {
        return false;
    }

    // HEADERS with FIN=0, then a bare FIN in a separate STREAM frame.
    if (!requireTrue(client.sendStreamData(0, requestStream, false, &error),
                     "split-fin headers send failed") ||
        !requireTrue(client.sendStreamData(0, SwByteArray(), true, &error),
                     "split-fin bare-fin send failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // Deliver everything and pump; the server must answer despite the FIN
    // arriving in a payload-less frame.
    if (!requireTrue(transfer(client, server, now, &error), "split-fin transfer failed") ||
        !requireTrue(h3Server.pump(&error), "split-fin pump failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    return requireTrue(handlerCalled, "split-fin request was never dispatched");
}

// RFC 9114 6.2.1: the first frame on the control stream MUST be SETTINGS; a
// control stream that opens with another frame is H3_MISSING_SETTINGS and the
// server must reject the connection.
bool testHttp3ControlStreamMissingSettings() {
    SwString error;

    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("h3ctlcli"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("h3ctlsrv"), serverCid, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    SwHttp3Server h3Server(&server);
    if (!requireTrue(h3Server.start(&error), "control-stream test server start failed")) {
        return false;
    }

    // Build a control stream (type 0x00) whose first frame is GOAWAY, not
    // SETTINGS.
    SwByteArray badControl;
    if (!requireTrue(SwQuicVarIntCodec::encode(0x00, badControl, &error),
                     "control type encode failed")) {
        return false;
    }
    SwByteArray goaway;
    if (!requireTrue(SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::goAway(0), goaway, &error),
                     "goaway encode failed")) {
        return false;
    }
    badControl.append(goaway);

    if (!requireTrue(client.sendStreamData(2, badControl, false, &error),
                     "bad control stream send failed") ||
        !requireTrue(transfer(client, server, 500, &error), "bad control transfer failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // The server must reject the connection (pump returns false).
    SwString pumpError;
    const bool rejected = !h3Server.pump(&pumpError);
    return requireTrue(rejected,
                       "server accepted a control stream not starting with SETTINGS");
}

// A complete outer SETTINGS frame whose payload ends between an identifier and
// its value is malformed now, not a partial frame that could wait forever.
bool testHttp3ControlStreamRejectsMalformedSettingsImmediately() {
    SwString error;
    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("badsetc1"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("badsets1"), serverCid, &error)) {
        return false;
    }
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    SwHttp3Server h3Server(&server);
    if (!h3Server.start(&error)) return false;

    SwByteArray malformedControl;
    // stream type = control, frame type = SETTINGS, payload length = 1,
    // payload = one setting identifier with its mandatory value missing.
    if (!SwQuicVarIntCodec::encode(0x00, malformedControl, &error) ||
        !SwQuicVarIntCodec::encode(SwHttp3Frame::typeSettings(),
                                   malformedControl, &error) ||
        !SwQuicVarIntCodec::encode(1, malformedControl, &error)) {
        return false;
    }
    malformedControl.append(static_cast<char>(0x01));
    if (!client.sendStreamData(2, malformedControl, false, &error) ||
        !transfer(client, server, 700, &error)) {
        return false;
    }

    SwString pumpError;
    return requireTrue(!h3Server.pump(&pumpError),
                       "complete malformed SETTINGS was treated as partial") &&
           requireTrue(pumpError.contains("Malformed HTTP/3 control-stream frame"),
                       "malformed SETTINGS did not report a control-frame error");
}

// A control frame may legally arrive incrementally, but an attacker can claim
// a gigantic payload and never finish it. The retained prefix must consume the
// same local/global pending-byte budget as request buffering and stop growing at
// the configured bound even though QUIC readStream() keeps returning flow credit.
bool testHttp3PartialControlFrameUsesPendingBudget() {
    SwString error;
    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("ctlmemc1"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("ctlmems1"), serverCid, &error)) {
        return false;
    }
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    static const std::size_t kPendingLimit = 256;
    std::size_t globalPending = 0;
    bool bounded = false;
    {
        SwHttp3Server h3Server(&server);
        SwHttpLimits limits;
        limits.maxPendingRequestBytes = kPendingLimit;
        limits.maxPendingRequestBytesGlobal = kPendingLimit;
        h3Server.setLimits(limits);
        h3Server.setPendingRequestBudgetHandlers(
            [&](std::size_t bytes) -> bool {
                if (globalPending > kPendingLimit ||
                    bytes > kPendingLimit - globalPending) {
                    return false;
                }
                globalPending += bytes;
                return true;
            },
            [&](std::size_t bytes) {
                globalPending = bytes > globalPending
                    ? 0
                    : globalPending - bytes;
            });
        if (!h3Server.start(&error)) return false;

        SwHttp3Frame::SettingList settings;
        SwByteArray control;
        if (!SwHttp3Connection::buildControlStream(settings, control, &error) ||
            !client.sendStreamData(2, control, false, &error) ||
            !transfer(client, server, 800, &error) ||
            !h3Server.pump(&error)) {
            return false;
        }
        if (!requireTrue(h3Server.pendingRequestBytes() == 0 && globalPending == 0,
                         "parsed control SETTINGS retained pending bytes")) {
            return false;
        }

        SwByteArray partialFrame;
        if (!SwQuicVarIntCodec::encode(0x21, partialFrame, &error) ||
            !SwQuicVarIntCodec::encode(std::uint64_t(1) << 30,
                                       partialFrame, &error)) {
            return false;
        }
        partialFrame.append(SwByteArray(192, 'x'));
        if (!client.sendStreamData(2, partialFrame, false, &error) ||
            !transfer(client, server, 810, &error) ||
            !h3Server.pump(&error)) {
            return false;
        }
        if (!requireTrue(h3Server.pendingRequestBytes() > 0 &&
                             h3Server.pendingRequestBytes() <= kPendingLimit &&
                             globalPending == h3Server.pendingRequestBytes(),
                         "partial control frame was not charged to the pending budget")) {
            return false;
        }

        const SwByteArray continuation(192, 'y');
        if (!client.sendStreamData(2, continuation, false, &error) ||
            !transfer(client, server, 820, &error)) {
            return false;
        }
        SwString pumpError;
        const bool rejected = !h3Server.pump(&pumpError);
        bounded = requireTrue(rejected,
                              "oversized partial control frame bypassed pending budget") &&
                  requireTrue(pumpError.contains("buffering limit exceeded"),
                              "control-frame budget rejection reported the wrong error") &&
                  requireTrue(h3Server.pendingRequestBytes() <= kPendingLimit &&
                                  globalPending <= kPendingLimit,
                              "control-frame buffering exceeded its configured bound");
    }

    return bounded &&
           requireTrue(globalPending == 0,
                       "destroyed control stream leaked its global byte reservation");
}

// A completed H3 request must release its decoded headers/body immediately,
// while the response bytes remain independently queued in QUIC. Returning a
// MAX_STREAMS credit for each completed request also lets a long-lived browser
// connection go beyond the initial cumulative stream limit (100 by default).
bool testHttp3SequentialStreamCreditAndRequestBudgets() {
    SwString error;

    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("lifecli1"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("lifesrv1"), serverCid, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    // Start with only two client-initiated bidirectional streams. Every later
    // stream in this test therefore depends on a received MAX_STREAMS update.
    SwQuicTransportParameters serverParameters = server.localTransportParameters();
    serverParameters.initialMaxStreamsBidi = 2;
    server.applyLocalTransportParameters(serverParameters);
    client.applyPeerTransportParameters(serverParameters);
    const SwQuicTransportParameters clientParameters = client.localTransportParameters();
    server.applyPeerTransportParameters(clientParameters);

    SwHttp3Server h3Server(&server);
    SwHttpLimits limits;
    limits.maxPendingRequestBytes = 4096;
    limits.maxPendingRequestBytesGlobal = 4096;
    h3Server.setLimits(limits);

    std::size_t globalPendingBytes = 0;
    std::size_t globalPendingLimit = limits.maxPendingRequestBytesGlobal;
    h3Server.setPendingRequestBudgetHandlers(
        [&](std::size_t bytes) -> bool {
            if (globalPendingBytes > globalPendingLimit ||
                bytes > globalPendingLimit - globalPendingBytes) {
                return false;
            }
            globalPendingBytes += bytes;
            return true;
        },
        [&](std::size_t bytes) {
            globalPendingBytes = bytes > globalPendingBytes
                ? 0
                : globalPendingBytes - bytes;
        });

    std::size_t handlerCalls = 0;
    h3Server.setRequestHandler([&](const SwHttpRequest& request) -> SwHttpResponse {
        ++handlerCalls;
        return swHttpTextResponse(200, SwString("queued response for ") + request.path);
    });

    std::uint64_t now = 1000;
    if (!requireTrue(h3Server.start(&error), "lifecycle server start failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwHttp3Frame::SettingList clientSettings;
    SwByteArray controlStream;
    if (!requireTrue(SwHttp3Connection::buildControlStream(
                         clientSettings, controlStream, &error),
                     "lifecycle control stream build failed") ||
        !requireTrue(client.sendStreamData(2, controlStream, false, &error),
                     "lifecycle control stream send failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    const auto roundTrip = [&](std::size_t requestIndex,
                               const SwByteArray& expectedStatus,
                               bool expectHandler) -> bool {
        const std::uint64_t streamId = static_cast<std::uint64_t>(requestIndex) * 4;
        SwHttp3Connection::Request request;
        request.method = SwByteArray("POST");
        request.scheme = SwByteArray("https");
        request.authority = SwByteArray("example.test");
        request.path = SwByteArray(
            (SwString("/lifecycle/") + SwString::number(
                 static_cast<long long>(requestIndex))).toStdString());
        request.body = SwByteArray(128, static_cast<char>('a' + requestIndex % 26));

        SwByteArray requestStream;
        if (!SwHttp3Connection::buildRequest(request, requestStream, &error) ||
            !client.sendStreamData(streamId, requestStream, true, &error)) {
            std::cerr << "request " << requestIndex << ": "
                      << error.toStdString() << std::endl;
            return false;
        }

        const std::size_t callsBefore = handlerCalls;
        now += 30; // exceed the application-data delayed-ACK deadline
        if (!transfer(client, server, now, &error) || !h3Server.pump(&error)) {
            std::cerr << "request pump " << requestIndex << ": "
                      << error.toStdString() << std::endl;
            return false;
        }
        if (!requireTrue(h3Server.pendingRequestBytes() == 0,
                         "completed request retained H3 session bytes") ||
            !requireTrue(globalPendingBytes == 0,
                         "completed request retained global H3 bytes") ||
            !requireTrue(handlerCalls == callsBefore + (expectHandler ? 1U : 0U),
                         "request-budget rejection reached the route handler")) {
            return false;
        }

        now += 10;
        if (!transfer(server, client, now, &error)) {
            std::cerr << "response transfer " << requestIndex << ": "
                      << error.toStdString() << std::endl;
            return false;
        }

        // Parsing after SwHttp3Server released all request storage proves that
        // the response was retained by SwQuicConnection, not by that storage.
        const SwByteArray responseBytes = client.readStream(streamId);
        SwHttp3Connection::Response response;
        if (!SwHttp3Connection::parseResponse(responseBytes, response, &error)) {
            std::cerr << "response parse " << requestIndex << ": "
                      << error.toStdString() << std::endl;
            return false;
        }
        if (!requireTrue(response.status == expectedStatus,
                         "unexpected lifecycle response status") ||
            !requireTrue(!expectHandler || !response.body.isEmpty(),
                         "queued response body was lost during request cleanup")) {
            return false;
        }

        // The server may retire the stream and return cumulative MAX_STREAMS
        // credit only after the response FIN is acknowledged. Complete that
        // exchange before opening the next sequential request.
        now += 30; // exceed the application-data delayed-ACK deadline
        if (!transfer(client, server, now, &error)) {
            std::cerr << "response ACK " << requestIndex << ": "
                      << error.toStdString() << std::endl;
            return false;
        }
        now += 10;
        if (!transfer(server, client, now, &error)) {
            std::cerr << "MAX_STREAMS transfer " << requestIndex << ": "
                      << error.toStdString() << std::endl;
            return false;
        }
        return true;
    };

    // 105 successful sequential requests: this is above the historical fixed
    // limit of 100 and far above the deliberately configured initial limit 2.
    for (std::size_t i = 0; i < 105; ++i) {
        if (!roundTrip(i, SwByteArray("200"), true)) return false;
    }

    // Exercise the driver-global reservation hook, then the per-connection
    // pending-byte cap. Both must reject before routing, release all charges,
    // return stream credit, and leave the connection usable.
    globalPendingLimit = 8;
    if (!roundTrip(105, SwByteArray("503"), false)) return false;

    globalPendingLimit = 4096;
    limits.maxPendingRequestBytes = 8;
    h3Server.setLimits(limits);
    if (!roundTrip(106, SwByteArray("503"), false)) return false;

    limits.maxPendingRequestBytes = 4096;
    h3Server.setLimits(limits);
    if (!roundTrip(107, SwByteArray("200"), true)) return false;

    return requireTrue(handlerCalls == 106,
                       "unexpected number of lifecycle handler calls") &&
           requireTrue(h3Server.requestsHandled() == 108,
                       "unexpected number of handled request streams") &&
           requireTrue(server.localBidirectionalStreamLimit() == 110,
                       "MAX_STREAMS credit was not returned for every completed request") &&
           requireTrue(h3Server.requestStreamStateCount() == 0,
                       "completed HTTP/3 request states were retained") &&
           requireTrue(h3Server.completedRequestRangeCount() <= 1,
                       "completed HTTP/3 stream tombstones did not compact") &&
           requireTrue(server.trackedReceiveStreamCount() <= 1 &&
                       server.trackedSendStreamCount() <= 1,
                       "retired QUIC request stream state remained allocated") &&
           requireTrue(server.retiredPeerBidirectionalRangeCount() <= 1,
                       "retired QUIC stream tombstones did not compact") &&
           requireTrue(h3Server.pendingRequestBytes() == 0 && globalPendingBytes == 0,
                       "request budgets were not fully released");
}

// ACKing a response FIN does not close the send side while earlier DATA is
// missing. Deliver the FIN packet first, let packet-threshold loss detection
// schedule retransmissions, then ACK those retransmissions out of order.
bool testHttp3CreditWaitsForEveryResponseByteAck() {
    SwString error;
    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("ackcli01"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("acksrv01"), serverCid, &error)) {
        return false;
    }
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    SwQuicTransportParameters serverParameters = server.localTransportParameters();
    serverParameters.initialMaxStreamsBidi = 1;
    server.applyLocalTransportParameters(serverParameters);
    client.applyPeerTransportParameters(serverParameters);
    server.applyPeerTransportParameters(client.localTransportParameters());

    SwHttp3Server h3Server(&server);
    h3Server.setRequestHandler([](const SwHttpRequest&) -> SwHttpResponse {
        SwHttpResponse response = swHttpTextResponse(200, SwString("large response"));
        response.body = SwByteArray(9000, 'z');
        return response;
    });

    std::uint64_t now = 7000;
    if (!h3Server.start(&error)) return false;
    SwHttp3Frame::SettingList settings;
    SwByteArray control;
    if (!SwHttp3Connection::buildControlStream(settings, control, &error) ||
        !client.sendStreamData(2, control, false, &error) ||
        !transfer(client, server, now, &error) ||
        !h3Server.pump(&error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    // Flush the server control stream before the response so the captured
    // datagrams below contain the request response stream only.
    if (!transfer(server, client, now + 10, &error)) return false;
    if (!transfer(client, server, now + 40, &error)) return false;

    SwHttp3Connection::Request request;
    request.method = SwByteArray("GET");
    request.scheme = SwByteArray("https");
    request.authority = SwByteArray("example.test");
    request.path = SwByteArray("/reordered-acks");
    SwByteArray requestStream;
    if (!SwHttp3Connection::buildRequest(request, requestStream, &error) ||
        !client.sendStreamData(0, requestStream, true, &error) ||
        !transfer(client, server, now + 50, &error) ||
        !h3Server.pump(&error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwVector<SwByteArray> responseDatagrams;
    if (!server.buildDatagrams(now + 60, responseDatagrams, &error) ||
        !requireTrue(responseDatagrams.size() >= 4,
                     "large response did not span multiple QUIC packets")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // The last packet carries FIN. ACK it before all lower-offset DATA.
    if (!client.receiveDatagram(responseDatagrams[responseDatagrams.size() - 1],
                                now + 70, &error) ||
        !transfer(client, server, now + 100, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(server.localBidirectionalStreamLimit() == 1,
                     "FIN ACK returned MAX_STREAMS before earlier DATA was ACKed")) {
        return false;
    }

    // Deliver the old packets only after their loss was inferred. Their frames
    // are also retransmitted; duplicate/out-of-order ACKs must merge without
    // double-counting and retire the stream exactly once.
    for (std::size_t i = responseDatagrams.size() - 1; i-- > 0;) {
        if (!client.receiveDatagram(responseDatagrams[i], now + 110, &error)) {
            std::cerr << error.toStdString() << std::endl;
            return false;
        }
    }
    now += 120;
    for (std::size_t attempt = 0;
         attempt < 8 && server.localBidirectionalStreamLimit() == 1;
         ++attempt) {
        if (!transfer(server, client, now, &error) ||
            !transfer(client, server, now + 30, &error)) {
            std::cerr << error.toStdString() << std::endl;
            return false;
        }
        now += 40;
    }

    const SwByteArray responseBytes = client.readStream(0);
    SwHttp3Connection::Response response;
    return requireTrue(server.localBidirectionalStreamLimit() == 2,
                       "MAX_STREAMS was not returned after all retransmitted DATA was ACKed") &&
           requireTrue(SwHttp3Connection::parseResponse(responseBytes, response, &error),
                       "reordered multi-packet response could not be parsed") &&
           requireTrue(response.status == SwByteArray("200") &&
                       response.body.size() == 9000,
                       "reordered response bytes were incomplete") &&
           requireTrue(server.trackedReceiveStreamCount() <= 1 &&
                       server.trackedSendStreamCount() <= 1,
                       "terminal multi-packet stream state was retained") &&
           requireTrue(server.retiredPeerBidirectionalRangeCount() <= 1,
                       "multi-packet stream tombstones did not compact");
}

// A peer RESET before request FIN must free H3 buffers and terminate the
// response side. Conversely, an over-budget partial body makes the server send
// STOP_SENDING + RESET_STREAM immediately, while MAX_STREAMS still waits for
// the peer's resulting RESET_STREAM and the ACK of our reset.
bool testHttp3PartialRequestResetAndImmediateRejection() {
    SwString error;
    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("rstcli01"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("rstsrv01"), serverCid, &error)) {
        return false;
    }
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);
    SwQuicTransportParameters serverParameters = server.localTransportParameters();
    serverParameters.initialMaxStreamsBidi = 1;
    server.applyLocalTransportParameters(serverParameters);
    client.applyPeerTransportParameters(serverParameters);
    server.applyPeerTransportParameters(client.localTransportParameters());

    SwHttp3Server h3Server(&server);
    SwHttpLimits limits;
    limits.maxPendingRequestBytes = 4096;
    h3Server.setLimits(limits);
    std::size_t handlerCalls = 0;
    h3Server.setRequestHandler([&](const SwHttpRequest&) -> SwHttpResponse {
        ++handlerCalls;
        return swHttpTextResponse(200, SwString("unexpected"));
    });

    std::uint64_t now = 9000;
    if (!h3Server.start(&error)) return false;
    SwHttp3Frame::SettingList settings;
    SwByteArray control;
    if (!SwHttp3Connection::buildControlStream(settings, control, &error) ||
        !client.sendStreamData(2, control, false, &error) ||
        !transfer(client, server, now, &error) ||
        !h3Server.pump(&error)) {
        return false;
    }

    SwHttp3Connection::Request request;
    request.method = SwByteArray("POST");
    request.scheme = SwByteArray("https");
    request.authority = SwByteArray("example.test");
    request.path = SwByteArray("/reset-before-fin");
    request.body = SwByteArray(512, 'r');
    SwByteArray encoded;
    if (!SwHttp3Connection::buildRequest(request, encoded, &error)) return false;
    const SwByteArray prefix = encoded.left(static_cast<int>(encoded.size() / 2));
    if (!client.sendStreamData(0, prefix, false, &error) ||
        !transfer(client, server, now + 10, &error) ||
        !h3Server.pump(&error) ||
        !requireTrue(h3Server.pendingRequestBytes() > 0,
                     "partial request was not retained before RESET_STREAM")) {
        return false;
    }

    if (!client.resetStream(0, 0x10c, &error) ||
        !transfer(client, server, now + 20, &error) ||
        !h3Server.pump(&error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(h3Server.pendingRequestBytes() == 0 &&
                     h3Server.requestStreamStateCount() == 0 && handlerCalls == 0,
                     "peer RESET_STREAM retained/routed a partial H3 request") ||
        !requireTrue(server.localBidirectionalStreamLimit() == 1,
                     "peer RESET returned stream credit before response RESET was ACKed")) {
        return false;
    }
    if (!transfer(server, client, now + 30, &error) ||
        !transfer(client, server, now + 60, &error) ||
        !requireTrue(server.localBidirectionalStreamLimit() == 2,
                     "peer RESET stream did not reach the two-sided terminal state") ||
        !transfer(server, client, now + 70, &error)) {
        return false;
    }

    // Stream 4 uses the credit returned above. Reject it while it is still
    // open, then verify that merely ACKing the server RESET is insufficient:
    // the peer must also close its send direction in response to STOP_SENDING.
    limits.maxPendingRequestBytes = 32;
    h3Server.setLimits(limits);
    request.path = SwByteArray("/over-budget-open-body");
    request.body = SwByteArray(512, 'b');
    if (!SwHttp3Connection::buildRequest(request, encoded, &error)) return false;
    const SwByteArray oversizedPrefix = encoded.left(256);
    if (!client.sendStreamData(4, oversizedPrefix, false, &error) ||
        !transfer(client, server, now + 80, &error) ||
        !h3Server.pump(&error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(h3Server.pendingRequestBytes() == 0 &&
                     h3Server.requestStreamStateCount() == 0 && handlerCalls == 0,
                     "over-budget open request was not rejected immediately") ||
        !requireTrue(server.localBidirectionalStreamLimit() == 2,
                     "rejected open request returned credit before terminal RESETs")) {
        return false;
    }

    // Receiving STOP_SENDING makes the client queue RESET_STREAM. Its packet
    // also ACKs the server RESET; only processing both closes the server stream.
    if (!transfer(server, client, now + 90, &error) ||
        !transfer(client, server, now + 120, &error) ||
        !h3Server.pump(&error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(server.localBidirectionalStreamLimit() == 3,
                     "STOP_SENDING/RESET_STREAM exchange did not return credit") ||
        !requireTrue(h3Server.pendingRequestBytes() == 0 && handlerCalls == 0,
                     "rejected request regained budget or reached the handler") ||
        !transfer(server, client, now + 130, &error)) {
        return false;
    }

    // A valid HEADERS frame followed by one byte of an unfinished DATA frame
    // used to be routed when QUIC FIN arrived. It is a truncated final frame
    // and must be rejected before the handler.
    limits.maxPendingRequestBytes = 4096;
    h3Server.setLimits(limits);
    request.method = SwByteArray("GET");
    request.path = SwByteArray("/truncated-frame");
    request.body = SwByteArray();
    if (!SwHttp3Connection::buildRequest(request, encoded, &error)) return false;
    encoded.append(static_cast<char>(0x00)); // DATA type without its length
    if (!client.sendStreamData(8, encoded, true, &error) ||
        !transfer(client, server, now + 140, &error) ||
        !h3Server.pump(&error) ||
        !transfer(server, client, now + 150, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    SwHttp3Connection::Response truncatedResponse;
    const SwByteArray truncatedResponseBytes = client.readStream(8);
    return requireTrue(handlerCalls == 0,
                       "request with a truncated final H3 frame reached the handler") &&
           requireTrue(SwHttp3Connection::parseResponse(
                           truncatedResponseBytes, truncatedResponse, &error) &&
                       truncatedResponse.status == SwByteArray("400"),
                       "truncated final H3 frame was not rejected with 400") &&
           requireTrue(h3Server.completedRequestRangeCount() <= 1 &&
                       server.retiredPeerBidirectionalRangeCount() <= 1,
                       "reset stream tombstones did not compact");
}

// Multipart parsing duplicates in-memory form values (raw body, part data,
// formFields). The expanded representation must participate in both the local
// and driver-global pending-byte budgets before the route handler runs.
bool testHttp3MultipartExpansionUsesPendingBudget() {
    SwString error;
    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("mpcli001"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("mpsrv001"), serverCid, &error)) {
        return false;
    }
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        return false;
    }
    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    SwHttp3Server h3Server(&server);
    SwHttpLimits limits;
    limits.maxPendingRequestBytes = 1024;
    limits.maxPendingRequestBytesGlobal = 1024;
    h3Server.setLimits(limits);
    std::size_t globalPending = 0;
    h3Server.setPendingRequestBudgetHandlers(
        [&](std::size_t bytes) -> bool {
            if (globalPending > limits.maxPendingRequestBytesGlobal ||
                bytes > limits.maxPendingRequestBytesGlobal - globalPending) {
                return false;
            }
            globalPending += bytes;
            return true;
        },
        [&](std::size_t bytes) {
            globalPending = bytes > globalPending ? 0 : globalPending - bytes;
        });
    std::size_t handlerCalls = 0;
    h3Server.setRequestHandler([&](const SwHttpRequest&) -> SwHttpResponse {
        ++handlerCalls;
        return swHttpTextResponse(200, SwString("unexpected"));
    });

    std::uint64_t now = 11000;
    if (!h3Server.start(&error)) return false;
    SwHttp3Frame::SettingList settings;
    SwByteArray control;
    if (!SwHttp3Connection::buildControlStream(settings, control, &error) ||
        !client.sendStreamData(2, control, false, &error) ||
        !transfer(client, server, now, &error) ||
        !h3Server.pump(&error)) {
        return false;
    }

    SwHttp3Connection::Request request;
    request.method = SwByteArray("POST");
    request.scheme = SwByteArray("https");
    request.authority = SwByteArray("example.test");
    request.path = SwByteArray("/multipart-budget");
    request.extraHeaders.push_back(std::make_pair(
        SwByteArray("content-type"),
        SwByteArray("multipart/form-data; boundary=sw-boundary")));
    request.body = SwByteArray("--sw-boundary\r\n"
                               "Content-Disposition: form-data; name=\"field\"\r\n\r\n");
    request.body.append(SwByteArray(600, 'm'));
    request.body.append(SwByteArray("\r\n--sw-boundary--\r\n"));
    SwByteArray encoded;
    if (!SwHttp3Connection::buildRequest(request, encoded, &error) ||
        !requireTrue(encoded.size() < limits.maxPendingRequestBytes,
                     "multipart wire request unexpectedly exceeds pre-parse budget") ||
        !client.sendStreamData(0, encoded, true, &error) ||
        !transfer(client, server, now + 10, &error) ||
        !h3Server.pump(&error) ||
        !transfer(server, client, now + 20, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwHttp3Connection::Response response;
    const SwByteArray responseBytes = client.readStream(0);
    return requireTrue(handlerCalls == 0,
                       "multipart expansion bypassed the pending-byte budget") &&
           requireTrue(h3Server.pendingRequestBytes() == 0 && globalPending == 0,
                       "multipart rejection leaked pending-byte reservations") &&
           requireTrue(SwHttp3Connection::parseResponse(responseBytes, response, &error) &&
                       response.status == SwByteArray("503"),
                       "multipart expansion budget rejection did not return 503");
}

} // namespace

int main() {
    if (!testHttp3RequestResponse() ||
        !testWebTransportConnect() ||
        !testReliableResetPrimitives() ||
        !testHttp3SplitFinRequest() ||
        !testHttp3ControlStreamMissingSettings() ||
        !testHttp3ControlStreamRejectsMalformedSettingsImmediately() ||
        !testHttp3PartialControlFrameUsesPendingBudget() ||
        !testHttp3SequentialStreamCreditAndRequestBudgets() ||
        !testHttp3CreditWaitsForEveryResponseByteAck() ||
        !testHttp3PartialRequestResetAndImmediateRejection() ||
        !testHttp3MultipartExpansionUsesPendingBudget()) {
        return 1;
    }
    std::cout << "Http3ServerSelfTest passed" << std::endl;
    return 0;
}
