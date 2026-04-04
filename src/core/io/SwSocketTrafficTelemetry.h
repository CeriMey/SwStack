#pragma once

/**
 * @file src/core/io/SwSocketTrafficTelemetry.h
 * @ingroup core_io
 * @brief Socket traffic monitoring primitives aggregated by SwObject consumer.
 */

#include "SwList.h"
#include "SwMap.h"
#include "SwObject.h"
#include "SwRuntimeTelemetry.h"
#include "SwString.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

enum class SwSocketTrafficTransportKind {
    Tcp = 0,
    Udp = 1,
    Tls = 2
};

struct SwSocketTrafficTelemetryConfig {
    long long monitorPeriodUs;
    bool includeTcp;
    bool includeUdp;
    bool includeTlsAppBytes;

    SwSocketTrafficTelemetryConfig()
        : monitorPeriodUs(200000),
          includeTcp(true),
          includeUdp(true),
          includeTlsAppBytes(true) {}
};

struct SwSocketTrafficSocketSnapshot {
    unsigned long long socketId;
    unsigned long long consumerId;
    SwString consumerLabel;
    SwString consumerClassName;
    SwString socketLabel;
    SwString socketClassName;
    SwSocketTrafficTransportKind transportKind;
    SwString transportName;
    SwString localAddress;
    unsigned short localPort;
    SwString peerAddress;
    unsigned short peerPort;
    SwString endpointSummary;
    SwString stateLabel;
    unsigned long long rxRateBytesPerSecond;
    unsigned long long txRateBytesPerSecond;
    unsigned long long totalRateBytesPerSecond;
    unsigned long long rxBytesTotal;
    unsigned long long txBytesTotal;
    unsigned long long totalReceivedDatagrams;
    unsigned long long totalSentDatagrams;
    unsigned long long droppedDatagrams;
    unsigned long long queueHighWatermark;
    unsigned long long pendingDatagramCount;
    long long lastActivityNs;
    bool open;

    SwSocketTrafficSocketSnapshot()
        : socketId(0),
          consumerId(0),
          transportKind(SwSocketTrafficTransportKind::Tcp),
          localPort(0),
          peerPort(0),
          rxRateBytesPerSecond(0),
          txRateBytesPerSecond(0),
          totalRateBytesPerSecond(0),
          rxBytesTotal(0),
          txBytesTotal(0),
          totalReceivedDatagrams(0),
          totalSentDatagrams(0),
          droppedDatagrams(0),
          queueHighWatermark(0),
          pendingDatagramCount(0),
          lastActivityNs(0),
          open(false) {}
};

struct SwSocketTrafficConsumerSnapshot {
    unsigned long long consumerId;
    SwString consumerLabel;
    SwString consumerClassName;
    SwString stateLabel;
    unsigned long long socketCount;
    unsigned long long openSocketCount;
    unsigned long long rxRateBytesPerSecond;
    unsigned long long txRateBytesPerSecond;
    unsigned long long totalRateBytesPerSecond;
    unsigned long long rxBytesTotal;
    unsigned long long txBytesTotal;
    double sharePercentOfTotal;
    long long lastActivityNs;
    SwList<SwSocketTrafficSocketSnapshot> sockets;

    SwSocketTrafficConsumerSnapshot()
        : consumerId(0),
          socketCount(0),
          openSocketCount(0),
          rxRateBytesPerSecond(0),
          txRateBytesPerSecond(0),
          totalRateBytesPerSecond(0),
          rxBytesTotal(0),
          txBytesTotal(0),
          sharePercentOfTotal(0.0),
          lastActivityNs(0) {}
};

struct SwSocketTrafficTotalsSnapshot {
    unsigned long long rxRateBytesPerSecond;
    unsigned long long txRateBytesPerSecond;
    unsigned long long totalRateBytesPerSecond;
    unsigned long long rxBytesTotal;
    unsigned long long txBytesTotal;
    unsigned long long openSocketCount;
    unsigned long long activeSocketCount;
    unsigned long long activeConsumerCount;
    unsigned long long topConsumerId;
    SwString topConsumerLabel;
    double topConsumerSharePercent;

    SwSocketTrafficTotalsSnapshot()
        : rxRateBytesPerSecond(0),
          txRateBytesPerSecond(0),
          totalRateBytesPerSecond(0),
          rxBytesTotal(0),
          txBytesTotal(0),
          openSocketCount(0),
          activeSocketCount(0),
          activeConsumerCount(0),
          topConsumerId(0),
          topConsumerSharePercent(0.0) {}
};

