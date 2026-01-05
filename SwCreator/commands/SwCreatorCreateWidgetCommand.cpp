#include "SwCreatorCreateWidgetCommand.h"

#include "ui/SwCreatorFormCanvas.h"
#include "serialization/SwCreatorSwuiSerializer.h"

#include "SwWidget.h"

SwCreatorCreateWidgetCommand::SwCreatorCreateWidgetCommand(SwCreatorFormCanvas* canvas, SwWidget* existingWidget)
    : m_canvas(canvas)
    , m_widget(existingWidget)
    , m_dx(0)
    , m_dy(0) {
    if (existingWidget) {
        m_xml = SwCreatorSwuiSerializer::serializeWidget(existingWidget);
    }
}

SwCreatorCreateWidgetCommand::SwCreatorCreateWidgetCommand(SwCreatorFormCanvas* canvas, const SwString& swuiXml, int dx, int dy)
    : m_canvas(canvas)
    , m_xml(swuiXml)
    , m_dx(dx)
    , m_dy(dy) {}

void SwCreatorCreateWidgetCommand::undo() {
    if (!m_canvas || !m_widget) {
        return;
    }
    m_canvas->removeDesignWidget(m_widget);
    m_widget = nullptr;
}

void SwCreatorCreateWidgetCommand::redo() {
    if (!m_canvas) {
        return;
    }
    if (m_widget) {
        return;
    }

    SwString error;
    SwWidget* w = SwCreatorSwuiSerializer::deserializeWidget(m_xml, m_canvas, &error);
    if (!w) {
        return;
    }

    SwRect r = w->getRect();
    w->move(r.x + m_dx, r.y + m_dy);

    m_canvas->registerDesignWidget(w);
    m_canvas->setSelectedWidget(w);
    m_canvas->selectionChanged(w);
    m_canvas->widgetAdded(w);
    w->setFocus(true);
    m_widget = w;
}
