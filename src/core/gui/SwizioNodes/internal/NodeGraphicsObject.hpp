#pragma once

#include "Export.hpp"

#include "SwizioNodes/internal/AbstractGraphModel.hpp"
#include "SwizioNodes/internal/Definitions.hpp"
#include "SwizioNodes/internal/StyleCollection.hpp"

#include "graphics/SwGraphicsItem.h"
#include "graphics/SwGraphicsItems.h"
#include "graphics/SwGraphicsRenderContext.h"
#include "graphics/SwGraphicsTypes.h"

#include "SwFont.h"
#include "SwPainter.h"
#include "SwString.h"
#include "SwWidget.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace SwizioNodes {

class BasicGraphicsScene;
class AbstractGraphModel;

class SWIZIO_NODES_PUBLIC NodeGraphicsObject : public SwGraphicsItem
{
public:
    struct PortHit {
        bool hit{false};
        PortType type{PortType::None};
        int index{-1};
    };

    NodeGraphicsObject(BasicGraphicsScene& scene,
                       NodeId nodeId,
                       const SwString& title = SwString(),
                       const std::vector<SwString>& inputs = {},
                       const std::vector<SwString>& outputs = {},
                       const SwColor& accent = SwColor{0, 139, 139},
                       const SwString& typeKey = SwString())
        : m_scene(&scene)
        , m_nodeId(nodeId)
        , m_typeKey(typeKey.isEmpty() ? title : typeKey)
        , m_title(title)
        , m_inputs(inputs)
        , m_outputs(outputs)
        , m_accent(accent)
        , m_multiIn(inputs.size(), true)
        , m_multiOut(outputs.size(), true) {
        setFlags(ItemIsSelectable | ItemIsMovable);
    }

    ~NodeGraphicsObject() override = default;

    NodeId nodeId() const { return m_nodeId; }

    SwString typeKey() const { return m_typeKey; }
    SwString title() const { return m_title; }

    int inputCount() const { return static_cast<int>(m_inputs.size()); }
    int outputCount() const { return static_cast<int>(m_outputs.size()); }

    const std::vector<SwString>& inputs() const { return m_inputs; }
    const std::vector<SwString>& outputs() const { return m_outputs; }

    SwString inputName(int index) const {
        if (index < 0 || index >= inputCount()) {
            return SwString();
        }
        return m_inputs[static_cast<size_t>(index)];
    }

    SwString outputName(int index) const {
        if (index < 0 || index >= outputCount()) {
            return SwString();
        }
        return m_outputs[static_cast<size_t>(index)];
    }

    SwColor accentColor() const { return m_accent; }

    SwString bodyText() const { return m_bodyText; }

    void setBodyText(const SwString& text) {
        if (m_bodyText == text) {
            return;
        }
        m_bodyText = text;
        update();
    }

    void setDefinition(const SwString& title,
                       const std::vector<SwString>& inputs,
                       const std::vector<SwString>& outputs,
                       const SwColor& accent,
                       const SwString& typeKey) {
        m_title = title;
        m_typeKey = typeKey.isEmpty() ? title : typeKey;
        m_inputs = inputs;
        m_outputs = outputs;
        m_accent = accent;
        m_multiIn.assign(inputs.size(), true);
        m_multiOut.assign(outputs.size(), true);
        updateEmbeddedWidgetLayout_();
        update();
    }

    // By default, both input and output ports accept multiple connections.
    bool allowsMultipleConnections(PortType type, int index) const {
        if (type == PortType::In) {
            if (index < 0 || index >= inputCount()) {
                return true;
            }
            return m_multiIn[static_cast<size_t>(index)];
        }
        if (type == PortType::Out) {
            if (index < 0 || index >= outputCount()) {
                return true;
            }
            return m_multiOut[static_cast<size_t>(index)];
        }
        return true;
    }

    void setAllowsMultipleConnections(PortType type, int index, bool allow) {
        if (type == PortType::In) {
            if (index < 0 || index >= inputCount()) {
                return;
            }
            m_multiIn[static_cast<size_t>(index)] = allow;
            return;
        }
        if (type == PortType::Out) {
            if (index < 0 || index >= outputCount()) {
                return;
            }
            m_multiOut[static_cast<size_t>(index)] = allow;
        }
    }

    SwPointF inputPortScenePos(int index) const {
        const SwPointF sp = scenePos();
        const SwPointF local = inputPortLocalPos_(index);
        return SwPointF(sp.x + local.x, sp.y + local.y);
    }

