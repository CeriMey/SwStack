#include "ConnectionPainter.hpp"

#if defined(QT_GUI_LIB)
#include <QtGui/QIcon>
#include "QStyleOptionGraphicsItem"
#include "AbstractGraphModel.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "ConnectionState.hpp"
#include "Definitions.hpp"
#include "NodeData.hpp"
#include "StyleCollection.hpp"

namespace {

// Calcul du LOD (Level Of Detail) à partir de la transformation actuelle du painter.
// Plus le LOD est faible, plus l'item est "petit" à l'écran (zoom out important).
inline double computeLOD(QPainter *painter)
{
    // Qt fournit une méthode statique pratique :
    // levelOfDetailFromTransform(painter->worldTransform())
    return QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());
}

} // namespace

namespace QtNodes {

static QPainterPath cubicPath(ConnectionGraphicsObject const &connection)
{
    QPointF const &in = connection.endPoint(PortType::In);
    QPointF const &out = connection.endPoint(PortType::Out);

    auto const c1c2 = connection.pointsC1C2();

    // cubic spline
    QPainterPath cubic(out);

    cubic.cubicTo(c1c2.first, c1c2.second, in);

    return cubic;
}

QPainterPath ConnectionPainter::getPainterStroke(ConnectionGraphicsObject const &connection)
{
    auto cubic = cubicPath(connection);

    QPointF const &out = connection.endPoint(PortType::Out);
    QPainterPath result(out);

    unsigned segments = 20;

    for (auto i = 0ul; i < segments; ++i) {
        double ratio = double(i + 1) / segments;
        result.lineTo(cubic.pointAtPercent(ratio));
    }

    QPainterPathStroker stroker;
    stroker.setWidth(10.0);

    return stroker.createStroke(result);
}

#ifdef NODE_DEBUG_DRAWING
static void debugDrawing(QPainter *painter, ConnectionGraphicsObject const &cgo)
{
    Q_UNUSED(painter);

    {
        QPointF const &in = cgo.endPoint(PortType::In);
        QPointF const &out = cgo.endPoint(PortType::Out);

        auto const points = cgo.pointsC1C2();

        painter->setPen(Qt::red);
        painter->setBrush(Qt::red);

        painter->drawLine(QLineF(out, points.first));
        painter->drawLine(QLineF(points.first, points.second));
        painter->drawLine(QLineF(points.second, in));
        painter->drawEllipse(points.first, 3, 3);
        painter->drawEllipse(points.second, 3, 3);

        painter->setBrush(Qt::NoBrush);
        painter->drawPath(cubicPath(cgo));
    }

    {
        painter->setPen(Qt::yellow);
        painter->drawRect(cgo.boundingRect());
    }
}

#endif

static void drawSketchLine(QPainter *painter, ConnectionGraphicsObject const &cgo)
{
    ConnectionState const &state = cgo.connectionState();

    if (state.requiresPort()) {
        auto const &connectionStyle = QtNodes::StyleCollection::connectionStyle();

        QPen pen;
        pen.setWidth(connectionStyle.constructionLineWidth());
        pen.setColor(connectionStyle.constructionColor());
        pen.setStyle(Qt::DashLine);

        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);

        auto cubic = cubicPath(cgo);

        // cubic spline
        painter->drawPath(cubic);
    }
}

static void drawHoveredOrSelected(QPainter *painter, ConnectionGraphicsObject const &cgo)
{
    bool const hovered = cgo.connectionState().hovered();
    bool const selected = cgo.isSelected();

    // drawn as a fat background
    if (hovered || selected) {
        auto const &connectionStyle = QtNodes::StyleCollection::connectionStyle();

        double const lineWidth = connectionStyle.lineWidth();

        QPen pen;
        pen.setWidth(2 * lineWidth);
        pen.setColor(selected ? connectionStyle.selectedHaloColor()
                              : connectionStyle.hoveredColor());

        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);

        // cubic spline
        auto const cubic = cubicPath(cgo);
        painter->drawPath(cubic);
    }
}

