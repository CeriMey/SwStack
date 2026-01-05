#pragma once

#if defined(QT_GUI_LIB)
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#else
#include "SwPainter.h"
#include "graphics/SwGraphicsRenderContext.h"
#include "graphics/SwPainterPath.h"
#endif

#include "Definitions.hpp"

namespace QtNodes {

class ConnectionGeometry;
class ConnectionGraphicsObject;

class ConnectionPainter
{
public:
#if defined(QT_GUI_LIB)
    static void paint(QPainter *painter, ConnectionGraphicsObject const &cgo);

    static QPainterPath getPainterStroke(ConnectionGraphicsObject const &cgo);
#else
    struct PaintData
    {
        SwPointF startScene{};
        SwPointF endScene{};

        bool useCustomColor{false};
        SwColor customColor{0, 0, 0};

        bool hovered{false};
        bool selected{false};
        bool complete{true};

        bool flowActive{false};
        double flowPhase{0.0};
    };

    static void paint(SwPainter *painter,
                      const SwGraphicsRenderContext &ctx,
                      const PaintData &data);
#endif
};

} // namespace QtNodes