    SwPointF outputPortScenePos(int index) const {
        const SwPointF sp = scenePos();
        const SwPointF local = outputPortLocalPos_(index);
        return SwPointF(sp.x + local.x, sp.y + local.y);
    }

    PortHit hitTestPort(const SwPointF& scenePoint) const {
        PortHit out{};
        const int inCount = inputCount();
        const int outCount = outputCount();
        const double radius = static_cast<double>(kPortRadius) + 5.0;

        for (int i = 0; i < inCount; ++i) {
            const SwPointF p = inputPortScenePos(i);
            if (swDistance(p, scenePoint) <= radius) {
                out.hit = true;
                out.type = PortType::In;
                out.index = i;
                return out;
            }
        }

        for (int i = 0; i < outCount; ++i) {
            const SwPointF p = outputPortScenePos(i);
            if (swDistance(p, scenePoint) <= radius) {
                out.hit = true;
                out.type = PortType::Out;
                out.index = i;
                return out;
            }
        }

        return out;
    }

    void clearHoveredPort() {
        if (!m_hoverPort.hit) {
            return;
        }
        m_hoverPort = PortHit{};
        update();
    }

    void setHoveredPort(const PortHit& port) {
        if (!port.hit) {
            clearHoveredPort();
            return;
        }
        if (m_hoverPort.hit && m_hoverPort.type == port.type && m_hoverPort.index == port.index) {
            return;
        }
        m_hoverPort = port;
        update();
    }

    bool isPortHovered(PortType type, int index) const {
        return m_hoverPort.hit && m_hoverPort.type == type && m_hoverPort.index == index;
    }

    void setEmbeddedWidget(SwWidget* widget, int width = 0, int height = 0) {
        if (!widget) {
            return;
        }
        if (!m_proxyWidget) {
            m_proxyWidget = new SwGraphicsProxyWidget(widget);
            m_proxyWidget->setParentItem(this);
            if (scene()) {
                scene()->addItem(m_proxyWidget);
            }
        } else if (m_proxyWidget->widget() != widget) {
            m_proxyWidget->setWidget(widget);
        }

        if (width > 0 && height > 0) {
            m_proxyWidget->setWidgetBaseSize(width, height);
        }
        updateEmbeddedWidgetLayout_();
        update();
    }

    SwGraphicsProxyWidget* embeddedProxy() const { return m_proxyWidget; }

    void syncFromModel(AbstractGraphModel& graphModel);