static void drawNormalLine(QPainter *painter, ConnectionGraphicsObject const &cgo)
{
    ConnectionState const &state = cgo.connectionState();

    if (state.requiresPort())
        return;

    // colors

    auto const &connectionStyle = QtNodes::StyleCollection::connectionStyle();

    QColor normalColorOut = connectionStyle.normalColor();
    QColor normalColorIn = connectionStyle.normalColor();
    QColor selectedColor = connectionStyle.selectedColor();

    bool useGradientColor = false;

    AbstractGraphModel const &graphModel = cgo.graphModel();

    if (connectionStyle.useDataDefinedColors()) {
        using QtNodes::PortType;

        auto const cId = cgo.connectionId();

        auto dataTypeOut = graphModel
                               .portData(cId.outNodeId,
                                         PortType::Out,
                                         cId.outPortIndex,
                                         PortRole::DataType)
                               .value<NodeDataType>();

        auto dataTypeIn
            = graphModel.portData(cId.inNodeId, PortType::In, cId.inPortIndex, PortRole::DataType)
                  .value<NodeDataType>();

        useGradientColor = (dataTypeOut.id != dataTypeIn.id);

        normalColorOut = connectionStyle.normalColor(dataTypeOut.id);
        normalColorIn = connectionStyle.normalColor(dataTypeIn.id);
        selectedColor = normalColorOut.darker(200);
    }

    // geometry

    double const lineWidth = connectionStyle.lineWidth();

    // draw normal line
    QPen p;

    p.setWidth(lineWidth);

    bool const selected = cgo.isSelected();

    auto cubic = cubicPath(cgo);
    if (useGradientColor) {
        painter->setBrush(Qt::NoBrush);

        QColor cOut = normalColorOut;
        if (selected)
            cOut = cOut.darker(200);
        p.setColor(cOut);
        painter->setPen(p);

        unsigned int const segments = 60;

        for (unsigned int i = 0ul; i < segments; ++i) {
            double ratioPrev = double(i) / segments;
            double ratio = double(i + 1) / segments;

            if (i == segments / 2) {
                QColor cIn = normalColorIn;
                if (selected)
                    cIn = cIn.darker(200);

                p.setColor(cIn);
                painter->setPen(p);
            }
            painter->drawLine(cubic.pointAtPercent(ratioPrev), cubic.pointAtPercent(ratio));
        }

        {
            QIcon icon(":convert.png");

            QPixmap pixmap = icon.pixmap(QSize(22, 22));
            painter->drawPixmap(cubic.pointAtPercent(0.50)
                                    - QPoint(pixmap.width() / 2, pixmap.height() / 2),
                                pixmap);
        }
    } else {
        p.setColor(normalColorOut);

        if (selected) {
            p.setColor(selectedColor);
        }

        painter->setPen(p);
        painter->setBrush(Qt::NoBrush);

        painter->drawPath(cubic);
    }
}

void ConnectionPainter::paint(QPainter *painter, ConnectionGraphicsObject const &cgo)
{
    double lod = computeLOD(painter);

    QPainterPath path = cubicPath(cgo);
    double connectionLength = path.length();

    // Choisissez un seuil de LOD et de longueur
    static double const LOD_THRESHOLD = 0.07;
    static double const LENGTH_THRESHOLD = 1000.0; // Par exemple

    // Si trop petit à l'écran ET assez court, on n'affiche pas
    if (lod < LOD_THRESHOLD && connectionLength < LENGTH_THRESHOLD)
    {
        return;
    }

    drawHoveredOrSelected(painter, cgo);

    drawSketchLine(painter, cgo);

    drawNormalLine(painter, cgo);

#ifdef NODE_DEBUG_DRAWING
    debugDrawing(painter, cgo);
#endif

    // draw end points
    auto const &connectionStyle = QtNodes::StyleCollection::connectionStyle();

    double const pointDiameter = connectionStyle.pointDiameter();

    painter->setPen(connectionStyle.constructionColor());
    painter->setBrush(connectionStyle.constructionColor());
    double const pointRadius = pointDiameter / 2.0;
    painter->drawEllipse(cgo.out(), pointRadius, pointRadius);
    painter->drawEllipse(cgo.in(), pointRadius, pointRadius);
}

} // namespace QtNodes
#else
#include "SwizioNodes/StyleCollection"

#include <algorithm>
#include <cmath>

