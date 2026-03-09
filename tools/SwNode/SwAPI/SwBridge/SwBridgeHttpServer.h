#pragma once

#include "SwObject.h"
#include "SwHttpApp.h"
#include "SwJsonValue.h"
#include "SwString.h"
#include "SwMap.h"
#include "SwByteArray.h"
#include "SwSharedMemorySignal.h"

#include <cstdint>
#include <memory>

class SwTcpServer;
class SwTcpSocket;
class SwTimer;

class SwBridgeHttpServer : public SwObject {
public:
    SwBridgeHttpServer(uint16_t httpPort, SwObject* parent = nullptr);
    ~SwBridgeHttpServer() override;

    bool start();

    void setApiKey(const SwString& apiKey);

private:
    void registerRoutes_();

    // --- REST API handler helpers (called from SwHttpContext routes) ---
    static void sendErrorJson_(SwHttpContext& ctx, int statusCode, const SwString& message);

    // --- WebSocket (on wsPort_ = httpPort_ + 1) ---
    struct WsConnState {
        SwByteArray buffer;
        SwByteArray frag;
        uint8_t fragOpcode{0};
        bool fragActive{false};
        SwString target;
    };

    void onWsNewConnection_();
    void onWsClientData_(SwTcpSocket* client);
    void onWsClientDisconnected_(SwTcpSocket* client);
    void wsSendState_(SwTcpSocket* client);
    void wsBroadcastState_();

    // --- IPC helpers ---
    static bool splitTarget_(const SwString& fqn, SwString& nsOut, SwString& objOut);
    bool ipcSendPing_(const SwString& target, int n, const SwString& s);
    bool ipcSendConfigRaw_(const SwString& target, const SwString& configName, const SwString& value);
    void ensureTargetSubscriptions_(const SwString& target);

    uint16_t httpPort_{0};
    uint16_t wsPort_{0};

    SwHttpApp app_;
    SwTcpServer* wsServer_{nullptr};
    SwMap<SwTcpSocket*, WsConnState> wsConns_;
    SwMap<SwTcpSocket*, SwTimer*> wsPollTimers_;

    SwString apiKey_;
    uint64_t stateSeq_{0};

    SwString subscribedTarget_;
    std::shared_ptr<sw::ipc::Registry> reg_;
    std::shared_ptr<sw::ipc::Signal<int, SwString>> pongSig_;
    std::shared_ptr<sw::ipc::Signal<uint64_t, SwString>> cfgAckSig_;
    sw::ipc::Signal<int, SwString>::Subscription pongSub_;
    sw::ipc::Signal<uint64_t, SwString>::Subscription cfgAckSub_;

    SwString lastPong_;
    SwString lastConfigAck_;
};