    SwRectF boundingRect() const override {
        const LayoutMetrics layout = layoutMetrics_();
        return SwRectF(0.0, 0.0, static_cast<double>(layout.width), static_cast<double>(layout.height));
    }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override {
        if (!painter || !isVisible()) {
            return;
        }

        const double s = std::max(0.0001, ctx.scale);
        auto px = [&](int v) -> int { return static_cast<int>(std::lround(static_cast<double>(v) * s)); };
        auto px0 = [&](int v) -> int { return std::max(0, px(v)); };
        auto px1 = [&](int v) -> int { return std::max(1, px(v)); };

        const bool drawText = (s >= 0.45);
        const bool drawPorts = (s >= 0.18);
        const bool drawHeader = (s >= 0.12);
        const bool drawShadow = (s >= 0.12);

        const SwRectF br = boundingRect();
        SwRectF sceneRect = br;
        const SwPointF sp = scenePos();
        sceneRect.translate(sp.x, sp.y);
        const SwRect vr = ctx.mapFromScene(sceneRect);

        const NodeStyle& style = StyleCollection::nodeStyle();
        const bool selected = isSelected();

        const SwColor bg = style.GradientColor1;
        const SwColor headerBg = style.GradientColor0;
        const SwColor border = selected ? style.SelectedBoundaryColor : style.NormalBoundaryColor;
        const SwColor titleColor = style.FontColor;
        const SwColor portColor = style.ConnectionPointColor;
        const SwColor portFill = style.FilledConnectionPointColor;
        const SwColor shadow = style.ShadowColor;

        const int borderWidth = drawHeader ? px1(selected ? 2 : 1) : 0;

        if (drawShadow) {
            SwRect shadowRect = vr;
            shadowRect.x += px(4);
            shadowRect.y += px(6);
            painter->fillRoundedRect(shadowRect, px0(kRadius + 2), shadow, shadow, 0);
        }

        painter->fillRoundedRect(vr, px0(kRadius), bg, border, borderWidth);

        if (drawHeader) {
            const int headerH = std::max(1, px0(kHeaderHeight));
            SwRect headerRect = vr;
            headerRect.height = headerH;
            painter->fillRect(headerRect, headerBg, headerBg, 0);

            SwRect stripe = vr;
            stripe.width = px1(6);
            stripe.height = headerH;
            painter->fillRect(stripe, m_accent, m_accent, 0);

            if (drawText) {
                SwRect titleRect = vr;
                titleRect.height = headerH;
                titleRect.x += px0(12);
                titleRect.width = std::max(0, titleRect.width - px0(16));
                painter->drawText(titleRect,
                                  m_title,
                                  DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine,
                                  titleColor,
                                  SwFont(L"Segoe UI", px1(10), SemiBold));
            }
        }

        if (drawPorts) {
            const int portRadius = px1(kPortRadius);
            const int portBorderW = px1(1);
            const int labelGap = px0(6);
            const int labelHeight = px1(16);
            const int maxLabelWidth = std::max(0, vr.width / 2 - px0(20));

            for (int i = 0; i < inputCount(); ++i) {
                const SwPointF portScene = inputPortScenePos(i);
                const SwPointF portView = ctx.mapFromScene(portScene);
                const SwRect pr{static_cast<int>(std::lround(portView.x)) - portRadius,
                                static_cast<int>(std::lround(portView.y)) - portRadius,
                                portRadius * 2,
                                portRadius * 2};

                const bool hovered = isPortHovered(PortType::In, i);
                const SwColor fill = hovered ? portFill : portColor;
                painter->fillEllipse(pr, fill, border, portBorderW);

                if (drawText) {
                    SwRect label{
                        pr.x + pr.width + labelGap,
                        pr.y + (pr.height - labelHeight) / 2,
                        maxLabelWidth,
                        labelHeight};
                    painter->drawText(label,
                                      inputName(i),
                                      DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine,
                                      titleColor,
                                      SwFont(L"Segoe UI", px1(9), Normal));
                }
            }

            for (int i = 0; i < outputCount(); ++i) {
                const SwPointF portScene = outputPortScenePos(i);
                const SwPointF portView = ctx.mapFromScene(portScene);
                const SwRect pr{static_cast<int>(std::lround(portView.x)) - portRadius,
                                static_cast<int>(std::lround(portView.y)) - portRadius,
                                portRadius * 2,
                                portRadius * 2};

                const bool hovered = isPortHovered(PortType::Out, i);
                const SwColor fill = hovered ? portFill : portColor;
                painter->fillEllipse(pr, fill, border, portBorderW);

                if (drawText) {
                    SwRect label{
                        pr.x - labelGap - maxLabelWidth,
                        pr.y + (pr.height - labelHeight) / 2,
                        maxLabelWidth,
                        labelHeight};
                    painter->drawText(label,
                                      outputName(i),
                                      DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine,
                                      titleColor,
                                      SwFont(L"Segoe UI", px1(9), Normal));
                }
            }
        }

        if (drawText && !m_bodyText.isEmpty()) {
            const LayoutMetrics layout = layoutMetrics_();
            const int footerH = px1(kFooterHeight);
            SwRect pill{
                vr.x + px0(12),
                vr.y + px0(layout.height - kFooterHeight),
                std::max(0, vr.width - px0(24)),
                footerH - px0(6)};
            painter->fillRoundedRect(pill, px0(10), bg, border, px1(1));
            painter->drawText(pill,
                              m_bodyText,
                              DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine,
                              titleColor,
                              SwFont(L"Segoe UI", px1(9), Normal));
        }
    }

private:
    struct LayoutMetrics {
        int width{0};
        int height{0};
        int portRows{0};
        int widgetWidth{0};
        int widgetHeight{0};
        int widgetTop{0};
    };

    LayoutMetrics layoutMetrics_() const {
        LayoutMetrics out;
        out.portRows = std::max(inputCount(), outputCount());

        if (m_proxyWidget) {
            SwSize base = m_proxyWidget->widgetBaseSize();
            out.widgetWidth = base.width;
            out.widgetHeight = base.height;
        }

        out.width = kWidth;
        if (out.widgetWidth > 0) {
            out.width = std::max(out.width, out.widgetWidth + kWidgetPaddingX * 2);
        }

        const int footer = m_bodyText.isEmpty() ? 0 : kFooterHeight;
        const int widgetBlock = (out.widgetHeight > 0) ? (kWidgetPaddingY * 2 + out.widgetHeight) : 0;
        out.height = kHeaderHeight + kBodyPaddingY * 2 + out.portRows * kPortSpacing + widgetBlock + footer;
        out.widgetTop = kHeaderHeight + kBodyPaddingY + out.portRows * kPortSpacing + kWidgetPaddingY;
        return out;
    }