struct SwSocketTrafficSample {
    long long sampleTimeNs;
    bool baselineResetApplied;
    SwRuntimeIterationSnapshot runtimeIteration;
    SwSocketTrafficTotalsSnapshot totals;
    SwList<SwSocketTrafficConsumerSnapshot> consumers;
    SwList<SwSocketTrafficSocketSnapshot> sockets;

    SwSocketTrafficSample()
        : sampleTimeNs(0),
          baselineResetApplied(false) {}
};

class SwSocketTrafficTelemetrySink {
public:
    virtual ~SwSocketTrafficTelemetrySink() {}
    virtual void onSocketTrafficSample(const SwSocketTrafficSample& sample) = 0;
};

namespace sw_socket_traffic_monitor {

struct SocketState_ {
    SwObject* socketObject;
    std::atomic<unsigned long long> rxBytesTotal;
    std::atomic<unsigned long long> txBytesTotal;
    std::atomic<unsigned long long> rxDatagramsTotal;
    std::atomic<unsigned long long> txDatagramsTotal;
    std::atomic<unsigned long long> droppedDatagrams;
    std::atomic<unsigned long long> queueHighWatermark;
    std::atomic<unsigned long long> pendingDatagramCount;
    std::atomic<long long> lastActivityNs;
    std::atomic<bool> open;
    mutable std::mutex metaMutex;
    SwSocketTrafficTransportKind transportKind;
    SwString socketClassName;
    SwString localAddress;
    unsigned short localPort;
    SwString peerAddress;
    unsigned short peerPort;

    explicit SocketState_(SwObject* socket)
        : socketObject(socket),
          rxBytesTotal(0),
          txBytesTotal(0),
          rxDatagramsTotal(0),
          txDatagramsTotal(0),
          droppedDatagrams(0),
          queueHighWatermark(0),
          pendingDatagramCount(0),
          lastActivityNs(0),
          open(false),
          transportKind(SwSocketTrafficTransportKind::Tcp),
          localPort(0),
          peerPort(0) {}
};

struct SocketBaseline_ {
    unsigned long long rxBytesTotal;
    unsigned long long txBytesTotal;

    SocketBaseline_()
        : rxBytesTotal(0),
          txBytesTotal(0) {}
};

inline long long nowNs_() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline std::mutex& registryMutex_() {
    static std::mutex mutex;
    return mutex;
}

inline SwMap<SwObject*, std::shared_ptr<SocketState_>>& registry_() {
    static SwMap<SwObject*, std::shared_ptr<SocketState_>> states;
    return states;
}

inline bool isSocketHierarchyName_(const SwString& name) {
    return name == "SwAbstractSocket" ||
           name == "SwTcpSocket" ||
           name == "SwSslSocket" ||
           name == "SwUdpSocket";
}

inline bool isSocketObject_(const SwObject* object) {
    if (!object) {
        return false;
    }
    const SwList<SwString> hierarchy = object->classHierarchy();
    for (size_t i = 0; i < hierarchy.size(); ++i) {
        if (isSocketHierarchyName_(hierarchy[i])) {
            return true;
        }
    }
    return false;
}

inline SwString transportName_(SwSocketTrafficTransportKind kind) {
    switch (kind) {
    case SwSocketTrafficTransportKind::Tcp:
        return "TCP";
    case SwSocketTrafficTransportKind::Udp:
        return "UDP";
    case SwSocketTrafficTransportKind::Tls:
        return "TLS";
    }
    return "TCP";
}

inline SwString endpointSummary_(const SwString& localAddress,
                                 unsigned short localPort,
                                 const SwString& peerAddress,
                                 unsigned short peerPort) {
    SwString local;
    SwString peer;
    if (!localAddress.isEmpty() || localPort != 0) {
        local = localAddress.isEmpty() ? SwString("*") : localAddress;
        if (localPort != 0) {
            local += ":" + SwString::number(localPort);
        }
    }
    if (!peerAddress.isEmpty() || peerPort != 0) {
        peer = peerAddress.isEmpty() ? SwString("*") : peerAddress;
        if (peerPort != 0) {
            peer += ":" + SwString::number(peerPort);
        }
    }
    if (!local.isEmpty() && !peer.isEmpty()) {
        return local + " -> " + peer;
    }
    if (!peer.isEmpty()) {
        return peer;
    }
    return local;
}

inline SwString preferredObjectLabel_(SwObject* object) {
    if (!object) {
        return SwString();
    }
    if (!object->getObjectName().isEmpty()) {
        return object->getObjectName();
    }
    if (!object->className().isEmpty()) {
        return object->className();
    }
    return SwString();
}

inline SwString objectClassName_(SwObject* object) {
    if (!object) {
        return SwString("Unknown");
    }
    if (!object->className().isEmpty()) {
        return object->className();
    }
    return SwString("Unknown");
}

inline unsigned long long objectId_(SwObject* object) {
    return static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(object));
}

