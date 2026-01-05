#pragma once

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
    explicit AbstractNodeGeometry(AbstractGraphModel& graphModel)
        : m_graphModel(&graphModel)
    {
    }

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

    virtual SwRect resizeHandleRect(NodeId const nodeId) const = 0;

protected:
    AbstractGraphModel* m_graphModel{nullptr};
};

} // namespace SwizioNodes

