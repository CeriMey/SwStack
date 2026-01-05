#pragma once

#include "Export.hpp"

#include "SwizioNodes/internal/ConnectionPainter.hpp"
#include "SwizioNodes/internal/Definitions.hpp"
#include "SwizioNodes/internal/NodeGraphicsObject.hpp"

#include "graphics/SwGraphicsItem.h"
#include "graphics/SwGraphicsRenderContext.h"
#include "graphics/SwGraphicsTypes.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace SwizioNodes {

class BasicGraphicsScene;

class SWIZIO_NODES_PUBLIC ConnectionGraphicsObject : public SwGraphicsItem
{
public:
    ConnectionGraphicsObject(BasicGraphicsScene& scene, ConnectionId const connectionId)
        : m_connectionId(connectionId)
        , m_scene(&scene) {
        setFlags(ItemIsSelectable);
        setZValue(-10.0);
    }

    ~ConnectionGraphicsObject() override = default;

    ConnectionId const& connectionId() const { return m_connectionId; }

    BasicGraphicsScene* nodeScene() const { return m_scene; }

    void setOutEndpoint(NodeGraphicsObject* node, int port) {
        m_outNode = node;
        m_outPort = port;
        m_hasSceneEndPoint = false;
        update();
    }

    void setInEndpoint(NodeGraphicsObject* node, int port) {
        m_inNode = node;
        m_inPort = port;
        m_hasSceneEndPoint = false;
        update();
    }

    void setSceneEndPoint(const SwPointF& scenePos) {
        m_sceneEndPoint = scenePos;
        m_hasSceneEndPoint = true;
        update();
    }

    bool isComplete() const { return m_outNode && m_inNode; }

    NodeGraphicsObject* outNode() const { return m_outNode; }
    NodeGraphicsObject* inNode() const { return m_inNode; }
    int outPort() const { return m_outPort; }
    int inPort() const { return m_inPort; }

    void setHovered(bool on) {
        if (m_hovered == on) {
            return;
        }
        m_hovered = on;
        update();
    }

    bool isHovered() const { return m_hovered; }

    void setFlowPulseDurationMs(int ms) {
        m_flowPulseDurationMs = std::max(0, ms);
        if (m_flowPulseDurationMs == 0) {
            clearFlow();
        }
    }

    int flowPulseDurationMs() const { return m_flowPulseDurationMs; }

    void notifyDataFlow() {
        if (!isComplete() || m_flowPulseDurationMs <= 0) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!m_flowActive) {
            m_flowActive = true;
            m_flowStart = now;
        }
        m_flowLast = now;
        update();
    }

    void clearFlow() {
        if (!m_flowActive) {
            return;
        }
        m_flowActive = false;
        update();
    }

    bool tickFlow(const std::chrono::steady_clock::time_point& now) {
        if (!m_flowActive) {
            return false;
        }
        if (m_flowPulseDurationMs <= 0) {
            m_flowActive = false;
            return false;
        }
        const auto inactiveMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_flowLast).count();
        if (inactiveMs > static_cast<long long>(m_flowPulseDurationMs * 2)) {
            m_flowActive = false;
            update();
            return false;
        }
        update();
        return true;
    }

    SwRectF boundingRect() const override {
        const SwPointF a = startScene_();
        const SwPointF b = endScene_();
        const double minX = std::min(a.x, b.x);
        const double minY = std::min(a.y, b.y);
        const double maxX = std::max(a.x, b.x);
        const double maxY = std::max(a.y, b.y);
        const double pad = 120.0;
        return SwRectF(minX - pad, minY - pad, (maxX - minX) + pad * 2.0, (maxY - minY) + pad * 2.0);
    }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override {
        if (!painter || (!m_outNode && !m_inNode)) {
            return;
        }

        ConnectionPainter::PaintData data;
        data.startScene = startScene_();
        data.endScene = endScene_();
        data.hovered = m_hovered;
        data.selected = isSelected();
        data.complete = isComplete();

        if (m_flowActive && data.complete && m_flowPulseDurationMs > 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_flowStart).count();
            data.flowActive = true;
            data.flowPhase = std::fmod(static_cast<double>(ageMs) / static_cast<double>(m_flowPulseDurationMs), 1.0);
        }

        ConnectionPainter::paint(painter, ctx, data);
    }

private:
    SwPointF startScene_() const {
        if (m_outNode) {
            return m_outNode->outputPortScenePos(m_outPort);
        }
        return m_sceneEndPoint;
    }

    SwPointF endScene_() const {
        if (m_inNode) {
            return m_inNode->inputPortScenePos(m_inPort);
        }
        if (m_hasSceneEndPoint) {
            return m_sceneEndPoint;
        }
        return startScene_();
    }

    ConnectionId m_connectionId{};
    BasicGraphicsScene* m_scene{nullptr};

    NodeGraphicsObject* m_outNode{nullptr};
    NodeGraphicsObject* m_inNode{nullptr};
    int m_outPort{0};
    int m_inPort{0};

    SwPointF m_sceneEndPoint{};
    bool m_hasSceneEndPoint{false};
    bool m_hovered{false};

    int m_flowPulseDurationMs{300};
    bool m_flowActive{false};
    std::chrono::steady_clock::time_point m_flowStart{};
    std::chrono::steady_clock::time_point m_flowLast{};
};

} // namespace SwizioNodes