inline std::shared_ptr<SocketState_> ensureState_(SwObject* socketObject) {
    if (!socketObject) {
        return std::shared_ptr<SocketState_>();
    }

    std::lock_guard<std::mutex> lock(registryMutex_());
    SwMap<SwObject*, std::shared_ptr<SocketState_>>& states = registry_();
    if (states.contains(socketObject)) {
        return states[socketObject];
    }

    std::shared_ptr<SocketState_> state(new SocketState_(socketObject));
    {
        std::lock_guard<std::mutex> metaLock(state->metaMutex);
        state->socketClassName = socketObject->className();
    }
    states[socketObject] = state;
    return state;
}

inline std::shared_ptr<SocketState_> registerSocket(SwObject* socketObject, SwSocketTrafficTransportKind kind) {
    std::shared_ptr<SocketState_> state = ensureState_(socketObject);
    if (!state) {
        return std::shared_ptr<SocketState_>();
    }
    state->open.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> metaLock(state->metaMutex);
        state->transportKind = kind;
        if (socketObject) {
            state->socketClassName = socketObject->className();
        }
    }
    return state;
}

inline void unregisterSocket(SwObject* socketObject) {
    if (!socketObject) {
        return;
    }
    std::lock_guard<std::mutex> lock(registryMutex_());
    registry_().remove(socketObject);
}

inline void setTransportKind(const std::shared_ptr<SocketState_>& state, SwSocketTrafficTransportKind kind) {
    if (!state) {
        return;
    }
    std::lock_guard<std::mutex> metaLock(state->metaMutex);
    state->transportKind = kind;
}

inline void setTransportKind(SwObject* socketObject, SwSocketTrafficTransportKind kind) {
    setTransportKind(ensureState_(socketObject), kind);
}

inline void setOpenState(const std::shared_ptr<SocketState_>& state, bool open) {
    if (!state) {
        return;
    }
    state->open.store(open, std::memory_order_relaxed);
}

inline void setOpenState(SwObject* socketObject, bool open) {
    std::shared_ptr<SocketState_> state = ensureState_(socketObject);
    if (!state) {
        return;
    }
    setOpenState(state, open);
}

inline void updateEndpoints(const std::shared_ptr<SocketState_>& state,
                            const SwString& localAddress,
                            unsigned short localPort,
                            const SwString& peerAddress,
                            unsigned short peerPort) {
    if (!state) {
        return;
    }
    std::lock_guard<std::mutex> metaLock(state->metaMutex);
    state->localAddress = localAddress;
    state->localPort = localPort;
    state->peerAddress = peerAddress;
    state->peerPort = peerPort;
}

inline void updateEndpoints(SwObject* socketObject,
                            const SwString& localAddress,
                            unsigned short localPort,
                            const SwString& peerAddress,
                            unsigned short peerPort) {
    updateEndpoints(ensureState_(socketObject), localAddress, localPort, peerAddress, peerPort);
}

inline void addReceivedBytes(const std::shared_ptr<SocketState_>& state, unsigned long long bytes) {
    if (!state || bytes == 0) {
        return;
    }
    state->rxBytesTotal.fetch_add(bytes, std::memory_order_relaxed);
    state->lastActivityNs.store(nowNs_(), std::memory_order_relaxed);
}

inline void addReceivedBytes(SwObject* socketObject, unsigned long long bytes) {
    addReceivedBytes(ensureState_(socketObject), bytes);
}

inline void addSentBytes(const std::shared_ptr<SocketState_>& state, unsigned long long bytes) {
    if (!state || bytes == 0) {
        return;
    }
    state->txBytesTotal.fetch_add(bytes, std::memory_order_relaxed);
    state->lastActivityNs.store(nowNs_(), std::memory_order_relaxed);
}

inline void addSentBytes(SwObject* socketObject, unsigned long long bytes) {
    addSentBytes(ensureState_(socketObject), bytes);
}