namespace {

inline int roundToInt_(double v)
{
    return static_cast<int>(std::lround(v));
}

inline int clampInt_(int v, int lo, int hi)
{
    return std::max(lo, std::min(hi, v));
}

inline SwPointF cubicBezier_(const SwPointF &p0,
                             const SwPointF &p1,
                             const SwPointF &p2,
                             const SwPointF &p3,
                             double t)
{
    const double u = 1.0 - t;
    const double tt = t * t;
    const double uu = u * u;
    const double uuu = uu * u;
    const double ttt = tt * t;

    SwPointF p;
    p.x = uuu * p0.x;
    p.x += 3.0 * uu * t * p1.x;
    p.x += 3.0 * u * tt * p2.x;
    p.x += ttt * p3.x;

    p.y = uuu * p0.y;
    p.y += 3.0 * uu * t * p1.y;
    p.y += 3.0 * u * tt * p2.y;
    p.y += ttt * p3.y;
    return p;
}

inline SwColor clampColorOffset_(const SwColor &c, int dr, int dg, int db)
{
    auto clamp = [](int v) { return std::max(0, std::min(255, v)); };
    return SwColor{clamp(c.r + dr), clamp(c.g + dg), clamp(c.b + db)};
}

} // namespace

namespace QtNodes {

void ConnectionPainter::paint(SwPainter *painter,
                              const SwGraphicsRenderContext &ctx,
                              const PaintData &data)
{
    if (!painter) {
        return;
    }

    const double s = std::max(0.0001, ctx.scale);
    if (s < 0.06) {
        return;
    }

    const auto px1 = [&](double v) -> int { return std::max(1, roundToInt_(v * s)); };

    const SwPointF a = ctx.mapFromScene(data.startScene);
    const SwPointF b = ctx.mapFromScene(data.endScene);

    const double dx = std::max(80.0 * s, std::abs(b.x - a.x) * 0.5);
    const SwPointF c1(a.x + dx, a.y);
    const SwPointF c2(b.x - dx, b.y);

    const SwizioNodes::ConnectionStyle &style = SwizioNodes::StyleCollection::connectionStyle();

    const SwColor mainBase = data.useCustomColor ? data.customColor
                                                 : (data.complete ? style.normalColor() : style.constructionColor());
    SwColor main = mainBase;
    if (data.selected) {
        main = data.useCustomColor ? clampColorOffset_(mainBase, 30, 30, 30) : style.selectedColor();
    } else if (data.hovered) {
        main = data.useCustomColor ? clampColorOffset_(mainBase, 30, 30, 30) : style.hoveredColor();
    }

    const SwColor halo = style.selectedHaloColor();
    const SwColor outline = SwColor{15, 23, 42};

    const double baseWidth = data.complete ? static_cast<double>(style.lineWidth())
                                           : static_cast<double>(style.constructionLineWidth());
    const double extra = data.selected ? 2.0 : (data.hovered ? 1.0 : 0.0);
    const int w = px1(baseWidth + extra);
    const int haloW = data.selected ? std::max(w + 2, px1(baseWidth + extra + 4.0)) : 0;

    const double dist = std::hypot(b.x - a.x, b.y - a.y);
    const int segments = clampInt_(static_cast<int>(std::lround(dist / 10.0)), 24, 160);

    auto drawDot = [&](int x, int y, const SwColor &fill, int radiusPx) {
        const int d = radiusPx * 2;
        painter->fillEllipse(SwRect{x - radiusPx, y - radiusPx, d, d}, fill, outline, px1(1.0));
    };

    int lastX = roundToInt_(a.x);
    int lastY = roundToInt_(a.y);

    const int pr = std::max(2, px1(style.pointDiameter() * 0.5));
    drawDot(lastX, lastY, main, pr);

    for (int i = 1; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const SwPointF p = cubicBezier_(a, c1, c2, b, t);
        const int x = roundToInt_(p.x);
        const int y = roundToInt_(p.y);
        if (x == lastX && y == lastY) {
            continue;
        }

        if (haloW > 0) {
            painter->drawLine(lastX, lastY, x, y, halo, haloW);
        }
        painter->drawLine(lastX,
                          lastY,
                          x,
                          y,
                          outline,
                          std::max(w + 1, px1(baseWidth + extra + 2.0)));
        painter->drawLine(lastX, lastY, x, y, main, w);

        lastX = x;
        lastY = y;
    }

    drawDot(lastX, lastY, main, pr);

    if (data.flowActive && data.complete) {
        const double phase = data.flowPhase - std::floor(data.flowPhase);
        const SwPointF p = cubicBezier_(a, c1, c2, b, phase);
        const SwColor glow = clampColorOffset_(mainBase, 120, 120, 120);
        const int rr = std::max(2, px1(style.pointDiameter()));
        drawDot(roundToInt_(p.x), roundToInt_(p.y), glow, rr);
    }
}

} // namespace QtNodes
#endif
