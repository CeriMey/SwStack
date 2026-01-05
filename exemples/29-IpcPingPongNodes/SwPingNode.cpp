#include "SwRemoteObjectNode.h"
#include "SwSharedMemorySignal.h"

class SwPingNode : public SwRemoteObject {
 public:
    SwPingNode(const SwString& sysName,
               const SwString& nameSpace,
               const SwString& objectName,
               SwObject* parent = nullptr)
        : SwRemoteObject(sysName, nameSpace, objectName, parent),
          timer_(new SwTimer(this)) {
        ipcRegisterConfig(SwString, peer_, "peer", SwString("node/pong"), [this](const SwString&) { reconnect_(); });
        ipcRegisterConfig(int, periodMs_, "period_ms", 1000, [this](const int&) { retime_(); });

        SwObject::connect(timer_, &SwTimer::timeout, [this]() { tick_(); });
        retime_();
        reconnect_();
    }

 private:
    SW_REGISTER_SHM_SIGNAL(ping, int, SwString);

    void retime_() {
        if (!timer_) return;
        timer_->stop();
        timer_->start(periodMs_);
    }

    void reconnect_() {
        if (pongConn_) {
            delete pongConn_;
            pongConn_ = nullptr;
        }
        if (peer_.isEmpty()) return;

        pongConn_ = ipcConnect(peer_, "pong", this,
                               [this](int seq, SwString msg) {
                                   swDebug() << "[SwPingNode] got pong seq=" << seq << " msg=" << msg;
                               },
                               /*fireInitial=*/false);
        if (!pongConn_) {
            swWarning() << "[SwPingNode] ipcConnect failed (pong)";
        } else {
            swDebug() << "[SwPingNode] connected to " << peer_ << "#pong";
        }
    }

    void tick_() {
        ++seq_;
        const bool ok = emit ping(seq_, SwString("ping"));
        if (!ok) {
            swWarning() << "[SwPingNode] publish failed (ping)";
            return;
        }
        swDebug() << "[SwPingNode] sent ping seq=" << seq_;
    }

    SwString peer_{};
    int periodMs_{1000};
    int seq_{0};
    SwTimer* timer_{nullptr};
    SwObject* pongConn_{nullptr};
};

SW_REMOTE_OBJECT_NODE(SwPingNode)