inline void updateUdpStats(const std::shared_ptr<SocketState_>& state,
                           unsigned long long receivedDatagrams,
                           unsigned long long sentDatagrams,
                           unsigned long long droppedDatagrams,
                           unsigned long long queueHighWatermark,
                           unsigned long long pendingDatagramCount) {
    if (!state) {
        return;
    }
    state->rxDatagramsTotal.store(receivedDatagrams, std::memory_order_relaxed);
    state->txDatagramsTotal.store(sentDatagrams, std::memory_order_relaxed);
    state->droppedDatagrams.store(droppedDatagrams, std::memory_order_relaxed);
    state->queueHighWatermark.store(queueHighWatermark, std::memory_order_relaxed);
    state->pendingDatagramCount.store(pendingDatagramCount, std::memory_order_relaxed);
}

inline void updateUdpStats(SwObject* socketObject,
                           unsigned long long receivedDatagrams,
                           unsigned long long sentDatagrams,
                           unsigned long long droppedDatagrams,
                           unsigned long long queueHighWatermark,
                           unsigned long long pendingDatagramCount) {
    updateUdpStats(ensureState_(socketObject),
                   receivedDatagrams,
                   sentDatagrams,
                   droppedDatagrams,
                   queueHighWatermark,
                   pendingDatagramCount);
}

inline SwObject* resolveConsumerObject_(SwObject* socketObject) {
    SwObject* current = socketObject ? socketObject->parent() : nullptr;
    while (current) {
        if (!isSocketObject_(current)) {
            return current;
        }
        current = current->parent();
    }
    return socketObject;
}

inline SwList<std::shared_ptr<SocketState_>> registrySnapshot_() {
    SwList<std::shared_ptr<SocketState_>> snapshot;
    std::lock_guard<std::mutex> lock(registryMutex_());
    for (SwMap<SwObject*, std::shared_ptr<SocketState_>>::const_iterator it = registry_().begin();
         it != registry_().end();
         ++it) {
        snapshot.append(it.value());
    }
    return snapshot;
}

inline bool transportEnabled_(SwSocketTrafficTransportKind kind, const SwSocketTrafficTelemetryConfig& config) {
    if (kind == SwSocketTrafficTransportKind::Udp) {
        return config.includeUdp;
    }
    if (kind == SwSocketTrafficTransportKind::Tls) {
        return config.includeTlsAppBytes;
    }
    return config.includeTcp;
}

inline SwString composeSocketLabel_(SwObject* socketObject,
                                    const SwString& socketClassName,
                                    SwSocketTrafficTransportKind kind,
                                    const SwString& localAddress,
                                    unsigned short localPort,
                                    const SwString& peerAddress,
                                    unsigned short peerPort) {
    SwString label = preferredObjectLabel_(socketObject);
    if (label.isEmpty()) {
        label = socketClassName.isEmpty() ? transportName_(kind) + " socket" : socketClassName;
    }
    const SwString endpoint = endpointSummary_(localAddress, localPort, peerAddress, peerPort);
    if (!endpoint.isEmpty() && endpoint != label) {
        return label + "  " + endpoint;
    }
    return label;
}

inline SwString stateLabel_(bool open, unsigned long long totalRateBytesPerSecond) {
    if (!open) {
        return "closed";
    }
    if (totalRateBytesPerSecond > 0) {
        return "active";
    }
    return "idle";
}

} // namespace sw_socket_traffic_monitor

using SwSocketTrafficStateHandle = std::shared_ptr<sw_socket_traffic_monitor::SocketState_>;

inline SwSocketTrafficStateHandle swSocketTrafficRegisterSocket(SwObject* socketObject, SwSocketTrafficTransportKind kind) {
    return sw_socket_traffic_monitor::registerSocket(socketObject, kind);
}

inline void swSocketTrafficUnregisterSocket(SwObject* socketObject) {
    sw_socket_traffic_monitor::unregisterSocket(socketObject);
}

inline void swSocketTrafficSetTransportKind(const SwSocketTrafficStateHandle& state, SwSocketTrafficTransportKind kind) {
    sw_socket_traffic_monitor::setTransportKind(state, kind);
}

inline void swSocketTrafficSetTransportKind(SwObject* socketObject, SwSocketTrafficTransportKind kind) {
    sw_socket_traffic_monitor::setTransportKind(socketObject, kind);
}

