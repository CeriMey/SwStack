#pragma once

/**
 * @file
 * @ingroup core_swizio_nodes
 * @brief Declares the singleton-style repository for node-editor visual styles.
 *
 * `StyleCollection` stores the active node, connection, and view style objects used by
 * the embedded node-editor module. It gives rendering code a central place to read or
 * override the current theme without threading style instances through every object.
 */




#include "Export.hpp"

#include "ConnectionStyle.hpp"
#include "GraphicsViewStyle.hpp"
#include "NodeStyle.hpp"

#include "core/types/SwString.h"

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC StyleCollection
{
public:
    /**
     * @brief Returns the current node Style.
     * @return The current node Style.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static NodeStyle const& nodeStyle();

    /**
     * @brief Returns the current connection Style.
     * @return The current connection Style.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static ConnectionStyle const& connectionStyle();

    /**
     * @brief Returns the current flow View Style.
     * @return The current flow View Style.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static GraphicsViewStyle const& flowViewStyle();

    /**
     * @brief Returns the current widget Node Style.
     * @return The current widget Node Style.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwString const& widgetNodeStyle();

public:
    /**
     * @brief Sets the node Style.
     * @param NodeStyle Value passed to the method.
     * @return The requested node Style.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setNodeStyle(NodeStyle);

    /**
     * @brief Sets the connection Style.
     * @param ConnectionStyle Value passed to the method.
     * @return The requested connection Style.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setConnectionStyle(ConnectionStyle);

    /**
     * @brief Sets the graphics View Style.
     * @param GraphicsViewStyle Value passed to the method.
     * @return The requested graphics View Style.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setGraphicsViewStyle(GraphicsViewStyle);

    /**
     * @brief Sets the widget Node Style.
     * @return The requested widget Node Style.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
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