    void updateEmbeddedWidgetLayout_() {
        if (!m_proxyWidget) {
            return;
        }
        LayoutMetrics layout = layoutMetrics_();
        if (layout.widgetWidth <= 0 || layout.widgetHeight <= 0) {
            return;
        }
        const double x = (static_cast<double>(layout.width) - static_cast<double>(layout.widgetWidth)) * 0.5;
        const double y = static_cast<double>(layout.widgetTop);
        m_proxyWidget->setPos(x, y);
    }

    SwPointF inputPortLocalPos_(int index) const {
        const double y = static_cast<double>(kHeaderHeight + kBodyPaddingY + index * kPortSpacing + kPortSpacing / 2);
        return SwPointF(static_cast<double>(kPortMarginX), y);
    }

    SwPointF outputPortLocalPos_(int index) const {
        const LayoutMetrics layout = layoutMetrics_();
        const double y = static_cast<double>(kHeaderHeight + kBodyPaddingY + index * kPortSpacing + kPortSpacing / 2);
        return SwPointF(static_cast<double>(layout.width - kPortMarginX), y);
    }

    static const int kWidth = 320;
    static const int kHeaderHeight = 34;
    static const int kRadius = 12;
    static const int kPortSpacing = 26;
    static const int kBodyPaddingY = 12;
    static const int kPortMarginX = 16;
    static const int kPortRadius = 6;
    static const int kFooterHeight = 30;
    static const int kWidgetPaddingX = 16;
    static const int kWidgetPaddingY = 10;

    BasicGraphicsScene* m_scene{nullptr};
    NodeId m_nodeId{InvalidNodeId};
    SwString m_typeKey;
    SwString m_title;
    std::vector<SwString> m_inputs;
    std::vector<SwString> m_outputs;
    SwColor m_accent{0, 139, 139};
    SwString m_bodyText;
    PortHit m_hoverPort{};
    std::vector<bool> m_multiIn;
    std::vector<bool> m_multiOut;
    SwGraphicsProxyWidget* m_proxyWidget{nullptr};
};

inline void NodeGraphicsObject::syncFromModel(AbstractGraphModel& graphModel) {
    SwString title = graphModel.nodeData(m_nodeId, NodeRole::Caption).toString();
    if (title.isEmpty()) {
        title = graphModel.nodeData(m_nodeId, NodeRole::Type).toString();
    }
    if (!title.isEmpty()) {
        m_title = title;
        if (m_typeKey.isEmpty()) {
            m_typeKey = title;
        }
    }

    auto readPortNames = [&](PortType type, NodeRole role, const SwString& fallbackPrefix) {
        std::vector<SwString> names;
        const unsigned int count = graphModel.nodeData(m_nodeId, role).toUInt();
        names.reserve(count);
        for (unsigned int i = 0; i < count; ++i) {
            SwString name;
            SwAny visibleAny = graphModel.portData(m_nodeId, type, i, PortRole::CaptionVisible);
            if (visibleAny.toBool()) {
                name = graphModel.portData(m_nodeId, type, i, PortRole::Caption).toString();
            }
            if (name.isEmpty()) {
                name = fallbackPrefix + SwString::number(static_cast<int>(i + 1));
            }
            names.push_back(name);
        }
        return names;
    };

    m_inputs = readPortNames(PortType::In, NodeRole::InPortCount, "in");
    m_outputs = readPortNames(PortType::Out, NodeRole::OutPortCount, "out");
    m_multiIn.assign(m_inputs.size(), true);
    m_multiOut.assign(m_outputs.size(), true);

    SwAny widgetAny = graphModel.nodeData(m_nodeId, NodeRole::Widget);
    SwWidget* widget = nullptr;
    if (!widgetAny.typeName().empty()) {
        try {
            widget = widgetAny.get<SwWidget*>();
        } catch (...) {
            widget = nullptr;
        }
    }
    if (widget) {
        setEmbeddedWidget(widget);
    }

    updateEmbeddedWidgetLayout_();
    update();
}

} // namespace SwizioNodes