inline void swSocketTrafficSetOpenState(const SwSocketTrafficStateHandle& state, bool open) {
    sw_socket_traffic_monitor::setOpenState(state, open);
}

inline void swSocketTrafficSetOpenState(SwObject* socketObject, bool open) {
    sw_socket_traffic_monitor::setOpenState(socketObject, open);
}

inline void swSocketTrafficUpdateEndpoints(const SwSocketTrafficStateHandle& state,
                                           const SwString& localAddress,
                                           unsigned short localPort,
                                           const SwString& peerAddress,
                                           unsigned short peerPort) {
    sw_socket_traffic_monitor::updateEndpoints(state, localAddress, localPort, peerAddress, peerPort);
}

inline void swSocketTrafficUpdateEndpoints(SwObject* socketObject,
                                           const SwString& localAddress,
                                           unsigned short localPort,
                                           const SwString& peerAddress,
                                           unsigned short peerPort) {
    sw_socket_traffic_monitor::updateEndpoints(socketObject, localAddress, localPort, peerAddress, peerPort);
}

inline void swSocketTrafficAddReceivedBytes(const SwSocketTrafficStateHandle& state, unsigned long long bytes) {
    sw_socket_traffic_monitor::addReceivedBytes(state, bytes);
}

inline void swSocketTrafficAddReceivedBytes(SwObject* socketObject, unsigned long long bytes) {
    sw_socket_traffic_monitor::addReceivedBytes(socketObject, bytes);
}

inline void swSocketTrafficAddSentBytes(const SwSocketTrafficStateHandle& state, unsigned long long bytes) {
    sw_socket_traffic_monitor::addSentBytes(state, bytes);
}

inline void swSocketTrafficAddSentBytes(SwObject* socketObject, unsigned long long bytes) {
    sw_socket_traffic_monitor::addSentBytes(socketObject, bytes);
}

inline void swSocketTrafficUpdateUdpStats(const SwSocketTrafficStateHandle& state,
                                          unsigned long long receivedDatagrams,
                                          unsigned long long sentDatagrams,
                                          unsigned long long droppedDatagrams,
                                          unsigned long long queueHighWatermark,
                                          unsigned long long pendingDatagramCount) {
    sw_socket_traffic_monitor::updateUdpStats(state,
                                              receivedDatagrams,
                                              sentDatagrams,
                                              droppedDatagrams,
                                              queueHighWatermark,
                                              pendingDatagramCount);
}

inline void swSocketTrafficUpdateUdpStats(SwObject* socketObject,
                                          unsigned long long receivedDatagrams,
                                          unsigned long long sentDatagrams,
                                          unsigned long long droppedDatagrams,
                                          unsigned long long queueHighWatermark,
                                          unsigned long long pendingDatagramCount) {
    sw_socket_traffic_monitor::updateUdpStats(socketObject,
                                              receivedDatagrams,
                                              sentDatagrams,
                                              droppedDatagrams,
                                              queueHighWatermark,
                                              pendingDatagramCount);
}

class SwSocketTrafficTelemetrySession : public SwRuntimeTelemetrySession {
public:
    explicit SwSocketTrafficTelemetrySession(SwSocketTrafficTelemetrySink* sink,
                                             const SwSocketTrafficTelemetryConfig& config = SwSocketTrafficTelemetryConfig())
        : sink_(sink),
          config_(config) {}

    void onAttached(SwCoreApplication* app) override {
        app_ = app;
    }

    void onDetached() override {
        app_ = nullptr;
        lastSampleTimeNs_ = 0;
        pendingBaselineReset_ = true;
        baselines_.clear();
    }

    void bindToCurrentThread() override {}

    void updateIterationSnapshot(const SwRuntimeIterationSnapshot& snapshot) override {
        if (!enabled_ || !sink_) {
            return;
        }

        const long long nowNs = sw_socket_traffic_monitor::nowNs_();
        if (!pendingBaselineReset_ &&
            lastSampleTimeNs_ != 0 &&
            config_.monitorPeriodUs > 0 &&
            (nowNs - lastSampleTimeNs_) < (config_.monitorPeriodUs * 1000LL)) {
            return;
        }

        SwSocketTrafficSample sample;
        sample.sampleTimeNs = nowNs;
        sample.runtimeIteration = snapshot;
        sample.baselineResetApplied = pendingBaselineReset_ || (lastSampleTimeNs_ == 0);

        buildSample_(sample);
        sink_->onSocketTrafficSample(sample);
    }

