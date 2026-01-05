#include "AbstractNodeGeometry.hpp"

#include "AbstractGraphModel.hpp"
#include "StyleCollection.hpp"

#include <cmath>

namespace {
inline QRectF expandedBoundingRect(QSize const &s, double ratio)
{
    const double widthMargin = static_cast<double>(s.width()) * ratio;
    const double heightMargin = static_cast<double>(s.height()) * ratio;
    return QRectF(-widthMargin,
                  -heightMargin,
                  static_cast<double>(s.width()) + widthMargin * 2.0,
                  static_cast<double>(s.height()) + heightMargin * 2.0);
}
} // namespace

namespace QtNodes {

AbstractNodeGeometry::AbstractNodeGeometry(AbstractGraphModel &graphModel)
    : _graphModel(&graphModel)
{
    //
}

QRectF AbstractNodeGeometry::boundingRect(NodeId const nodeId) const
{
    QSize s = size(nodeId);
    return expandedBoundingRect(s, 0.20);
}

QPointF AbstractNodeGeometry::portScenePosition(NodeId const nodeId,
                                                PortType const portType,
                                                PortIndex const index,
                                                QTransform const &t) const
{
    QPointF result = portPosition(nodeId, portType, index);

    return t.map(result);
}

PortIndex AbstractNodeGeometry::checkPortHit(NodeId const nodeId,
                                             PortType const portType,
                                             QPointF const nodePoint) const
{
    auto const &nodeStyle = StyleCollection::nodeStyle();

    PortIndex result = InvalidPortIndex;

    if(!_graphModel){
        return result;
    }

    if (portType == PortType::None)
        return result;

    double const tolerance = 2.0 * nodeStyle.ConnectionPointDiameter;
    double const toleranceSquared = tolerance * tolerance;

    size_t const n = _graphModel->nodeData<unsigned int>(nodeId,
                                                        (portType == PortType::Out)
                                                            ? NodeRole::OutPortCount
                                                            : NodeRole::InPortCount);

    for (unsigned int portIndex = 0; portIndex < n; ++portIndex) {
        auto pp = portPosition(nodeId, portType, portIndex);

        double const dx = pp.x() - nodePoint.x();
        double const dy = pp.y() - nodePoint.y();
        double const distanceSquared = (dx * dx) + (dy * dy);

        if (distanceSquared < toleranceSquared) {
            result = portIndex;
            break;
        }
    }

    return result;
}

} // namespace QtNodes
