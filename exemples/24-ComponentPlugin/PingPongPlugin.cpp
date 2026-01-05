#include "SwRemoteObjectComponent.h"

#include "SwSharedMemorySignal.h"
#include "SwTimer.h"

#include <iostream>

namespace demo {

class PingComponent : public SwRemoteObject {
 public:
    PingComponent(const SwString& sysName,
                  const SwString& nameSpace,
                  const SwString& objectName,
                  SwObject* parent = nullptr)
        : SwRemoteObject(sysName, nameSpace, objectName, parent),
          timer_(new SwTimer(this)) {
        ipcRegisterConfig(SwString, peer_, "peer", SwString(), [this](const SwString&) { reconnect_(); });
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
                                   std::cout << "[Ping] got pong seq=" << seq
                                             << " msg=" << msg.toStdString() << std::endl;
                               },
                               /*fireInitial=*/false);
        if (!pongConn_) {
            std::cerr << "[Ping] ipcConnect failed (pong)" << std::endl;
        } else {
            std::cout << "[Ping] connected to " << peer_.toStdString() << "#pong" << std::endl;
        }
    }

    void tick_() {
        ++seq_;
        const bool ok = emit ping(seq_, SwString("ping"));
        if (!ok) {
            std::cerr << "[Ping] publish failed (ping)" << std::endl;
            return;
        }
        std::cout << "[Ping] sent ping seq=" << seq_ << std::endl;
    }

    SwString peer_{};
    int periodMs_{1000};
    int seq_{0};
    SwTimer* timer_{nullptr};
    SwObject* pongConn_{nullptr};
};

class PongComponent : public SwRemoteObject {
 public:
    PongComponent(const SwString& sysName,
                  const SwString& nameSpace,
                  const SwString& objectName,
                  SwObject* parent = nullptr)
        : SwRemoteObject(sysName, nameSpace, objectName, parent) {
        ipcRegisterConfig(SwString, peer_, "peer", SwString(), [this](const SwString&) { reconnect_(); });
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
                                   std::cout << "[Pong] got ping seq=" << seq
                                             << " msg=" << msg.toStdString() << std::endl;
                                   const bool ok = emit pong(seq, SwString("pong"));
                                   if (!ok) {
                                       std::cerr << "[Pong] publish failed (pong)" << std::endl;
                                   }
                               },
                               /*fireInitial=*/false);
        if (!pingConn_) {
            std::cerr << "[Pong] ipcConnect failed (ping)" << std::endl;
        } else {
            std::cout << "[Pong] connected to " << peer_.toStdString() << "#ping" << std::endl;
        }
    }

    SwString peer_{};
    SwObject* pingConn_{nullptr};
};

} // namespace demo

SW_REGISTER_COMPONENT_NODE(demo::PingComponent);
SW_REGISTER_COMPONENT_NODE(demo::PongComponent);
