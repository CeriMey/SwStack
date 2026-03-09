#include "SwCreatorDeleteWidgetCommand.h"

#include "designer/SwCreatorFormCanvas.h"
#include "serialization/SwCreatorSwuiSerializer.h"

#include "SwWidget.h"

SwCreatorDeleteWidgetCommand::SwCreatorDeleteWidgetCommand(SwCreatorFormCanvas* canvas, SwWidget* widget)
    : m_canvas(canvas)
    , m_widget(widget) {
    if (widget) {
        m_xml = SwCreatorSwuiSerializer::serializeWidget(widget);
    }
}

void SwCreatorDeleteWidgetCommand::undo() {
    if (!m_canvas || m_widget || m_xml.isEmpty()) {
        return;
    }

    SwString error;
    SwWidget* w = SwCreatorSwuiSerializer::deserializeWidget(m_xml, m_canvas, &error);
    if (!w) {
        return;
    }

    m_canvas->registerDesignWidget(w);
    m_canvas->setSelectedWidget(w);
    m_canvas->selectionChanged(w);
    m_canvas->widgetAdded(w);
    w->setFocus(true);
    m_widget = w;
}

void SwCreatorDeleteWidgetCommand::redo() {
    if (!m_canvas || !m_widget) {
        return;
    }
    m_canvas->removeDesignWidget(m_widget);
    m_widget = nullptr;
}