    void setEnabled(bool enabled) {
        enabled_ = enabled;
        if (enabled_) {
            pendingBaselineReset_ = true;
        }
    }

    bool enabled() const {
        return enabled_;
    }

    void resetBaselines() {
        pendingBaselineReset_ = true;
    }

    void setMonitorPeriodUs(long long monitorPeriodUs) {
        config_.monitorPeriodUs = std::max(0LL, monitorPeriodUs);
        pendingBaselineReset_ = true;
    }

    long long monitorPeriodUs() const {
        return config_.monitorPeriodUs;
    }

    const SwSocketTrafficTelemetryConfig& config() const {
        return config_;
    }

private:
    void buildSample_(SwSocketTrafficSample& sample) {
        const SwList<std::shared_ptr<sw_socket_traffic_monitor::SocketState_>> states =
            sw_socket_traffic_monitor::registrySnapshot_();
        SwMap<const void*, sw_socket_traffic_monitor::SocketBaseline_> nextBaselines;

        const bool zeroRates = pendingBaselineReset_ || lastSampleTimeNs_ == 0;
        const double deltaSeconds =
            (zeroRates || sample.sampleTimeNs <= lastSampleTimeNs_)
                ? 0.0
                : static_cast<double>(sample.sampleTimeNs - lastSampleTimeNs_) / 1000000000.0;

        SwMap<unsigned long long, std::size_t> consumerIndexes;

        for (size_t i = 0; i < states.size(); ++i) {
            const std::shared_ptr<sw_socket_traffic_monitor::SocketState_>& state = states[i];
            if (!state || !state->socketObject || !SwObject::isLive(state->socketObject)) {
                continue;
            }

            SwSocketTrafficTransportKind transportKind = SwSocketTrafficTransportKind::Tcp;
            SwString socketClassName;
            SwString localAddress;
            unsigned short localPort = 0;
            SwString peerAddress;
            unsigned short peerPort = 0;
            {
                std::lock_guard<std::mutex> metaLock(state->metaMutex);
                transportKind = state->transportKind;
                socketClassName = state->socketClassName;
                localAddress = state->localAddress;
                localPort = state->localPort;
                peerAddress = state->peerAddress;
                peerPort = state->peerPort;
            }

            if (!sw_socket_traffic_monitor::transportEnabled_(transportKind, config_)) {
                continue;
            }

            const unsigned long long rxBytesTotal = state->rxBytesTotal.load(std::memory_order_relaxed);
            const unsigned long long txBytesTotal = state->txBytesTotal.load(std::memory_order_relaxed);
            const unsigned long long receivedDatagrams = state->rxDatagramsTotal.load(std::memory_order_relaxed);
            const unsigned long long sentDatagrams = state->txDatagramsTotal.load(std::memory_order_relaxed);
            const unsigned long long droppedDatagrams = state->droppedDatagrams.load(std::memory_order_relaxed);
            const unsigned long long queueHighWatermark = state->queueHighWatermark.load(std::memory_order_relaxed);
            const unsigned long long pendingDatagramCount = state->pendingDatagramCount.load(std::memory_order_relaxed);
            const long long lastActivityNs = state->lastActivityNs.load(std::memory_order_relaxed);
            const bool open = state->open.load(std::memory_order_relaxed);

            sw_socket_traffic_monitor::SocketBaseline_ previous;
            if (baselines_.contains(state->socketObject)) {
                previous = baselines_[state->socketObject];
            }

            sw_socket_traffic_monitor::SocketBaseline_ next;
            next.rxBytesTotal = rxBytesTotal;
            next.txBytesTotal = txBytesTotal;
            nextBaselines[state->socketObject] = next;

            unsigned long long rxRate = 0;
            unsigned long long txRate = 0;
            if (deltaSeconds > 0.0) {
                const unsigned long long rxDelta =
                    rxBytesTotal >= previous.rxBytesTotal ? (rxBytesTotal - previous.rxBytesTotal) : rxBytesTotal;
                const unsigned long long txDelta =
                    txBytesTotal >= previous.txBytesTotal ? (txBytesTotal - previous.txBytesTotal) : txBytesTotal;
                rxRate = static_cast<unsigned long long>(static_cast<double>(rxDelta) / deltaSeconds);
                txRate = static_cast<unsigned long long>(static_cast<double>(txDelta) / deltaSeconds);
            }

            SwObject* consumerObject = sw_socket_traffic_monitor::resolveConsumerObject_(state->socketObject);
            const unsigned long long consumerId = sw_socket_traffic_monitor::objectId_(consumerObject);
            SwString consumerLabel = sw_socket_traffic_monitor::preferredObjectLabel_(consumerObject);
            const SwString endpointSummary =
                sw_socket_traffic_monitor::endpointSummary_(localAddress, localPort, peerAddress, peerPort);
            if (consumerLabel.isEmpty()) {
                consumerLabel = endpointSummary.isEmpty()
                                    ? sw_socket_traffic_monitor::transportName_(transportKind) + " consumer"
                                    : endpointSummary;
            }

            SwSocketTrafficSocketSnapshot socketSnapshot;
            socketSnapshot.socketId = sw_socket_traffic_monitor::objectId_(state->socketObject);
            socketSnapshot.consumerId = consumerId;
            socketSnapshot.consumerLabel = consumerLabel;
            socketSnapshot.consumerClassName = sw_socket_traffic_monitor::objectClassName_(consumerObject);
            socketSnapshot.socketClassName = socketClassName.isEmpty()
                                                 ? sw_socket_traffic_monitor::objectClassName_(state->socketObject)
                                                 : socketClassName;
            socketSnapshot.transportKind = transportKind;
            socketSnapshot.transportName = sw_socket_traffic_monitor::transportName_(transportKind);
            socketSnapshot.localAddress = localAddress;
            socketSnapshot.localPort = localPort;
            socketSnapshot.peerAddress = peerAddress;
            socketSnapshot.peerPort = peerPort;
            socketSnapshot.endpointSummary = endpointSummary;
            socketSnapshot.rxRateBytesPerSecond = rxRate;
            socketSnapshot.txRateBytesPerSecond = txRate;
            socketSnapshot.totalRateBytesPerSecond = rxRate + txRate;
            socketSnapshot.rxBytesTotal = rxBytesTotal;
            socketSnapshot.txBytesTotal = txBytesTotal;
            socketSnapshot.totalReceivedDatagrams = receivedDatagrams;
            socketSnapshot.totalSentDatagrams = sentDatagrams;
            socketSnapshot.droppedDatagrams = droppedDatagrams;
            socketSnapshot.queueHighWatermark = queueHighWatermark;
            socketSnapshot.pendingDatagramCount = pendingDatagramCount;
            socketSnapshot.lastActivityNs = lastActivityNs;
            socketSnapshot.open = open;
            socketSnapshot.stateLabel =
                sw_socket_traffic_monitor::stateLabel_(open, socketSnapshot.totalRateBytesPerSecond);
            socketSnapshot.socketLabel = sw_socket_traffic_monitor::composeSocketLabel_(state->socketObject,
                                                                                        socketSnapshot.socketClassName,
                                                                                        transportKind,
                                                                                        localAddress,
                                                                                        localPort,
                                                                                        peerAddress,
                                                                                        peerPort);

            sample.sockets.append(socketSnapshot);
            sample.totals.rxRateBytesPerSecond += socketSnapshot.rxRateBytesPerSecond;
            sample.totals.txRateBytesPerSecond += socketSnapshot.txRateBytesPerSecond;
            sample.totals.rxBytesTotal += socketSnapshot.rxBytesTotal;
            sample.totals.txBytesTotal += socketSnapshot.txBytesTotal;
            if (open) {
                ++sample.totals.openSocketCount;
            }
            if (socketSnapshot.totalRateBytesPerSecond > 0) {
                ++sample.totals.activeSocketCount;
            }

            std::size_t consumerIndex = 0;
            if (consumerIndexes.contains(consumerId)) {
                consumerIndex = consumerIndexes[consumerId];
            } else {
                consumerIndex = sample.consumers.size();
                consumerIndexes[consumerId] = consumerIndex;
                SwSocketTrafficConsumerSnapshot consumerSnapshot;
                consumerSnapshot.consumerId = consumerId;
                consumerSnapshot.consumerLabel = consumerLabel;
                consumerSnapshot.consumerClassName =
                    sw_socket_traffic_monitor::objectClassName_(consumerObject);
                sample.consumers.append(consumerSnapshot);
            }

            SwSocketTrafficConsumerSnapshot& consumer = sample.consumers[consumerIndex];
            consumer.socketCount += 1;
            if (open) {
                consumer.openSocketCount += 1;
            }
            consumer.rxRateBytesPerSecond += socketSnapshot.rxRateBytesPerSecond;
            consumer.txRateBytesPerSecond += socketSnapshot.txRateBytesPerSecond;
            consumer.totalRateBytesPerSecond += socketSnapshot.totalRateBytesPerSecond;
            consumer.rxBytesTotal += socketSnapshot.rxBytesTotal;
            consumer.txBytesTotal += socketSnapshot.txBytesTotal;
            consumer.lastActivityNs = std::max(consumer.lastActivityNs, socketSnapshot.lastActivityNs);
            consumer.sockets.append(socketSnapshot);
        }

        sample.totals.totalRateBytesPerSecond =
            sample.totals.rxRateBytesPerSecond + sample.totals.txRateBytesPerSecond;
        sample.totals.activeConsumerCount = static_cast<unsigned long long>(sample.consumers.size());

        for (size_t i = 0; i < sample.consumers.size(); ++i) {
            SwSocketTrafficConsumerSnapshot& consumer = sample.consumers[i];
            consumer.sharePercentOfTotal =
                sample.totals.totalRateBytesPerSecond == 0
                    ? 0.0
                    : (100.0 * static_cast<double>(consumer.totalRateBytesPerSecond) /
                       static_cast<double>(sample.totals.totalRateBytesPerSecond));
            consumer.stateLabel = sw_socket_traffic_monitor::stateLabel_(consumer.openSocketCount > 0,
                                                                         consumer.totalRateBytesPerSecond);
            std::sort(consumer.sockets.begin(),
                      consumer.sockets.end(),
                      [](const SwSocketTrafficSocketSnapshot& lhs, const SwSocketTrafficSocketSnapshot& rhs) {
                          if (lhs.totalRateBytesPerSecond == rhs.totalRateBytesPerSecond) {
                              return lhs.socketLabel < rhs.socketLabel;
                          }
                          return lhs.totalRateBytesPerSecond > rhs.totalRateBytesPerSecond;
                      });
        }

        std::sort(sample.consumers.begin(),
                  sample.consumers.end(),
                  [](const SwSocketTrafficConsumerSnapshot& lhs, const SwSocketTrafficConsumerSnapshot& rhs) {
                      if (lhs.totalRateBytesPerSecond == rhs.totalRateBytesPerSecond) {
                          return lhs.consumerLabel < rhs.consumerLabel;
                      }
                      return lhs.totalRateBytesPerSecond > rhs.totalRateBytesPerSecond;
                  });

        std::sort(sample.sockets.begin(),
                  sample.sockets.end(),
                  [](const SwSocketTrafficSocketSnapshot& lhs, const SwSocketTrafficSocketSnapshot& rhs) {
                      if (lhs.totalRateBytesPerSecond == rhs.totalRateBytesPerSecond) {
                          return lhs.socketLabel < rhs.socketLabel;
                      }
                      return lhs.totalRateBytesPerSecond > rhs.totalRateBytesPerSecond;
                  });

        if (!sample.consumers.isEmpty()) {
            sample.totals.topConsumerId = sample.consumers[0].consumerId;
            sample.totals.topConsumerLabel = sample.consumers[0].consumerLabel;
            sample.totals.topConsumerSharePercent = sample.consumers[0].sharePercentOfTotal;
        }

        baselines_ = nextBaselines;
        lastSampleTimeNs_ = sample.sampleTimeNs;
        pendingBaselineReset_ = false;
    }

    SwSocketTrafficTelemetrySink* sink_{nullptr};
    SwCoreApplication* app_{nullptr};
    SwSocketTrafficTelemetryConfig config_{};
    bool enabled_{true};
    bool pendingBaselineReset_{true};
    long long lastSampleTimeNs_{0};
    SwMap<const void*, sw_socket_traffic_monitor::SocketBaseline_> baselines_;
};

using SwSocketTrafficMonitorConfig = SwSocketTrafficTelemetryConfig;
using SwSocketTrafficMonitorSink = SwSocketTrafficTelemetrySink;
using SwSocketTrafficMonitorSession = SwSocketTrafficTelemetrySession;
using SwSocketTrafficTelemetrySocketSnapshot = SwSocketTrafficSocketSnapshot;
using SwSocketTrafficTelemetryConsumerSnapshot = SwSocketTrafficConsumerSnapshot;
using SwSocketTrafficTelemetryTotalsSnapshot = SwSocketTrafficTotalsSnapshot;
using SwSocketTrafficTelemetrySample = SwSocketTrafficSample;
