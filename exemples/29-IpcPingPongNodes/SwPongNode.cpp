#include "SwRemoteObjectNode.h"
#include "SwSharedMemorySignal.h"

class SwPongNode : public SwRemoteObject {
 public:
    SwPongNode(const SwString& sysName,
               const SwString& nameSpace,
               const SwString& objectName,
               SwObject* parent = nullptr)
        : SwRemoteObject(sysName, nameSpace, objectName, parent) {
        ipcRegisterConfig(SwString, peer_, "peer", SwString("node/ping"), [this](const SwString&) { reconnect_(); });
        reconnect_();
    }

 private:
    SW_REGISTER_SHM_SIGNAL(pong, int, SwString);

    void reconnect_() {
        if (pingConn_) {
            delete pingConn_;
            pingConn_ = nullptr;
        }
        if (peer_.isEmpty()) return;

        pingConn_ = ipcConnect(peer_, "ping", this,
                               [this](int seq, SwString msg) {
                                   swDebug() << "[SwPongNode] got ping seq=" << seq << " msg=" << msg;
                                   const bool ok = emit pong(seq, SwString("pong"));
                                   if (!ok) {
                                       swWarning() << "[SwPongNode] publish failed (pong)";
                                   }
                               },
                               /*fireInitial=*/false);
        if (!pingConn_) {
            swWarning() << "[SwPongNode] ipcConnect failed (ping)";
        } else {
            swDebug() << "[SwPongNode] connected to " << peer_ << "#ping";
        }
    }

    SwString peer_{};
    SwObject* pingConn_{nullptr};
};

SW_REMOTE_OBJECT_NODE(SwPongNode)
