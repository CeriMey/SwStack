#pragma once

#include "Export.hpp"

#include "ConnectionStyle.hpp"
#include "GraphicsViewStyle.hpp"
#include "NodeStyle.hpp"

#include "core/types/SwString.h"

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC StyleCollection
{
public:
    static NodeStyle const& nodeStyle();

    static ConnectionStyle const& connectionStyle();

    static GraphicsViewStyle const& flowViewStyle();

    static SwString const& widgetNodeStyle();

public:
    static void setNodeStyle(NodeStyle);

    static void setConnectionStyle(ConnectionStyle);

    static void setGraphicsViewStyle(GraphicsViewStyle);

    static void setWidgetNodeStyle(const SwString&);

private:
    StyleCollection() = default;
    StyleCollection(StyleCollection const&) = delete;
    StyleCollection& operator=(StyleCollection const&) = delete;

    static StyleCollection& instance();

private:
    NodeStyle m_nodeStyle;
    ConnectionStyle m_connectionStyle;
    GraphicsViewStyle m_flowViewStyle;
    SwString m_widgetNodeViewStyle;
};

inline StyleCollection& StyleCollection::instance()
{
    static StyleCollection inst;
    return inst;
}

inline NodeStyle const& StyleCollection::nodeStyle() { return instance().m_nodeStyle; }

inline ConnectionStyle const& StyleCollection::connectionStyle() { return instance().m_connectionStyle; }

inline GraphicsViewStyle const& StyleCollection::flowViewStyle() { return instance().m_flowViewStyle; }

inline SwString const& StyleCollection::widgetNodeStyle() { return instance().m_widgetNodeViewStyle; }

inline void StyleCollection::setNodeStyle(NodeStyle style) { instance().m_nodeStyle = std::move(style); }

inline void StyleCollection::setConnectionStyle(ConnectionStyle style)
{
    instance().m_connectionStyle = std::move(style);
}

inline void StyleCollection::setGraphicsViewStyle(GraphicsViewStyle style)
{
    instance().m_flowViewStyle = std::move(style);
}

inline void StyleCollection::setWidgetNodeStyle(const SwString& style) { instance().m_widgetNodeViewStyle = style; }

inline void NodeStyle::setNodeStyle(SwString jsonText)
{
    NodeStyle style(std::move(jsonText));
    StyleCollection::setNodeStyle(std::move(style));
}

inline void NodeStyle::setWidgetStyle(SwString jsonText) { StyleCollection::setWidgetNodeStyle(jsonText); }

inline void ConnectionStyle::setConnectionStyle(SwString jsonText)
{
    ConnectionStyle style(std::move(jsonText));
    StyleCollection::setConnectionStyle(std::move(style));
}

inline void GraphicsViewStyle::setStyle(SwString jsonText)
{
    GraphicsViewStyle style(std::move(jsonText));
    StyleCollection::setGraphicsViewStyle(std::move(style));
}

} // namespace SwizioNodes
