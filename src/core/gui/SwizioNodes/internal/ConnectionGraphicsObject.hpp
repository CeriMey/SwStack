#pragma once

/**
 * @file
 * @ingroup core_swizio_nodes
 * @brief Declares the scene item responsible for visualizing a node-editor connection.
 *
 * This graphics object tracks one logical edge from the graph model and turns it into a
 * selectable, paintable scene primitive. It sits between the abstract connection state and
 * the `SwGraphicsScene`, handling geometry refresh, hit testing, and repaint requests.
 */




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
    /**
     * @brief Constructs a `ConnectionGraphicsObject` instance.
     * @param scene Value passed to the method.
     * @param scene Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    ConnectionGraphicsObject(BasicGraphicsScene& scene, ConnectionId const connectionId)
        : m_connectionId(connectionId)
        , m_scene(&scene) {
        setFlags(ItemIsSelectable);
        setZValue(-10.0);
    }

    /**
     * @brief Destroys the `ConnectionGraphicsObject` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~ConnectionGraphicsObject() override = default;

    /**
     * @brief Returns the current connection Id.
     * @return The current connection Id.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    ConnectionId const& connectionId() const { return m_connectionId; }

    /**
     * @brief Returns the current node Scene.
     * @return The current node Scene.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    BasicGraphicsScene* nodeScene() const { return m_scene; }

    /**
     * @brief Sets the out Endpoint.
     * @param node Value passed to the method.
     * @param port Local port used by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOutEndpoint(NodeGraphicsObject* node, int port) {
        m_outNode = node;
        m_outPort = port;
        m_hasSceneEndPoint = false;
        update();
    }

    /**
     * @brief Sets the in Endpoint.
     * @param node Value passed to the method.
     * @param port Local port used by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setInEndpoint(NodeGraphicsObject* node, int port) {
        m_inNode = node;
        m_inPort = port;
        m_hasSceneEndPoint = false;
        update();
    }

    /**
     * @brief Sets the scene End Point.
     * @param scenePos Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSceneEndPoint(const SwPointF& scenePos) {
        m_sceneEndPoint = scenePos;
        m_hasSceneEndPoint = true;
        update();
    }

    /**
     * @brief Returns whether the object reports complete.
     * @return `true` when the object reports complete; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isComplete() const { return m_outNode && m_inNode; }

    /**
     * @brief Returns the current out Node.
     * @return The current out Node.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    NodeGraphicsObject* outNode() const { return m_outNode; }
    /**
     * @brief Returns the current in Node.
     * @return The current in Node.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    NodeGraphicsObject* inNode() const { return m_inNode; }
    /**
     * @brief Returns the current out Port.
     * @return The current out Port.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int outPort() const { return m_outPort; }
    /**
     * @brief Returns the current in Port.
     * @return The current in Port.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int inPort() const { return m_inPort; }

    /**
     * @brief Sets the hovered.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHovered(bool on) {
        if (m_hovered == on) {
            return;
        }
        m_hovered = on;
        update();
    }

    /**
     * @brief Returns whether the object reports hovered.
     * @return `true` when the object reports hovered; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isHovered() const { return m_hovered; }

    /**
     * @brief Sets the flow Pulse Duration Ms.
     * @param ms Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFlowPulseDurationMs(int ms) {
        m_flowPulseDurationMs = std::max(0, ms);
        if (m_flowPulseDurationMs == 0) {
            clearFlow();
        }
    }

    /**
     * @brief Returns the current flow Pulse Duration Ms.
     * @return The current flow Pulse Duration Ms.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int flowPulseDurationMs() const { return m_flowPulseDurationMs; }

    /**
     * @brief Performs the `notifyDataFlow` operation.
     */
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

    /**
     * @brief Clears the current object state.
     */
    void clearFlow() {
        if (!m_flowActive) {
            return;
        }
        m_flowActive = false;
        update();
    }

    /**
     * @brief Performs the `tickFlow` operation.
     * @param now Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
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
