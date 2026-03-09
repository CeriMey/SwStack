#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/AbstractNodeGeometry.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by AbstractNodeGeometry in the CoreSw node-editor
 * layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the abstract node geometry interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Most declarations here are extension points or internal contracts that coordinate graph
 * editing, visualization, and interaction.
 *
 */


#include "Export.hpp"

#include "SwizioNodes/internal/AbstractGraphModel.hpp"
#include "SwizioNodes/internal/StyleCollection.hpp"

#include "core/types/Sw.h"
#include "graphics/SwGraphicsTypes.h"
#include "graphics/SwTransform.h"

#include <cmath>

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC AbstractNodeGeometry
{
public:
    /**
     * @brief Constructs a `AbstractNodeGeometry` instance.
     * @param graphModel Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit AbstractNodeGeometry(AbstractGraphModel& graphModel)
        : m_graphModel(&graphModel)
    {
    }

    /**
     * @brief Destroys the `AbstractNodeGeometry` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~AbstractNodeGeometry() = default;

    /**
     * The node's size plus some additional margin around it to account for drawing
     * effects (for example shadows) or node's parts outside the size rectangle
     * (for example port points).
     */
    virtual SwRectF boundingRect(NodeId const nodeId) const
    {
        const SwSize s = size(nodeId);

        const double ratio = 0.20;
        const double widthMargin = static_cast<double>(s.width) * ratio;
        const double heightMargin = static_cast<double>(s.height) * ratio;

        SwRectF r(0.0, 0.0, static_cast<double>(s.width), static_cast<double>(s.height));
        r.x -= widthMargin;
        r.y -= heightMargin;
        r.width += widthMargin * 2.0;
        r.height += heightMargin * 2.0;
        return r;
    }

    /// A direct rectangle defining the borders of the node's rectangle.
    virtual SwSize size(NodeId const nodeId) const = 0;

    /// Recompute cached size when ports or embedded widgets change.
    virtual void recomputeSize(NodeId const nodeId) const = 0;

    /// Port position in node's coordinate system.
    virtual SwPointF portPosition(NodeId const nodeId,
                                  PortType const portType,
                                  PortIndex const index) const
        = 0;

    /// A convenience function using the `portPosition` and a given transformation.
    virtual SwPointF portScenePosition(NodeId const nodeId,
                                       PortType const portType,
                                       PortIndex const index,
                                       SwTransform const& t) const
    {
        const SwPointF result = portPosition(nodeId, portType, index);
        return t.map(result);
    }

    /// Defines where to draw port label. The point corresponds to a font baseline.
    virtual SwPointF portTextPosition(NodeId const nodeId,
                                      PortType const portType,
                                      PortIndex const portIndex) const
        = 0;

    /// Defines where to start drawing the caption. The point corresponds to a font baseline.
    virtual SwPointF captionPosition(NodeId const nodeId) const = 0;

    /// Caption rect is needed for estimating the total node size.
    virtual SwRectF captionRect(NodeId const nodeId) const = 0;

    /// Position for an embedded widget. Return any value if you don't embed.
    virtual SwPointF widgetPosition(NodeId const nodeId) const = 0;

    /**
     * @brief Performs the `checkPortHit` operation.
     * @param nodeId Value passed to the method.
     * @param portType Value passed to the method.
     * @param nodePoint Value passed to the method.
     * @return The requested check Port Hit.
     */
    virtual PortIndex checkPortHit(NodeId const nodeId,
                                   PortType const portType,
                                   SwPointF const nodePoint) const
    {
        PortIndex result = InvalidPortIndex;

        if (!m_graphModel) {
            return result;
        }

        if (portType == PortType::None) {
            return result;
        }

        auto const& nodeStyle = StyleCollection::nodeStyle();
        const double tolerance = 2.0 * nodeStyle.ConnectionPointDiameter;

        const NodeRole portCountRole = (portType == PortType::Out) ? NodeRole::OutPortCount : NodeRole::InPortCount;
        const PortCount n = m_graphModel->nodeData<PortCount>(nodeId, portCountRole);

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
            const SwPointF pp = portPosition(nodeId, portType, portIndex);
            const SwPointF p(pp.x - nodePoint.x, pp.y - nodePoint.y);
            const double distance = std::sqrt(p.x * p.x + p.y * p.y);
            if (distance < tolerance) {
                result = portIndex;
                break;
            }
        }

        return result;
    }

    /**
     * @brief Performs the `resizeHandleRect` operation.
     * @param nodeId Value passed to the method.
     * @return The requested resize Handle Rect.
     */
    virtual SwRect resizeHandleRect(NodeId const nodeId) const = 0;

protected:
    AbstractGraphModel* m_graphModel{nullptr};
};

} // namespace SwizioNodes
