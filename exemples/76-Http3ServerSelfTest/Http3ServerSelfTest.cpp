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
    std::vector<SwByteArray> datagrams;
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

    return requireTrue(response.status == SwByteArray("200"), "response status mismatch") &&
           requireTrue(response.body == SwByteArray("hello from h3"), "response body mismatch") &&
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

    SwHttp3Server h3Server(&server);
    bool sessionRequested = false;
    std::uint64_t sessionStreamId = 0;
    h3Server.setWebTransportHandler([&](const SwHttpRequest& request,
                                        std::uint64_t sessionId) -> bool {
        (void)request;
        sessionRequested = true;
        sessionStreamId = sessionId;
        return true;
    });

    std::uint64_t now = 800;
    if (!requireTrue(h3Server.start(&error), "wt server start failed")) {
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
    if (!requireTrue(client.sendStreamData(0, connectStream, true, &error),
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

} // namespace

int main() {
    if (!testHttp3RequestResponse() ||
        !testWebTransportConnect() ||
        !testHttp3SplitFinRequest() ||
        !testHttp3ControlStreamMissingSettings()) {
        return 1;
    }
    std::cout << "Http3ServerSelfTest passed" << std::endl;
    return 0;
}
