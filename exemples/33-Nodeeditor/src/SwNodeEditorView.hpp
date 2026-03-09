#pragma once

#include "SwFrame.h"
#include "SwLineEdit.h"
#include "SwListWidget.h"

#include "graphics/SwGraphicsItems.h"
#include "graphics/SwGraphicsScene.h"
#include "graphics/SwGraphicsView.h"

#include "SwUndoStack.h"

#include "SwWidgetPlatformAdapter.h"
#include "SwTimer.h"

#include "SwizioNodes/DataFlowGraphModel"
#include "SwizioNodes/DataFlowGraphicsScene"
#include "SwizioNodes/ConnectionPainter"
#include "SwizioNodes/NodeDelegateModelRegistry"

#include "../nodes/AddNodeModel.h"
#include "../nodes/BinaryMathModel.h"
#include "../nodes/AnyListData.h"
#include "../nodes/GenericNodeModel.h"
#include "../nodes/InputNodeModel.h"
#include "../nodes/OutputNodeModel.h"
#include "../nodes/TimesNodeModel.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <gdiplus.h>
#endif

namespace swnodeeditor {

static int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

static long long packNodePortKey(int nodeId, int port) {
    return (static_cast<long long>(nodeId) << 32) | static_cast<unsigned int>(port);
}

static SwWidget* findRootWidget(SwObject* start) {
    SwWidget* lastWidget = nullptr;
    for (SwObject* p = start; p; p = p->parent()) {
        if (auto* w = dynamic_cast<SwWidget*>(p)) {
            lastWidget = w;
        }
    }
    return lastWidget;
}

static std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static bool containsCaseInsensitive(const SwString& haystack, const SwString& needle) {
    if (needle.isEmpty()) {
        return true;
    }
    const std::string h = toLowerAscii(haystack.toStdString());
    const std::string n = toLowerAscii(needle.toStdString());
    return h.find(n) != std::string::npos;
}

static bool tryParseDoubleLoose(const SwString& text, double* out) {
    if (!out) {
        return false;
    }

    std::string s = text.toStdString();
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };

    size_t start = 0;
    while (start < s.size() && isSpace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && isSpace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    s = s.substr(start, end - start);
    if (s.empty()) {
        return false;
    }

    // Accept both "3.14" and "3,14".
    for (char& c : s) {
        if (c == ',') {
            c = '.';
        }
    }

    errno = 0;
    char* endPtr = nullptr;
    const double v = std::strtod(s.c_str(), &endPtr);
    if (!endPtr || endPtr == s.c_str()) {
        return false;
    }
    while (*endPtr && isSpace(static_cast<unsigned char>(*endPtr))) {
        ++endPtr;
    }
    if (*endPtr != '\0') {
        return false;
    }
    if (errno == ERANGE || !std::isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

static SwColor clampColor(int r, int g, int b) {
    return SwColor{clampInt(r, 0, 255), clampInt(g, 0, 255), clampInt(b, 0, 255)};
}

static int roundToInt(double v) {
    return static_cast<int>(std::lround(v));
}

static SwPointF lerp(const SwPointF& a, const SwPointF& b, double t) {
    return SwPointF(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

static SwPointF cubicBezier(const SwPointF& p0, const SwPointF& p1, const SwPointF& p2, const SwPointF& p3, double t) {
    // De Casteljau.
    const SwPointF a = lerp(p0, p1, t);
    const SwPointF b = lerp(p1, p2, t);
    const SwPointF c = lerp(p2, p3, t);
    const SwPointF d = lerp(a, b, t);
    const SwPointF e = lerp(b, c, t);
    return lerp(d, e, t);
}

static double pointSegmentDistance(const SwPointF& p, const SwPointF& a, const SwPointF& b) {
    const double vx = b.x - a.x;
    const double vy = b.y - a.y;
    const double wx = p.x - a.x;
    const double wy = p.y - a.y;

    const double c1 = wx * vx + wy * vy;
    if (c1 <= 0.0) {
        return std::hypot(p.x - a.x, p.y - a.y);
    }

    const double c2 = vx * vx + vy * vy;
    if (c2 <= c1) {
        return std::hypot(p.x - b.x, p.y - b.y);
    }

    const double t = c1 / c2;
    const double px = a.x + t * vx;
    const double py = a.y + t * vy;
    return std::hypot(p.x - px, p.y - py);
}

class SwGridBackgroundItem final : public SwGraphicsItem {
public:
    SwGridBackgroundItem() { setZValue(-1000.0); }

    SwRectF boundingRect() const override {
        // Not used for culling (the view repaints everything). Provide a safe bound.
        return SwRectF(-100000.0, -100000.0, 200000.0, 200000.0);
    }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override {
        if (!painter || !isVisible()) {
            return;
        }
        const SwRect vr = ctx.viewRect;
        const SwColor bg = SwColor{17, 24, 39};      // slate-900
        const SwColor minor = SwColor{30, 41, 59};   // slate-800
        const SwColor major = SwColor{51, 65, 85};   // slate-700
        const SwColor cross = SwColor{99, 102, 241}; // indigo-500

        painter->fillRect(vr, bg, bg, 0);

        const double minorStep = 32.0;
        const double majorStep = 160.0;

        const SwPointF sceneTL = ctx.mapToScene(SwPointF(static_cast<double>(vr.x), static_cast<double>(vr.y)));
        const SwPointF sceneBR = ctx.mapToScene(SwPointF(static_cast<double>(vr.x + vr.width), static_cast<double>(vr.y + vr.height)));

        const double minX = std::min(sceneTL.x, sceneBR.x);
        const double maxX = std::max(sceneTL.x, sceneBR.x);
        const double minY = std::min(sceneTL.y, sceneBR.y);
        const double maxY = std::max(sceneTL.y, sceneBR.y);

        const double startX = std::floor(minX / minorStep) * minorStep;
        const double startY = std::floor(minY / minorStep) * minorStep;

        for (double x = startX; x <= maxX; x += minorStep) {
            const bool isMajor = (std::fmod(std::abs(x), majorStep) < 0.0001);
            const SwColor c = isMajor ? major : minor;
            const SwPointF p = ctx.mapFromScene(SwPointF(x, 0.0));
            const int vx = roundToInt(p.x);
            painter->drawLine(vx, vr.y, vx, vr.y + vr.height, c, 1);
        }

        for (double y = startY; y <= maxY; y += minorStep) {
            const bool isMajor = (std::fmod(std::abs(y), majorStep) < 0.0001);
            const SwColor c = isMajor ? major : minor;
            const SwPointF p = ctx.mapFromScene(SwPointF(0.0, y));
            const int vy = roundToInt(p.y);
            painter->drawLine(vr.x, vy, vr.x + vr.width, vy, c, 1);
        }

        // Axis cross at scene origin.
        const SwPointF originView = ctx.mapFromScene(SwPointF(0.0, 0.0));
        const int ox = roundToInt(originView.x);
        const int oy = roundToInt(originView.y);
        painter->drawLine(ox - 10, oy, ox + 10, oy, cross, 2);
        painter->drawLine(ox, oy - 10, ox, oy + 10, cross, 2);
    }
};

class SwNodeItem : public SwGraphicsItem {
public:
    enum PortType { InPort, OutPort };

    struct PortHit {
        bool hit{false};
        PortType type{InPort};
        int index{-1};
    };

    SwNodeItem(int id,
               const SwString& title,
               const std::vector<SwString>& inputs,
               const std::vector<SwString>& outputs,
               const SwColor& accentColor = SwColor{99, 102, 241},
               const SwString& typeKey = SwString())
        : m_id(id)
        , m_typeKey(typeKey.isEmpty() ? title : typeKey)
        , m_title(title)
        , m_inputs(inputs)
        , m_outputs(outputs)
        , m_accent(accentColor)
        , m_multiIn(inputs.size(), true)
        , m_multiOut(outputs.size(), true) {
        setFlags(ItemIsSelectable | ItemIsMovable);
    }

    int id() const { return m_id; }
    SwString typeKey() const { return m_typeKey; }
    SwString title() const { return m_title; }

    int inputCount() const { return static_cast<int>(m_inputs.size()); }
    int outputCount() const { return static_cast<int>(m_outputs.size()); }

    // By default, both input and output ports accept multiple connections.
    // Override this in derived node types if you need "single connection" pins.
    virtual bool allowsMultipleConnections(PortType type, int index) const {
        if (type == InPort) {
            if (index < 0 || index >= inputCount()) {
                return true;
            }
            return m_multiIn[static_cast<size_t>(index)];
        }
        if (index < 0 || index >= outputCount()) {
            return true;
        }
        return m_multiOut[static_cast<size_t>(index)];
    }

    void setAllowsMultipleConnections(PortType type, int index, bool allow) {
        if (type == InPort) {
            if (index < 0 || index >= inputCount()) {
                return;
            }
            m_multiIn[static_cast<size_t>(index)] = allow;
            return;
        }
        if (index < 0 || index >= outputCount()) {
            return;
        }
        m_multiOut[static_cast<size_t>(index)] = allow;
    }

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

    SwRectF boundingRect() const override {
        const int portRows = std::max(inputCount(), outputCount());
        const int footer = m_bodyText.isEmpty() ? 0 : kFooterHeight;
        const double h = static_cast<double>(kHeaderHeight + kBodyPaddingY * 2 + portRows * kPortSpacing + footer);
        return SwRectF(0.0, 0.0, static_cast<double>(kWidth), h);
    }

    SwPointF inputPortScenePos(int index) const {
        const SwPointF sp = scenePos();
        const SwPointF local = inputPortLocalPos(index);
        return SwPointF(sp.x + local.x, sp.y + local.y);
    }

    SwPointF outputPortScenePos(int index) const {
        const SwPointF sp = scenePos();
        const SwPointF local = outputPortLocalPos(index);
        return SwPointF(sp.x + local.x, sp.y + local.y);
    }

    PortHit hitTestPort(const SwPointF& scenePoint) const {
        PortHit out{};
        const int inCount = inputCount();
        const int outCount = outputCount();
        const double radius = kPortRadius + 5.0;

        for (int i = 0; i < inCount; ++i) {
            const SwPointF p = inputPortScenePos(i);
            if (swDistance(p, scenePoint) <= radius) {
                out.hit = true;
                out.type = InPort;
                out.index = i;
                return out;
            }
        }

        for (int i = 0; i < outCount; ++i) {
            const SwPointF p = outputPortScenePos(i);
            if (swDistance(p, scenePoint) <= radius) {
                out.hit = true;
                out.type = OutPort;
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

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override {
        if (!painter || !isVisible()) {
            return;
        }

        const double s = std::max(0.0001, ctx.scale);
        auto px = [&](int v) -> int { return roundToInt(static_cast<double>(v) * s); };
        auto px0 = [&](int v) -> int { return std::max(0, px(v)); };
        auto px1 = [&](int v) -> int { return std::max(1, px(v)); };

        const bool drawShadow = (s >= 0.12);
        const bool drawHeader = (s >= 0.12);
        const bool drawPorts = (s >= 0.12);
        const bool drawText = (s >= 0.50);
        const bool drawBorder = (s >= 0.35);
        const bool drawFooter = (drawText && !m_bodyText.isEmpty());

        const SwRectF br = boundingRect();
        SwRectF sceneRect = br;
        const SwPointF sp = scenePos();
        sceneRect.translate(sp.x, sp.y);
        const SwRect vr = ctx.mapFromScene(sceneRect);

        const bool selected = isSelected();

        const SwColor shadow = SwColor{10, 12, 18};
        const SwColor bg = selected ? SwColor{30, 41, 59} : SwColor{24, 32, 46};
        const SwColor border = selected ? clampColor(m_accent.r + 30, m_accent.g + 30, m_accent.b + 30) : SwColor{51, 65, 85};
        const int borderWidth = drawBorder ? px1(selected ? 3 : 2) : 0;
        const SwColor headerBg = SwColor{15, 23, 42};
        const SwColor titleColor = SwColor{226, 232, 240};

        // Drop shadow.
        if (drawShadow) {
            SwRect shadowRect = vr;
            shadowRect.x += px(6);
            shadowRect.y += px(8);
            painter->fillRoundedRect(shadowRect, px0(kRadius + 2), shadow, shadow, 0);
        }

        painter->fillRoundedRect(vr, px0(kRadius), bg, border, borderWidth);

        // Title bar (draw taller, then mask its bottom rounded corners).
        if (drawHeader) {
            const int headerH = std::max(1, px0(kHeaderHeight));
            const int radius = px0(kRadius);

            SwRect headerRect = vr;
            headerRect.height = px0(kHeaderHeight + kRadius);
            painter->fillRoundedRect(headerRect, radius, headerBg, headerBg, 0);

            SwRect stripe = vr;
            stripe.width = px1(8);
            stripe.height = headerH;
            painter->fillRect(stripe, m_accent, m_accent, 0);

            if (s >= 0.25 && radius > 0) {
                SwRect headerMask = vr;
                headerMask.y += headerH;
                headerMask.height = radius;
                painter->fillRect(headerMask, bg, bg, 0);
                painter->drawLine(vr.x,
                                  vr.y + headerH,
                                  vr.x + vr.width,
                                  vr.y + headerH,
                                  SwColor{71, 85, 105},
                                  px1(1));
            }

            if (drawText) {
                SwRect titleRect = vr;
                titleRect.height = headerH;
                const int titleMargin = px0(14);
                titleRect.x += titleMargin;
                titleRect.width = std::max(0, titleRect.width - titleMargin * 2);
                painter->drawText(titleRect,
                                  m_title,
                                  DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine,
                                  titleColor,
                                  SwFont(L"Segoe UI", px1(10), SemiBold));
            }
        } else {
            // Even in low LOD, keep a 1px accent stripe so nodes stay identifiable.
            SwRect stripe = vr;
            stripe.width = 1;
            stripe.height = std::max(1, px0(kHeaderHeight));
            painter->fillRect(stripe, m_accent, m_accent, 0);
        }

        // Ports + labels.
        if (drawPorts) {
            const int portRadius = px1(kPortRadius);
            const int portBorderW = px1(2);
            const int portLabelGap = px0(8);
            const int portLabelPad = px0(24);
            const int portLabelHeight = px1(20);
            const int portLabelYOffset = px0(10);
            const SwFont portFont(L"Segoe UI", px1(9), Normal);

            auto drawPort = [&](const SwRect& pr, const SwColor& fill, const SwColor& borderCol, bool hovered) {
#if defined(_WIN32)
                if (void* native = painter->nativeHandle()) {
                    HDC hdc = static_cast<HDC>(native);
                    Gdiplus::Graphics graphics(hdc);
                    if (graphics.GetLastStatus() == Gdiplus::Ok) {
                        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

                        auto toRectF = [](const SwRect& r) -> Gdiplus::RectF {
                            return Gdiplus::RectF(static_cast<Gdiplus::REAL>(r.x),
                                                  static_cast<Gdiplus::REAL>(r.y),
                                                  static_cast<Gdiplus::REAL>(r.width),
                                                  static_cast<Gdiplus::REAL>(r.height));
                        };

                        if (hovered) {
                            const int ring = px1(4);
                            const int glow = ring + px1(3);
                            SwRect glowRect{pr.x - glow, pr.y - glow, pr.width + glow * 2, pr.height + glow * 2};
                            Gdiplus::SolidBrush glowBrush(Gdiplus::Color(70, fill.r, fill.g, fill.b));
                            graphics.FillEllipse(&glowBrush, toRectF(glowRect));

                            SwRect ringRect{pr.x - ring, pr.y - ring, pr.width + ring * 2, pr.height + ring * 2};
                            Gdiplus::SolidBrush ringFillBrush(Gdiplus::Color(255, bg.r, bg.g, bg.b));
                            graphics.FillEllipse(&ringFillBrush, toRectF(ringRect));
                            const int ringW = std::max(1, px1(2));
                            Gdiplus::Pen ringPen(Gdiplus::Color(255, fill.r, fill.g, fill.b),
                                                 static_cast<Gdiplus::REAL>(ringW));
                            ringPen.SetLineJoin(Gdiplus::LineJoinRound);
                            graphics.DrawEllipse(&ringPen, toRectF(ringRect));
                        }

                        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, fill.r, fill.g, fill.b));
                        graphics.FillEllipse(&fillBrush, toRectF(pr));

                        const int bw = std::max(0, hovered ? (portBorderW + 1) : portBorderW);
                        if (bw > 0) {
                            Gdiplus::Pen pen(Gdiplus::Color(255, borderCol.r, borderCol.g, borderCol.b),
                                             static_cast<Gdiplus::REAL>(bw));
                            pen.SetLineJoin(Gdiplus::LineJoinRound);
                            graphics.DrawEllipse(&pen, toRectF(pr));
                        }

                        if (hovered) {
                            const int dot = std::max(2, pr.width / 3);
                            SwRect dotRect{pr.x + pr.width / 4 - dot / 2, pr.y + pr.height / 4 - dot / 2, dot, dot};
                            Gdiplus::SolidBrush dotBrush(Gdiplus::Color(80, 255, 255, 255));
                            graphics.FillEllipse(&dotBrush, toRectF(dotRect));
                        }
                        return;
                    }
                }
#endif
                if (hovered) {
                    const int ring = px1(4);
                    SwRect ringRect{pr.x - ring, pr.y - ring, pr.width + ring * 2, pr.height + ring * 2};
                    painter->fillEllipse(ringRect, bg, fill, px1(2));
                }
                painter->fillEllipse(pr, fill, borderCol, hovered ? (portBorderW + 1) : portBorderW);
            };

            const int portRows = std::max(inputCount(), outputCount());
            for (int row = 0; row < portRows; ++row) {
                // In
                if (row < inputCount()) {
                    const SwPointF pScene = inputPortScenePos(row);
                    const SwPointF pView = ctx.mapFromScene(pScene);
                    SwRect pr{roundToInt(pView.x - portRadius),
                              roundToInt(pView.y - portRadius),
                              portRadius * 2,
                              portRadius * 2};
                    const bool hovered = isPortHovered(InPort, row);
                    const SwColor baseFill = SwColor{59, 130, 246};
                    const SwColor fill = hovered ? clampColor(baseFill.r + 30, baseFill.g + 30, baseFill.b + 30) : baseFill;
                    const SwColor borderCol = hovered ? clampColor(15 + 30, 23 + 30, 42 + 30) : SwColor{15, 23, 42};
                    drawPort(pr, fill, borderCol, hovered);

                    if (drawText) {
                        SwRect label{pr.x + pr.width + portLabelGap,
                                     pr.y - portLabelYOffset,
                                     std::max(0, vr.width / 2 - portLabelPad),
                                     portLabelHeight};
                        painter->drawText(label,
                                          m_inputs[row],
                                          DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine,
                                          SwColor{203, 213, 225},
                                          portFont);
                    }
                }

                // Out
                if (row < outputCount()) {
                    const SwPointF pScene = outputPortScenePos(row);
                    const SwPointF pView = ctx.mapFromScene(pScene);
                    SwRect pr{roundToInt(pView.x - portRadius),
                              roundToInt(pView.y - portRadius),
                              portRadius * 2,
                              portRadius * 2};
                    const bool hovered = isPortHovered(OutPort, row);
                    const SwColor baseFill = SwColor{16, 185, 129};
                    const SwColor fill = hovered ? clampColor(baseFill.r + 30, baseFill.g + 30, baseFill.b + 30) : baseFill;
                    const SwColor borderCol = hovered ? clampColor(15 + 30, 23 + 30, 42 + 30) : SwColor{15, 23, 42};
                    drawPort(pr, fill, borderCol, hovered);

                    if (drawText) {
                        SwRect label{std::max(vr.x, pr.x - (vr.width / 2)),
                                     pr.y - portLabelYOffset,
                                     std::max(0, vr.width / 2 - portLabelPad),
                                     portLabelHeight};
                        painter->drawText(label,
                                          m_outputs[row],
                                          DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine,
                                          SwColor{203, 213, 225},
                                          portFont);
                    }
                }
            }
        }

        if (drawFooter) {
            const int footerH = px0(kFooterHeight);
            const int footerY = vr.y + vr.height - footerH;
            const int footerInset = px0(10);
            painter->drawLine(vr.x + footerInset,
                              footerY,
                              vr.x + vr.width - footerInset,
                              footerY,
                              SwColor{51, 65, 85},
                              px1(1));

            const int pillX = px0(16);
            const int pillY = px0(8);
            const int pillW = px0(32);
            const int pillH = px0(16);

            SwRect pill{vr.x + pillX,
                        footerY + pillY,
                        std::max(0, vr.width - pillW),
                        std::max(0, footerH - pillH)};
            const SwColor pillBg = selected ? SwColor{51, 65, 85} : SwColor{30, 41, 59};
            const SwColor pillBorder = selected ? clampColor(m_accent.r + 40, m_accent.g + 40, m_accent.b + 40) : SwColor{71, 85, 105};
            painter->fillRoundedRect(pill, px0(10), pillBg, pillBorder, px1(1));
            painter->drawText(pill,
                              m_bodyText,
                              DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine,
                              SwColor{226, 232, 240},
                              SwFont(L"Segoe UI", px1(10), SemiBold));
        }
    }

private:
    SwPointF inputPortLocalPos(int index) const {
        const double y = static_cast<double>(kHeaderHeight + kBodyPaddingY + index * kPortSpacing + kPortSpacing / 2);
        return SwPointF(static_cast<double>(kPortMarginX), y);
    }

    SwPointF outputPortLocalPos(int index) const {
        const SwRectF br = boundingRect();
        const double y = static_cast<double>(kHeaderHeight + kBodyPaddingY + index * kPortSpacing + kPortSpacing / 2);
        return SwPointF(br.width - static_cast<double>(kPortMarginX), y);
    }

    static const int kWidth = 320;
    static const int kHeaderHeight = 34;
    static const int kRadius = 14;
    static const int kPortSpacing = 26;
    static const int kBodyPaddingY = 12;
    static const int kPortMarginX = 16;
    static const int kPortRadius = 7;
    static const int kFooterHeight = 34;

    int m_id{0};
    SwString m_typeKey;
    SwString m_title;
    std::vector<SwString> m_inputs;
    std::vector<SwString> m_outputs;
    SwColor m_accent{99, 102, 241};
    SwString m_bodyText;
    PortHit m_hoverPort{};
    std::vector<bool> m_multiIn;
    std::vector<bool> m_multiOut;
};

class SwConnectionItem final : public SwGraphicsItem {
public:
    SwConnectionItem(SwNodeItem* outNode, int outPort)
        : m_outNode(outNode), m_outPort(outPort) {
        setZValue(-10.0);
    }

    void setFlowPulseDurationMs(int ms) {
        m_flowPulseDurationMs = std::max(0, ms);
        if (m_flowPulseDurationMs == 0) {
            clearFlow();
        }
    }

    int flowPulseDurationMs() const { return m_flowPulseDurationMs; }

    void notifyDataFlow() {
        if (!isComplete() || m_flowPulseDurationMs <= 0) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!m_flowActive) {
            m_flowActive = true;
            m_flowStart = now;
        }
        m_flowLast = now;
        update();
    }

    void clearFlow() {
        if (!m_flowActive) {
            return;
        }
        m_flowActive = false;
        update();
    }

    bool tickFlow(const std::chrono::steady_clock::time_point& now) {
        if (!m_flowActive) {
            return false;
        }
        if (m_flowPulseDurationMs <= 0) {
            m_flowActive = false;
            update();
            return false;
        }
        const auto inactiveMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_flowLast).count();
        if (inactiveMs >= m_flowPulseDurationMs) {
            m_flowActive = false;
            update();
            return false;
        }
        return true;
    }

    bool isFlowActive() const { return m_flowActive; }

    void setHovered(bool on) {
        if (m_hovered == on) {
            return;
        }
        m_hovered = on;
        update();
    }

    bool isHovered() const { return m_hovered; }

    void setInEndpoint(SwNodeItem* inNode, int inPort) {
        m_inNode = inNode;
        m_inPort = inPort;
        m_hasSceneEndPoint = false;
        update();
    }

    void setSceneEndPoint(const SwPointF& scenePoint) {
        m_sceneEndPoint = scenePoint;
        m_hasSceneEndPoint = true;
        update();
    }

    SwNodeItem* outNode() const { return m_outNode; }
    int outPort() const { return m_outPort; }

    SwNodeItem* inNode() const { return m_inNode; }
    int inPort() const { return m_inPort; }

    bool isComplete() const { return m_inNode != nullptr; }

    SwRectF boundingRect() const override {
        // Not used for culling (the view repaints everything). Provide a safe bound.
        return SwRectF(-100000.0, -100000.0, 200000.0, 200000.0);
    }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override {
        if (!painter || !isVisible()) {
            return;
        }
        if (!m_outNode || m_outPort < 0) {
            return;
        }

        SwPointF endScene{};
        if (m_inNode) {
            endScene = m_inNode->inputPortScenePos(m_inPort);
        } else if (m_hasSceneEndPoint) {
            endScene = m_sceneEndPoint;
        } else {
            return;
        }

        SwizioNodes::ConnectionPainter::PaintData data;
        data.startScene = m_outNode->outputPortScenePos(m_outPort);
        data.endScene = endScene;
        data.complete = (m_inNode != nullptr);
        data.hovered = m_hovered;
        data.selected = isSelected();
        if (m_outNode) {
            data.useCustomColor = true;
            data.customColor = m_outNode->accentColor();
        }

        if (m_flowActive && m_flowPulseDurationMs > 0 && m_inNode) {
            const auto now = std::chrono::steady_clock::now();
            const auto inactiveMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_flowLast).count();
            if (inactiveMs < m_flowPulseDurationMs) {
                const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_flowStart).count();
                data.flowActive = true;
                data.flowPhase = std::fmod(static_cast<double>(ageMs) / static_cast<double>(m_flowPulseDurationMs), 1.0);
            }
        }

        SwizioNodes::ConnectionPainter::paint(painter, ctx, data);
    }

private:
    SwNodeItem* m_outNode{nullptr};
    int m_outPort{0};

    SwNodeItem* m_inNode{nullptr};
    int m_inPort{0};

    SwPointF m_sceneEndPoint{};
    bool m_hasSceneEndPoint{false};
    bool m_hovered{false};

    int m_flowPulseDurationMs{300};
    bool m_flowActive{false};
    std::chrono::steady_clock::time_point m_flowStart{};
    std::chrono::steady_clock::time_point m_flowLast{};
};

class SwNodeRegistryPopup final : public SwFrame {
    SW_OBJECT(SwNodeRegistryPopup, SwFrame)

public:
    enum class ItemKind {
        CreateNode,
        Command,
    };

    enum class CommandId {
        Undo,
        Redo,
        Cut,
        Copy,
        Paste,
        Delete,
    };

    struct Item {
        ItemKind kind{ItemKind::CreateNode};
        CommandId command{CommandId::Undo};
        SwString display;
        SwString payload;
    };

    class SearchLineEdit final : public SwLineEdit {
        SW_OBJECT(SearchLineEdit, SwLineEdit)

    public:
        explicit SearchLineEdit(const SwString& placeholderText, SwWidget* parent = nullptr)
            : SwLineEdit(placeholderText, parent) {}

        void setEscapeHandler(std::function<void()> fn) { m_onEscape = std::move(fn); }
        void setReturnHandler(std::function<void()> fn) { m_onReturn = std::move(fn); }
        void setDownHandler(std::function<void()> fn) { m_onDown = std::move(fn); }

    protected:
        void keyPressEvent(KeyEvent* event) override {
            if (!event) {
                return;
            }

            if (SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
                if (m_onEscape) {
                    m_onEscape();
                }
                event->accept();
                return;
            }

            if (SwWidgetPlatformAdapter::isReturnKey(event->key())) {
                if (m_onReturn) {
                    m_onReturn();
                }
                event->accept();
                return;
            }

            if (SwWidgetPlatformAdapter::isDownArrowKey(event->key())) {
                if (m_onDown) {
                    m_onDown();
                }
                event->accept();
                return;
            }

            SwLineEdit::keyPressEvent(event);
        }

    private:
        std::function<void()> m_onEscape;
        std::function<void()> m_onReturn;
        std::function<void()> m_onDown;
    };

    explicit SwNodeRegistryPopup(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        setFrameShape(SwFrame::Shape::StyledPanel);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setStyleSheet(R"(
            SwNodeRegistryPopup {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 14px;
            }
        )");

        m_search = new SearchLineEdit("Search...", this);
        m_search->setStyleSheet(R"(
            SearchLineEdit {
                background-color: rgb(248, 250, 252);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 12px;
                padding: 6px 10px;
                color: rgb(15, 23, 42);
            }
        )");

        m_list = new SwListWidget(this);
        m_list->setViewportPadding(6);
        m_list->setRowHeight(28);

        SwObject::connect(m_search, &SwLineEdit::TextChanged, this, [this](const SwString& text) { applyFilter_(text); });
        SwObject::connect(m_list, &SwListView::clicked, this, [this](const SwModelIndex& idx) { activateIndex_(idx); });
        SwObject::connect(m_list, &SwListView::activated, this, [this](const SwModelIndex& idx) { activateIndex_(idx); });

        m_search->setEscapeHandler([this]() { closeRequested(); });
        m_search->setReturnHandler([this]() { activateCurrent_(); });
        m_search->setDownHandler([this]() {
            if (m_list) {
                m_list->setFocus(true);
            }
        });
    }

    void setActivateHandler(std::function<void(const Item&)> fn) { m_activate = std::move(fn); }

    void setItems(std::vector<Item> items) {
        m_items = std::move(items);
        applyFilter_(m_search ? m_search->getText() : SwString());
    }

    void clearSearch() {
        if (m_search) {
            m_search->setText(SwString());
        }
    }

    void focusSearch() {
        if (m_search) {
            m_search->setFocus(true);
        }
    }

signals:
    DECLARE_SIGNAL_VOID(closeRequested);

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout_();
    }

private:
    void updateLayout_() {
        const SwRect r = frameGeometry();
        const int pad = 10;
        const int searchH = 34;
        const int gap = 8;

        if (m_search) {
            m_search->move(r.x + pad, r.y + pad);
            m_search->resize(std::max(0, r.width - 2 * pad), searchH);
        }

        if (m_list) {
            const int y = r.y + pad + searchH + gap;
            m_list->move(r.x + pad, y);
            m_list->resize(std::max(0, r.width - 2 * pad), std::max(0, r.height - (y - r.y) - pad));
        }
    }

    void applyFilter_(const SwString& text) {
        m_filtered.clear();
        m_filtered.reserve(m_items.size());
        for (const Item& it : m_items) {
            if (containsCaseInsensitive(it.display, text) || containsCaseInsensitive(it.payload, text)) {
                m_filtered.push_back(it);
            }
        }

        if (!m_list) {
            return;
        }

        m_list->clear();
        for (const Item& it : m_filtered) {
            m_list->addItem(it.display);
        }

        if (m_list->count() > 0 && m_list->selectionModel() && m_list->model()) {
            SwModelIndex idx = m_list->model()->index(0, 0);
            if (idx.isValid()) {
                m_list->selectionModel()->setCurrentIndex(idx);
            }
        }

        update();
    }

    void activateCurrent_() {
        if (!m_list || !m_list->selectionModel()) {
            return;
        }
        const SwModelIndex idx = m_list->selectionModel()->currentIndex();
        if (idx.isValid()) {
            activateIndex_(idx);
        }
    }

    void activateIndex_(const SwModelIndex& idx) {
        if (!idx.isValid()) {
            return;
        }
        const int row = idx.row();
        if (row < 0 || row >= static_cast<int>(m_filtered.size())) {
            return;
        }

        const Item it = m_filtered[static_cast<size_t>(row)];
        if (m_activate) {
            m_activate(it);
        }
        closeRequested();
    }

    SearchLineEdit* m_search{nullptr};
    SwListWidget* m_list{nullptr};
    std::vector<Item> m_items;
    std::vector<Item> m_filtered;
    std::function<void(const Item&)> m_activate;
};

class SwNodeRegistryOverlay final : public SwWidget {
    SW_OBJECT(SwNodeRegistryOverlay, SwWidget)

public:
    SwNodeRegistryOverlay(SwWidget* root, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_root(root) {
        setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setFocusPolicy(FocusPolicyEnum::Strong);
    }

    void setPopup(SwNodeRegistryPopup* popup) { m_popup = popup; }

signals:
    DECLARE_SIGNAL_VOID(outsideClicked);

protected:
    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (m_popup) {
            const SwRect r = m_popup->frameGeometry();
            const bool inside = event->x() >= r.x && event->x() <= (r.x + r.width) &&
                                event->y() >= r.y && event->y() <= (r.y + r.height);
            if (!inside) {
                outsideClicked();
                if (m_root) {
                    MouseEvent forwarded(EventType::MousePressEvent, event->x(), event->y(), event->button());
                    static_cast<SwWidgetInterface*>(m_root)->mousePressEvent(&forwarded);
                }
                event->accept();
                return;
            }
        }

        SwWidget::mousePressEvent(event);
    }

private:
    SwWidget* m_root{nullptr};
    SwNodeRegistryPopup* m_popup{nullptr};
};

class SwNodeEditorView final : public SwGraphicsView {
    SW_OBJECT(SwNodeEditorView, SwGraphicsView)

public:
    explicit SwNodeEditorView(SwWidget* parent = nullptr)
        : SwGraphicsView(parent)
        , m_undo(this) {
        setStyleSheet(R"(
            SwNodeEditorView {
                background-color: rgba(0,0,0,0);
                border-color: rgb(30, 41, 59);
                border-width: 1px;
                border-radius: 16px;
            }
        )");

        registerDefaultNodeTypes_();

        m_graphRegistry = std::make_shared<SwizioNodes::NodeDelegateModelRegistry>();
        m_graphRegistry->registerModel<InputNodeModel>();
        m_graphRegistry->registerModel<AddNodeModel>();
        m_graphRegistry->registerModel<TimesNodeModel>();
        m_graphRegistry->registerModel<GenericNodeModel>();
        m_graphRegistry->registerModel<OutputNodeModel>();

        m_graph.reset(new SwizioNodes::DataFlowGraphModel("NodeEditorExample", m_graphRegistry));

        SwObject::connect(m_graph.get(),
                          &SwizioNodes::DataFlowGraphModel::outPortDataUpdated,
                          this,
                          [this](SwizioNodes::NodeId const nodeId, SwizioNodes::PortIndex const portIndex) {
                              onModelOutPortDataUpdated_(nodeId, portIndex);
                          });
        SwObject::connect(m_graph.get(),
                          &SwizioNodes::DataFlowGraphModel::inPortDataWasSet,
                          this,
                          [this](SwizioNodes::NodeId const nodeId, SwizioNodes::PortType const portType, SwizioNodes::PortIndex const portIndex) {
                              onModelInPortDataWasSet_(nodeId, portType, portIndex);
                          });

        auto* dfScene = new SwizioNodes::DataFlowGraphicsScene(*m_graph, this);
        dfScene->setSceneRect(SwRectF(-4000.0, -4000.0, 8000.0, 8000.0));
        setScene(dfScene);
    }

    CUSTOM_PROPERTY(int, LinkFlowPulseMs, 300) {
        const int ms = std::max(0, value);
        for (SwConnectionItem* c : m_connections) {
            if (c) {
                c->setFlowPulseDurationMs(ms);
            }
        }
        if (ms <= 0) {
            if (m_flowTimer && m_flowTimer->isActive()) {
                m_flowTimer->stop();
            }
            for (SwConnectionItem* c : m_connections) {
                if (c) {
                    c->clearFlow();
                }
            }
            if (scene()) {
                scene()->changed();
            }
        }
    }

public:
    SwNodeItem* addNode(const SwString& title, const SwPointF& scenePos) {
        const int id = ++m_nextNodeId;
        std::vector<SwString> inPorts;
        std::vector<SwString> outPorts;
        inPorts.push_back("in");
        outPorts.push_back("out");

        auto* node = new SwNodeItem(id, title, inPorts, outPorts);
        node->setPos(scenePos);
        node->setZValue(10.0);
        scene()->addItem(node);
        m_nodes.push_back(node);
        (void)ensureModelNodeForUiNode_(node);
        return node;
    }

    SwNodeItem* addNode(const SwString& title,
                        const std::vector<SwString>& inputs,
                        const std::vector<SwString>& outputs,
                        const SwPointF& scenePos,
                        const SwColor& accentColor = SwColor{99, 102, 241},
                        const SwString& typeKey = SwString()) {
        const int id = ++m_nextNodeId;
        auto* node = new SwNodeItem(id, title, inputs, outputs, accentColor, typeKey);
        node->setPos(scenePos);
        node->setZValue(10.0);
        scene()->addItem(node);
        m_nodes.push_back(node);
        (void)ensureModelNodeForUiNode_(node);
        return node;
    }

    SwConnectionItem* connect(SwNodeItem* outNode, int outPort, SwNodeItem* inNode, int inPort) {
        if (!outNode || !inNode) {
            return nullptr;
        }
        if (outPort < 0 || outPort >= outNode->outputCount()) {
            return nullptr;
        }
        if (inPort < 0 || inPort >= inNode->inputCount()) {
            return nullptr;
        }

        disarmFlow_();

        if (!ensureModelNodeForUiNode_(outNode) || !ensureModelNodeForUiNode_(inNode)) {
            return nullptr;
        }

        SwizioNodes::ConnectionId cid;
        cid.outNodeId = static_cast<SwizioNodes::NodeId>(outNode->id());
        cid.outPortIndex = static_cast<SwizioNodes::PortIndex>(outPort);
        cid.inNodeId = static_cast<SwizioNodes::NodeId>(inNode->id());
        cid.inPortIndex = static_cast<SwizioNodes::PortIndex>(inPort);
        if (m_graph && !m_graph->connectionPossible(cid)) {
            return nullptr;
        }

        if (!inNode->allowsMultipleConnections(SwNodeItem::InPort, inPort)) {
            for (SwConnectionItem* c : m_connections) {
                if (c && c->inNode() == inNode && c->inPort() == inPort) {
                    return nullptr;
                }
            }
        }

        if (!outNode->allowsMultipleConnections(SwNodeItem::OutPort, outPort)) {
            for (SwConnectionItem* c : m_connections) {
                if (c && c->outNode() == outNode && c->outPort() == outPort) {
                    return nullptr;
                }
            }
        }

        auto* conn = new SwConnectionItem(outNode, outPort);
        conn->setInEndpoint(inNode, inPort);
        conn->setFlowPulseDurationMs(std::max(0, getLinkFlowPulseMs()));
        scene()->addItem(conn);
        m_connections.push_back(conn);

        if (m_graph) {
            m_graph->addConnection(cid);
        }
        return conn;
    }

    void populateDemoGraph() {
        setScale(1.0);
        setScroll(0.0, 0.0);

        // Background grid.
        scene()->addItem(new SwGridBackgroundItem());

        auto* n1 = createNodeFromType_("Input", SwPointF(80.0, 90.0));
        auto* n2 = createNodeFromType_("Add", SwPointF(420.0, 240.0));
        auto* n3 = createNodeFromType_("Times", SwPointF(420.0, 90.0));
        auto* n4 = createNodeFromType_("Output", SwPointF(700.0, 120.0));

        connect(n1, 0, n2, 0);
        connect(n1, 0, n2, 1);
        connect(n2, 0, n3, 0);
        connect(n1, 0, n3, 1);
        connect(n3, 0, n4, 0);

        m_undo.clear();
        m_pasteSerial = 0;
        selectNode_(nullptr);
    }

protected:
    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        m_lastMouseView = SwPointF(event->x(), event->y());
        const SwPointF scenePos = mapToScene(m_lastMouseView);

        if (event->button() != SwMouseButton::Right) {
            // If a real SwWidget exists under the cursor (proxy widget), let it handle the click.
            SwWidget* child = getChildUnderCursor(event->x(), event->y());
            if (child && child != this) {
                if (SwNodeItem* hit = topNodeAt_(scenePos)) {
                    selectNode_(hit);
                }
                SwGraphicsView::mousePressEvent(event);
                return;
            }
        }

        if (event->button() == SwMouseButton::Right) {
            // Do not clear multi-selection when right-clicking on empty space.
            if (SwNodeItem* hit = topNodeAt_(scenePos)) {
                if (!hit->isSelected()) {
                    selectNode_(hit);
                } else {
                    ensureNodeSelected_(hit);
                }
            } else if (m_selectedConnection) {
                m_selectedConnection->setSelected(false);
                m_selectedConnection = nullptr;
                if (scene()) {
                    scene()->changed();
                }
            }
            showRegistryPopup_(event->x(), event->y(), scenePos);
            event->accept();
            return;
        }

        updateHover_(scenePos, m_lastMouseView, true);

        SwNodeItem* hitNode = topNodeAt_(scenePos);
        if (!hitNode && m_hoverConnection) {
            selectConnection_(m_hoverConnection);
            event->accept();
            return;
        }
        if (hitNode) {
            const SwNodeItem::PortHit port = hitNode->hitTestPort(scenePos);
            if (port.hit) {
                ensureNodeSelected_(hitNode);
                if (port.type == SwNodeItem::InPort) {
                    SwConnectionItem* c = nullptr;
                    if (m_hoverConnection && m_hoverConnection->inNode() == hitNode && m_hoverConnection->inPort() == port.index) {
                        c = m_hoverConnection;
                    }
                    if (!c) {
                        c = closestConnectionForInput_(hitNode, port.index, m_lastMouseView);
                    }
                    if (c) {
                        if (beginReconnection_(c, scenePos)) {
                            event->accept();
                            return;
                        }
                    }
                } else {
                    if (!hitNode->allowsMultipleConnections(SwNodeItem::OutPort, port.index)) {
                        if (m_hoverConnection && m_hoverConnection->outNode() == hitNode && m_hoverConnection->outPort() == port.index) {
                            if (beginReconnection_(m_hoverConnection, scenePos)) {
                                event->accept();
                                return;
                            }
                        }
                    }
                    beginConnection_(hitNode, port.index, scenePos);
                    event->accept();
                    return;
                }
            }

            if (event->isCtrlPressed()) {
                toggleNodeSelection_(hitNode);
                event->accept();
                return;
            }

            if (!hitNode->isSelected()) {
                selectNode_(hitNode);
            } else {
                ensureNodeSelected_(hitNode);
            }

            m_draggingNodes = true;
            m_dragNodes.clear();
            m_dragNodesStartPos.clear();
            for (SwNodeItem* n : m_nodes) {
                if (n && n->isSelected()) {
                    m_dragNodes.push_back(n);
                    m_dragNodesStartPos.push_back(n->pos());
                }
            }
            m_dragStartScene = scenePos;
            event->accept();
            return;
        }

        if (event->isShiftPressed()) {
            beginRubberBand_(scenePos, event->isCtrlPressed());
            event->accept();
            return;
        }

        // Clicked on empty space: start panning.
        selectNode_(nullptr);
        m_panning = true;
        m_panStartView = m_lastMouseView;
        m_panStartScroll = m_scroll;
        event->accept();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        m_lastMouseView = SwPointF(event->x(), event->y());
        const SwPointF scenePos = mapToScene(m_lastMouseView);

        if (m_previewConnection) {
            m_previewConnection->setSceneEndPoint(scenePos);
            updateHover_(scenePos, m_lastMouseView, false);
            event->accept();
            return;
        }

        if (m_rubberBandActive) {
            clearHover_();
            updateRubberBand_(scenePos);
            event->accept();
            return;
        }

        if (m_draggingNodes && !m_dragNodes.empty() && m_dragNodes.size() == m_dragNodesStartPos.size()) {
            clearHover_();
            const SwPointF delta(scenePos.x - m_dragStartScene.x, scenePos.y - m_dragStartScene.y);
            for (size_t i = 0; i < m_dragNodes.size(); ++i) {
                SwNodeItem* n = m_dragNodes[i];
                if (!n) {
                    continue;
                }
                const SwPointF start = m_dragNodesStartPos[i];
                n->setPos(start.x + delta.x, start.y + delta.y);
            }
            event->accept();
            return;
        }

        if (m_panning) {
            clearHover_();
            const double s = std::max(0.01, scale());
            const SwPointF deltaView(m_lastMouseView.x - m_panStartView.x, m_lastMouseView.y - m_panStartView.y);
            m_scroll = SwPointF(m_panStartScroll.x - deltaView.x / s, m_panStartScroll.y - deltaView.y / s);
            setScroll(m_scroll.x, m_scroll.y);
            event->accept();
            return;
        }

        updateHover_(scenePos, m_lastMouseView, true);
        SwGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        m_lastMouseView = SwPointF(event->x(), event->y());
        const SwPointF scenePos = mapToScene(m_lastMouseView);

        if (m_previewConnection) {
            finishConnection_(scenePos);
            event->accept();
            return;
        }

        if (m_rubberBandActive) {
            endRubberBand_();
            event->accept();
        }

        if (m_draggingNodes && !m_dragNodes.empty() && m_dragNodes.size() == m_dragNodesStartPos.size()) {
            std::vector<MoveNodesCommand::Entry> moved;
            moved.reserve(m_dragNodes.size());
            for (size_t i = 0; i < m_dragNodes.size(); ++i) {
                SwNodeItem* n = m_dragNodes[i];
                if (!n) {
                    continue;
                }
                const SwPointF before = m_dragNodesStartPos[i];
                const SwPointF after = n->pos();
                if (before.x != after.x || before.y != after.y) {
                    MoveNodesCommand::Entry e;
                    e.nodeId = n->id();
                    e.before = before;
                    e.after = after;
                    moved.push_back(e);
                }
            }
            m_draggingNodes = false;
            m_dragNodes.clear();
            m_dragNodesStartPos.clear();

            if (!moved.empty()) {
                m_undo.push(new MoveNodesCommand(this, std::move(moved)));
            }
        } else {
            m_draggingNodes = false;
            m_dragNodes.clear();
            m_dragNodesStartPos.clear();
        }
        m_panning = false;
        SwGraphicsView::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        m_lastMouseView = SwPointF(event->x(), event->y());
        const SwPointF scenePos = mapToScene(m_lastMouseView);
        if (!topNodeAt_(scenePos)) {
            showRegistryPopup_(event->x(), event->y(), scenePos);
            event->accept();
            return;
        }
        SwGraphicsView::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }

        const bool textInputFocused = hasTextInputFocus_();

        if (event->isCtrlPressed()) {
            if (SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'Z')) {
                if (event->isShiftPressed()) {
                    redo_();
                } else {
                    undo_();
                }
                event->accept();
                return;
            }
            if (SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'Y')) {
                redo_();
                event->accept();
                return;
            }
            if (!textInputFocused && SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'C')) {
                copySelected_();
                event->accept();
                return;
            }
            if (!textInputFocused && SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'X')) {
                cutSelected_();
                event->accept();
                return;
            }
            if (!textInputFocused && SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'V')) {
                paste_();
                event->accept();
                return;
            }
        }

        if (!textInputFocused && SwWidgetPlatformAdapter::isDeleteKey(event->key())) {
            deleteSelected_();
            event->accept();
            return;
        }

        // Zoom shortcuts (Ctrl +/-).
#if defined(_WIN32)
        const int plusKeyCode = VK_OEM_PLUS;
        const int minusKeyCode = VK_OEM_MINUS;
#else
        const int plusKeyCode = '+';
        const int minusKeyCode = '-';
#endif
        if (event->isCtrlPressed()) {
            if (event->key() == plusKeyCode) {
                zoomAround_(std::min(2.5, scale() * 1.1), m_lastMouseView);
                event->accept();
                return;
            }
            if (event->key() == minusKeyCode) {
                zoomAround_(std::max(0.05, scale() / 1.1), m_lastMouseView);
                event->accept();
                return;
            }
        }

        SwGraphicsView::keyPressEvent(event);
    }

    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }

        m_lastMouseView = SwPointF(event->x(), event->y());
        const double steps = static_cast<double>(event->delta()) / 120.0;
        if (steps == 0.0) {
            return;
        }

        const double factor = std::pow(1.12, steps);
        const double newScale = std::min(2.5, std::max(0.05, scale() * factor));
        zoomAround_(newScale, m_lastMouseView);
        event->accept();
    }

private:
    struct NodeType {
        SwString key;
        SwString title;
        std::vector<SwString> inputs;
        std::vector<SwString> outputs;
        SwColor accent{99, 102, 241};
        bool hasInlineLineEdit{false};
    };

    void registerDefaultNodeTypes_() {
        m_nodeTypes.clear();
        m_nodeTypes.reserve(12);

        {
            NodeType t;
            t.key = "Input";
            t.title = "Input";
            t.inputs = std::vector<SwString>{};
            t.outputs = std::vector<SwString>{"value"};
            t.accent = SwColor{16, 185, 129};
            t.hasInlineLineEdit = true;
            m_nodeTypes.push_back(t);
        }
        {
            NodeType t;
            t.key = "Add";
            t.title = "Add";
            t.inputs = std::vector<SwString>{"a", "b"};
            t.outputs = std::vector<SwString>{"sum"};
            t.accent = SwColor{59, 130, 246};
            m_nodeTypes.push_back(t);
        }
        {
            NodeType t;
            t.key = "Times";
            t.title = "Times";
            t.inputs = std::vector<SwString>{"a", "b"};
            t.outputs = std::vector<SwString>{"product"};
            t.accent = SwColor{245, 158, 11};
            m_nodeTypes.push_back(t);
        }
        {
            NodeType t;
            t.key = "Output";
            t.title = "Output";
            t.inputs = std::vector<SwString>{"in"};
            t.outputs = std::vector<SwString>{};
            t.accent = SwColor{168, 85, 247};
            m_nodeTypes.push_back(t);
        }
        {
            NodeType t;
            t.key = "Generic";
            t.title = "Generic";
            t.inputs = std::vector<SwString>{"in"};
            t.outputs = std::vector<SwString>{"out"};
            t.accent = SwColor{99, 102, 241};
            m_nodeTypes.push_back(t);
        }
    }

    const NodeType* nodeType_(const SwString& key) const {
        for (const NodeType& t : m_nodeTypes) {
            if (t.key == key) {
                return &t;
            }
        }
        return nullptr;
    }

    SwNodeItem* createNodeFromType_(const SwString& key, const SwPointF& scenePos) {
        const NodeType* type = nodeType_(key);
        SwNodeItem* node = nullptr;
        if (!type) {
            node = addNode(key, scenePos);
        } else {
            node = addNode(type->title, type->inputs, type->outputs, scenePos, type->accent, type->key);
        }
        if (!node || !scene() || !type) {
            return node;
        }

        if (type->hasInlineLineEdit) {
            const int nodeId = node->id();
            SwWidget* embedded = nullptr;
            if (m_graph) {
                if (auto* model = m_graph->delegateModel(static_cast<SwizioNodes::NodeId>(nodeId))) {
                    embedded = model->embeddedWidget();
                }
            }

            if (embedded) {
                if (auto* le = dynamic_cast<SwLineEdit*>(embedded)) {
                    le->resize(140, 34);
                    le->setText("42");
                    SwObject::connect(le,
                                      &SwLineEdit::TextChanged,
                                      this,
                                      [this](const SwString&) { armFlow_(); });
                    setInputNodeValueFromText_(nodeId, le->getText(), false);
                }

                auto* proxy = new SwGraphicsProxyWidget(embedded);
                proxy->setParentItem(node);
                proxy->setPos(90.0, 42.0);
                proxy->setWidgetBaseSize(140, 34);
                scene()->addItem(proxy);
            }
        }

        return node;
    }

    void ensureRegistryPopup_() {
        if (m_registryOverlay && m_registryPopup) {
            return;
        }

        SwWidget* root = findRootWidget(this);
        if (!root) {
            return;
        }

        if (!m_registryOverlay) {
            m_registryOverlay = new SwNodeRegistryOverlay(root, root);
            m_registryOverlay->move(0, 0);
            m_registryOverlay->resize(root->width(), root->height());
            SwObject::connect(m_registryOverlay, &SwNodeRegistryOverlay::outsideClicked, this, [this]() { cancelRegistryPopup_(); });
        }

        if (!m_registryPopup) {
            m_registryPopup = new SwNodeRegistryPopup(m_registryOverlay);
            m_registryPopup->resize(360, 420);
            m_registryPopup->setActivateHandler([this](const SwNodeRegistryPopup::Item& it) { handleRegistryItem_(it); });
            SwObject::connect(m_registryPopup, &SwNodeRegistryPopup::closeRequested, this, [this]() { cancelRegistryPopup_(); });
        }

        m_registryOverlay->setPopup(m_registryPopup);
        updateRegistryPopupItems_();

        SwObject::connect(root, &SwWidget::resized, this, [this](int w, int h) {
            if (m_registryOverlay) {
                m_registryOverlay->move(0, 0);
                m_registryOverlay->resize(w, h);
            }
        });
    }

    void updateRegistryPopupItems_() {
        if (!m_registryPopup) {
            return;
        }
        std::vector<SwNodeRegistryPopup::Item> items;
        items.reserve(m_nodeTypes.size() + 16);

        if (!m_pendingLink.active) {
            auto addCmd = [&](const char* label, SwNodeRegistryPopup::CommandId id) {
                SwNodeRegistryPopup::Item it;
                it.kind = SwNodeRegistryPopup::ItemKind::Command;
                it.command = id;
                it.display = SwString(label);
                items.push_back(it);
            };

            addCmd("Undo", SwNodeRegistryPopup::CommandId::Undo);
            addCmd("Redo", SwNodeRegistryPopup::CommandId::Redo);
            addCmd("Cut", SwNodeRegistryPopup::CommandId::Cut);
            addCmd("Copy", SwNodeRegistryPopup::CommandId::Copy);
            addCmd("Paste", SwNodeRegistryPopup::CommandId::Paste);
            addCmd("Delete", SwNodeRegistryPopup::CommandId::Delete);
        }

        for (const NodeType& t : m_nodeTypes) {
            SwNodeRegistryPopup::Item it;
            it.kind = SwNodeRegistryPopup::ItemKind::CreateNode;
            it.display = t.title;
            it.payload = t.key;
            items.push_back(it);
        }
        m_registryPopup->setItems(std::move(items));
    }

    void showRegistryPopup_(int globalX, int globalY, const SwPointF& scenePos) {
        ensureRegistryPopup_();
        if (!m_registryOverlay || !m_registryPopup) {
            return;
        }

        m_registryScenePos = scenePos;
        updateRegistryPopupItems_();
        m_registryPopup->clearSearch();

        const int w = m_registryPopup->width() > 0 ? m_registryPopup->width() : 360;
        const int h = m_registryPopup->height() > 0 ? m_registryPopup->height() : 420;

        const int maxX = std::max(0, m_registryOverlay->width() - w - 2);
        const int maxY = std::max(0, m_registryOverlay->height() - h - 2);

        const int px = clampInt(globalX, 2, maxX);
        const int py = clampInt(globalY, 2, maxY);

        m_registryPopup->move(px, py);
        m_registryPopup->resize(w, h);

        m_registryOverlay->show();
        m_registryPopup->show();
        m_registryOverlay->update();
        m_registryPopup->update();
        m_registryPopup->focusSearch();
    }

    void hideRegistryPopup_() {
        if (m_registryPopup) {
            m_registryPopup->hide();
        }
        if (m_registryOverlay) {
            m_registryOverlay->hide();
        }
    }

    void cancelRegistryPopup_() {
        cancelPendingLink_();
        hideRegistryPopup_();
    }

    void handleRegistryItem_(const SwNodeRegistryPopup::Item& it) {
        if (it.kind == SwNodeRegistryPopup::ItemKind::Command) {
            switch (it.command) {
            case SwNodeRegistryPopup::CommandId::Undo:
                undo_();
                return;
            case SwNodeRegistryPopup::CommandId::Redo:
                redo_();
                return;
            case SwNodeRegistryPopup::CommandId::Cut:
                cutSelected_();
                return;
            case SwNodeRegistryPopup::CommandId::Copy:
                copySelected_();
                return;
            case SwNodeRegistryPopup::CommandId::Paste:
                pasteAt_(m_registryScenePos);
                return;
            case SwNodeRegistryPopup::CommandId::Delete:
                deleteSelected_();
                return;
            }
            return;
        }

        if (it.kind == SwNodeRegistryPopup::ItemKind::CreateNode) {
            const int newNodeId = addNodeUndoable_(it.payload, m_registryScenePos);
            if (m_pendingLink.active) {
                if (tryConnectPendingLinkToNode_(newNodeId)) {
                    m_pendingLink = {};
                }
            }
            return;
        }
    }

    void zoomAround_(double newScale, const SwPointF& viewPos) {
        const SwRect vr = frameGeometry();
        const SwPointF anchor = mapToScene(viewPos);
        const double s = std::max(0.01, newScale);

        setScale(s);

        const double newScrollX = anchor.x - (viewPos.x - static_cast<double>(vr.x)) / s;
        const double newScrollY = anchor.y - (viewPos.y - static_cast<double>(vr.y)) / s;
        m_scroll = SwPointF(newScrollX, newScrollY);
        setScroll(m_scroll.x, m_scroll.y);
    }

    struct EvalValue {
        bool valid{false};
        double value{0.0};
    };

    struct ConnectionSnapshot {
        int outNodeId{0};
        int outPort{0};
        int inNodeId{0};
        int inPort{0};
    };

    struct NodeSnapshot {
        int id{0};
        SwString typeKey;
        SwString title;
        std::vector<SwString> inputs;
        std::vector<SwString> outputs;
        SwColor accent{99, 102, 241};
        SwPointF pos{};
        bool hasInlineLineEdit{false};
        SwString inlineLineEditText;
    };

    SwNodeItem* nodeById_(int id) const {
        for (SwNodeItem* n : m_nodes) {
            if (n && n->id() == id) {
                return n;
            }
        }
        return nullptr;
    }

    SwString inlineLineEditText_(SwNodeItem* node) const {
        if (!node) {
            return SwString();
        }
        for (SwGraphicsItem* child : node->childItems()) {
            auto* proxy = dynamic_cast<SwGraphicsProxyWidget*>(child);
            if (!proxy) {
                continue;
            }
            auto* le = dynamic_cast<SwLineEdit*>(proxy->widget());
            if (!le) {
                continue;
            }
            return le->getText();
        }
        return SwString();
    }

    void syncNodeConnectionPoliciesFromModel_(SwNodeItem* node) {
        if (!node || !m_graph) {
            return;
        }

        const SwizioNodes::NodeId nodeId = static_cast<SwizioNodes::NodeId>(std::max(0, node->id()));

        for (int i = 0; i < node->inputCount(); ++i) {
            SwAny policyAny =
                m_graph->portData(nodeId, SwizioNodes::PortType::In, static_cast<SwizioNodes::PortIndex>(i), SwizioNodes::PortRole::ConnectionPolicyRole);
            SwizioNodes::ConnectionPolicy policy = SwizioNodes::ConnectionPolicy::Many;
            if (!policyAny.typeName().empty()) {
                try {
                    policy = policyAny.get<SwizioNodes::ConnectionPolicy>();
                } catch (...) {
                }
            }
            node->setAllowsMultipleConnections(SwNodeItem::InPort, i, policy == SwizioNodes::ConnectionPolicy::Many);
        }

        for (int i = 0; i < node->outputCount(); ++i) {
            SwAny policyAny =
                m_graph->portData(nodeId, SwizioNodes::PortType::Out, static_cast<SwizioNodes::PortIndex>(i), SwizioNodes::PortRole::ConnectionPolicyRole);
            SwizioNodes::ConnectionPolicy policy = SwizioNodes::ConnectionPolicy::Many;
            if (!policyAny.typeName().empty()) {
                try {
                    policy = policyAny.get<SwizioNodes::ConnectionPolicy>();
                } catch (...) {
                }
            }
            node->setAllowsMultipleConnections(SwNodeItem::OutPort, i, policy == SwizioNodes::ConnectionPolicy::Many);
        }
    }

    bool ensureModelNodeForUiNode_(SwNodeItem* node) {
        if (!node || !m_graph || !m_graphRegistry) {
            return false;
        }

        const SwizioNodes::NodeId nodeId = static_cast<SwizioNodes::NodeId>(std::max(0, node->id()));
        if (m_graph->nodeExists(nodeId)) {
            return true;
        }

        const SwString modelName = node->typeKey();
        if (!m_graphRegistry->registeredModelCreators().count(modelName)) {
            return false;
        }

        SwJsonObject nodeJson;
        nodeJson["id"] = static_cast<long long>(nodeId);

        SwJsonObject internalData;
        internalData["model-name"] = modelName.toStdString();
        nodeJson["internal-data"] = internalData;

        SwJsonObject posJson;
        posJson["x"] = node->pos().x;
        posJson["y"] = node->pos().y;
        nodeJson["position"] = posJson;

        m_graph->loadNode(nodeJson);
        syncNodeConnectionPoliciesFromModel_(node);
        updateNodeBodyFromModel_(node);
        return true;
    }

    void setInputNodeValueFromText_(int nodeId, const SwString& text, bool armFlow) {
        if (!m_graph || nodeId <= 0) {
            return;
        }

        if (armFlow) {
            armFlow_();
        } else {
            disarmFlow_();
        }

        auto* model = m_graph->delegateModel<InputNodeModel>(static_cast<SwizioNodes::NodeId>(nodeId));
        if (!model) {
            return;
        }

        double v = 0.0;
        const bool ok = tryParseDoubleLoose(text, &v);
        model->setValue(v, ok);
    }

    void setModelNodePos_(int nodeId, const SwPointF& pos) {
        if (!m_graph || nodeId <= 0) {
            return;
        }
        (void)m_graph->setNodeData(static_cast<SwizioNodes::NodeId>(nodeId), SwizioNodes::NodeRole::Position, SwAny::from(pos));
    }

    bool isFlowArmed_() const {
        return std::chrono::steady_clock::now() <= m_flowArmedUntil;
    }

    void armFlow_() {
        const int ms = std::max(0, getLinkFlowPulseMs());
        if (ms <= 0) {
            return;
        }
        m_flowArmedUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    }

    void disarmFlow_() {
        m_flowArmedUntil = std::chrono::steady_clock::time_point{};
    }

    void updateNodeBodyFromModel_(SwNodeItem* node) {
        if (!node || !m_graph) {
            return;
        }

        const SwString type = node->typeKey();
        const SwizioNodes::NodeId nodeId = static_cast<SwizioNodes::NodeId>(std::max(0, node->id()));

        if (type == "Add" || type == "Times" || type == "Generic") {
            SwAny dataAny = m_graph->portData(nodeId, SwizioNodes::PortType::Out, 0u, SwizioNodes::PortRole::Data);
            std::shared_ptr<SwizioNodes::NodeData> base;
            if (!dataAny.typeName().empty()) {
                try {
                    base = dataAny.get<std::shared_ptr<SwizioNodes::NodeData>>();
                } catch (...) {
                    base.reset();
                }
            }
            auto data = std::dynamic_pointer_cast<AnyListData>(base);
            const AnyListData::NumberPayload num = data ? data->readNumber() : AnyListData::NumberPayload{};
            if (num.valid) {
                node->setBodyText(SwString("= ") + SwString::number(num.value, 6));
            } else {
                node->setBodyText("= ?");
            }
            return;
        }

        if (type == "Output") {
            if (auto* model = m_graph->delegateModel<OutputNodeModel>(nodeId)) {
                auto in = model->input();
                const AnyListData::NumberPayload num = in ? in->readNumber() : AnyListData::NumberPayload{};
                if (num.valid) {
                    node->setBodyText(SwString("Result: ") + SwString::number(num.value, 6));
                } else {
                    node->setBodyText("Result: ?");
                }
            } else {
                node->setBodyText("Result: ?");
            }
            return;
        }

        if (type == "Input") {
            node->setBodyText(SwString());
            return;
        }

        node->setBodyText(SwString());
    }

    void onModelOutPortDataUpdated_(SwizioNodes::NodeId const nodeId, SwizioNodes::PortIndex const portIndex) {
        SwNodeItem* node = nodeById_(static_cast<int>(nodeId));
        if (node) {
            updateNodeBodyFromModel_(node);
        }

        if (!isFlowArmed_()) {
            return;
        }

        for (SwConnectionItem* c : m_connections) {
            if (!c || !c->outNode()) {
                continue;
            }
            if (static_cast<SwizioNodes::NodeId>(std::max(0, c->outNode()->id())) != nodeId) {
                continue;
            }
            if (static_cast<SwizioNodes::PortIndex>(std::max(0, c->outPort())) != portIndex) {
                continue;
            }
            c->notifyDataFlow();
            ensureFlowTimerRunning_();
        }
    }

    void onModelInPortDataWasSet_(SwizioNodes::NodeId const nodeId,
                                 SwizioNodes::PortType const portType,
                                 SwizioNodes::PortIndex const /*portIndex*/) {
        if (portType != SwizioNodes::PortType::In) {
            return;
        }
        SwNodeItem* node = nodeById_(static_cast<int>(nodeId));
        if (!node) {
            return;
        }
        if (node->typeKey() != "Output") {
            return;
        }
        updateNodeBodyFromModel_(node);
    }

    void ensureFlowTimerRunning_() {
        if (std::max(0, getLinkFlowPulseMs()) <= 0) {
            return;
        }
        if (!m_flowTimer) {
            m_flowTimer = new SwTimer(this);
            SwObject::connect(m_flowTimer, &SwTimer::timeout, this, [this]() { tickFlowAnimations_(); });
        }
        if (!m_flowTimer->isActive()) {
            m_flowTimer->start(16);
        }
    }

    void tickFlowAnimations_() {
        const auto now = std::chrono::steady_clock::now();
        bool any = false;
        for (SwConnectionItem* c : m_connections) {
            if (c) {
                any |= c->tickFlow(now);
            }
        }
        if (scene()) {
            scene()->changed();
        }
        if (!any && m_flowTimer) {
            m_flowTimer->stop();
        }
    }

    void recomputeGraph_(bool animateFlow) {
        auto sameValue = [](const EvalValue& a, const EvalValue& b) -> bool {
            if (a.valid != b.valid) {
                return false;
            }
            if (!a.valid) {
                return true;
            }
            const double diff = std::abs(a.value - b.value);
            const double scale = std::max(1.0, std::max(std::abs(a.value), std::abs(b.value)));
            return diff <= 1e-12 * scale;
        };

        std::unordered_map<long long, SwConnectionItem*> inLinks;
        inLinks.reserve(m_connections.size() * 2 + 16);
        for (SwConnectionItem* c : m_connections) {
            if (!c || !c->outNode() || !c->inNode()) {
                continue;
            }
            inLinks[packNodePortKey(c->inNode()->id(), c->inPort())] = c;
        }

        std::unordered_map<long long, EvalValue> memo;
        memo.reserve(m_nodes.size() * 2 + 16);
        std::unordered_set<long long> changedOutputs;
        if (animateFlow) {
            changedOutputs.reserve(m_nodes.size() * 2 + 16);
        }
        std::unordered_set<long long> visiting;
        visiting.reserve(m_nodes.size() * 2 + 16);

        auto markChanged = [&](long long key, const EvalValue& v) {
            if (!animateFlow) {
                return;
            }
            auto itPrev = m_lastOutputs.find(key);
            if (itPrev == m_lastOutputs.end() || !sameValue(itPrev->second, v)) {
                changedOutputs.insert(key);
            }
        };

        std::function<EvalValue(int, int)> evalOut;
        std::function<EvalValue(int, int)> evalIn;

        evalIn = [&](int nodeId, int inPort) -> EvalValue {
            const long long k = packNodePortKey(nodeId, inPort);
            auto it = inLinks.find(k);
            if (it == inLinks.end()) {
                return EvalValue{};
            }

            SwConnectionItem* conn = it->second;
            if (!conn || !conn->outNode()) {
                return EvalValue{};
            }

            const long long outKey = packNodePortKey(conn->outNode()->id(), conn->outPort());
            const EvalValue v = evalOut(conn->outNode()->id(), conn->outPort());
            if (animateFlow && (changedOutputs.find(outKey) != changedOutputs.end())) {
                conn->notifyDataFlow();
                ensureFlowTimerRunning_();
            }
            return v;
        };

        evalOut = [&](int nodeId, int outPort) -> EvalValue {
            const long long k = packNodePortKey(nodeId, outPort);
            auto itMemo = memo.find(k);
            if (itMemo != memo.end()) {
                return itMemo->second;
            }
            if (visiting.find(k) != visiting.end()) {
                return EvalValue{};
            }
            visiting.insert(k);

            EvalValue out{};
            SwNodeItem* node = nodeById_(nodeId);
            if (!node) {
                visiting.erase(k);
                markChanged(k, out);
                memo[k] = out;
                return out;
            }

            const SwString type = node->typeKey();
            if (type == "Input" && outPort == 0) {
                double v = 0.0;
                if (tryParseDoubleLoose(inlineLineEditText_(node), &v)) {
                    out.valid = true;
                    out.value = v;
                }
            } else if (type == "Add" && outPort == 0) {
                const EvalValue a = evalIn(nodeId, 0);
                const EvalValue b = evalIn(nodeId, 1);
                if (a.valid && b.valid) {
                    out.valid = true;
                    out.value = a.value + b.value;
                }
            } else if (type == "Times" && outPort == 0) {
                const EvalValue a = evalIn(nodeId, 0);
                const EvalValue b = evalIn(nodeId, 1);
                if (a.valid && b.valid) {
                    out.valid = true;
                    out.value = a.value * b.value;
                }
            } else if (type == "Generic" && outPort == 0) {
                out = evalIn(nodeId, 0);
            }

            visiting.erase(k);
            markChanged(k, out);
            memo[k] = out;
            return out;
        };

        for (SwNodeItem* node : m_nodes) {
            if (!node) {
                continue;
            }

            const SwString type = node->typeKey();
            if (type == "Add" || type == "Times" || type == "Generic") {
                const EvalValue v = evalOut(node->id(), 0);
                node->setBodyText(v.valid ? (SwString("= ") + SwString::number(v.value, 6)) : SwString("= ?"));
            } else if (type == "Output") {
                const EvalValue v = evalIn(node->id(), 0);
                node->setBodyText(v.valid ? (SwString("Result: ") + SwString::number(v.value, 6)) : SwString("Result: ?"));
            } else {
                node->setBodyText(SwString());
            }
        }

        m_lastOutputs = std::move(memo);
    }

    NodeSnapshot snapshotNode_(SwNodeItem* node) const {
        NodeSnapshot snap;
        if (!node) {
            return snap;
        }

        snap.id = node->id();
        snap.typeKey = node->typeKey();
        snap.title = node->title();
        snap.inputs = node->inputs();
        snap.outputs = node->outputs();
        snap.accent = node->accentColor();
        snap.pos = node->pos();

        const SwString inlineText = inlineLineEditText_(node);
        if (!inlineText.isEmpty()) {
            snap.hasInlineLineEdit = true;
            snap.inlineLineEditText = inlineText;
        }

        return snap;
    }

    std::vector<ConnectionSnapshot> snapshotConnectionsForNode_(SwNodeItem* node) const {
        std::vector<ConnectionSnapshot> out;
        if (!node) {
            return out;
        }
        const int id = node->id();
        for (SwConnectionItem* c : m_connections) {
            if (!c || !c->outNode() || !c->inNode()) {
                continue;
            }
            if (c->outNode()->id() != id && c->inNode()->id() != id) {
                continue;
            }
            ConnectionSnapshot s;
            s.outNodeId = c->outNode()->id();
            s.outPort = c->outPort();
            s.inNodeId = c->inNode()->id();
            s.inPort = c->inPort();
            out.push_back(s);
        }
        return out;
    }

    ConnectionSnapshot snapshotConnection_(SwConnectionItem* conn) const {
        ConnectionSnapshot s;
        if (!conn || !conn->outNode() || !conn->inNode()) {
            return s;
        }
        s.outNodeId = conn->outNode()->id();
        s.outPort = conn->outPort();
        s.inNodeId = conn->inNode()->id();
        s.inPort = conn->inPort();
        return s;
    }

    NodeSnapshot makeNewNodeSnapshot_(const SwString& typeKey,
                                     const SwPointF& scenePos,
                                     const SwString& titleOverride = SwString(),
                                     const SwString& inlineTextOverride = SwString()) {
        NodeSnapshot snap;
        snap.id = ++m_nextNodeId;
        snap.typeKey = typeKey;
        snap.pos = scenePos;

        const NodeType* type = nodeType_(typeKey);
        if (type) {
            snap.title = titleOverride.isEmpty() ? type->title : titleOverride;
            snap.inputs = type->inputs;
            snap.outputs = type->outputs;
            snap.accent = type->accent;
            snap.hasInlineLineEdit = type->hasInlineLineEdit;
            if (snap.hasInlineLineEdit) {
                snap.inlineLineEditText = inlineTextOverride.isEmpty() ? SwString("42") : inlineTextOverride;
            }
            return snap;
        }

        snap.title = titleOverride.isEmpty() ? typeKey : titleOverride;
        snap.inputs = std::vector<SwString>{"in"};
        snap.outputs = std::vector<SwString>{"out"};
        snap.accent = SwColor{99, 102, 241};
        return snap;
    }

    SwNodeItem* restoreNode_(const NodeSnapshot& snap) {
        if (!scene() || snap.id <= 0) {
            return nullptr;
        }

        m_nextNodeId = std::max(m_nextNodeId, snap.id);

        auto* node = new SwNodeItem(snap.id,
                                    snap.title,
                                    snap.inputs,
                                    snap.outputs,
                                    snap.accent,
                                    snap.typeKey);
        node->setPos(snap.pos);
        node->setZValue(10.0);
        scene()->addItem(node);
        m_nodes.push_back(node);
        (void)ensureModelNodeForUiNode_(node);

        if (snap.hasInlineLineEdit) {
            const int nodeId = node->id();
            SwWidget* embedded = nullptr;
            if (m_graph) {
                if (auto* model = m_graph->delegateModel(static_cast<SwizioNodes::NodeId>(nodeId))) {
                    embedded = model->embeddedWidget();
                }
            }

            if (embedded) {
                if (auto* le = dynamic_cast<SwLineEdit*>(embedded)) {
                    const SwString text = snap.inlineLineEditText.isEmpty() ? SwString("42") : snap.inlineLineEditText;
                    le->resize(140, 34);
                    le->setText(text);
                    SwObject::connect(le,
                                      &SwLineEdit::TextChanged,
                                      this,
                                      [this](const SwString&) { armFlow_(); });
                    setInputNodeValueFromText_(nodeId, le->getText(), false);
                }

                auto* proxy = new SwGraphicsProxyWidget(embedded);
                proxy->setParentItem(node);
                proxy->setPos(90.0, 42.0);
                proxy->setWidgetBaseSize(140, 34);
                scene()->addItem(proxy);
            }
        }

        return node;
    }

    bool removeConnection_(const ConnectionSnapshot& snap) {
        for (size_t i = 0; i < m_connections.size(); ++i) {
            SwConnectionItem* c = m_connections[i];
            if (!c || !c->outNode() || !c->inNode()) {
                continue;
            }
            if (c->outNode()->id() != snap.outNodeId ||
                c->outPort() != snap.outPort ||
                c->inNode()->id() != snap.inNodeId ||
                c->inPort() != snap.inPort) {
                continue;
            }

            disarmFlow_();
            if (m_graph && snap.outNodeId > 0 && snap.inNodeId > 0) {
                SwizioNodes::ConnectionId cid;
                cid.outNodeId = static_cast<SwizioNodes::NodeId>(snap.outNodeId);
                cid.outPortIndex = static_cast<SwizioNodes::PortIndex>(std::max(0, snap.outPort));
                cid.inNodeId = static_cast<SwizioNodes::NodeId>(snap.inNodeId);
                cid.inPortIndex = static_cast<SwizioNodes::PortIndex>(std::max(0, snap.inPort));
                (void)m_graph->deleteConnection(cid);
            }

            if (scene()) {
                scene()->removeItem(c);
            }
            if (m_hoverConnection == c) {
                m_hoverConnection = nullptr;
            }
            if (m_selectedConnection == c) {
                m_selectedConnection = nullptr;
            }
            m_connections.erase(m_connections.begin() + static_cast<long long>(i));
            return true;
        }
        return false;
    }

    SwConnectionItem* createConnection_(const ConnectionSnapshot& snap) {
        SwNodeItem* outNode = nodeById_(snap.outNodeId);
        SwNodeItem* inNode = nodeById_(snap.inNodeId);
        if (!outNode || !inNode) {
            return nullptr;
        }
        return connect(outNode, snap.outPort, inNode, snap.inPort);
    }

    bool removeNodeById_(int id) {
        SwNodeItem* node = nodeById_(id);
        if (!node) {
            return false;
        }

        // Structural change -> never animate flow for this operation.
        disarmFlow_();

        if (m_selectedNode == node) {
            m_selectedNode = nullptr;
        }

        // Remove connections involving this node.
        for (size_t i = 0; i < m_connections.size();) {
            SwConnectionItem* c = m_connections[i];
            if (!c) {
                m_connections.erase(m_connections.begin() + static_cast<long long>(i));
                continue;
            }
            if (c->outNode() == node || c->inNode() == node) {
                if (m_hoverConnection == c) {
                    m_hoverConnection = nullptr;
                }
                if (m_selectedConnection == c) {
                    m_selectedConnection = nullptr;
                }
                if (scene()) {
                    scene()->removeItem(c);
                }
                m_connections.erase(m_connections.begin() + static_cast<long long>(i));
                continue;
            }
            ++i;
        }

        if (scene()) {
            scene()->removeItem(node);
        }
        m_nodes.erase(std::remove(m_nodes.begin(), m_nodes.end(), node), m_nodes.end());

        ensurePrimarySelectedNode_();
        if (scene()) {
            scene()->changed();
        }

        if (m_graph && id > 0) {
            (void)m_graph->deleteNode(static_cast<SwizioNodes::NodeId>(id));
        }
        return true;
    }

    class AddNodeCommand final : public SwUndoCommand {
    public:
        AddNodeCommand(SwNodeEditorView* view, NodeSnapshot snap)
            : SwUndoCommand("Add Node")
            , m_view(view)
            , m_snap(std::move(snap)) {}

        void undo() override {
            if (m_view) {
                (void)m_view->removeNodeById_(m_snap.id);
            }
        }

        void redo() override {
            if (!m_view) {
                return;
            }
            SwNodeItem* node = m_view->restoreNode_(m_snap);
            if (node) {
                m_view->selectNode_(node);
            }
        }

    private:
        SwNodeEditorView* m_view{nullptr};
        NodeSnapshot m_snap;
    };

    class DeleteNodeCommand final : public SwUndoCommand {
    public:
        DeleteNodeCommand(SwNodeEditorView* view, NodeSnapshot snap, std::vector<ConnectionSnapshot> conns)
            : SwUndoCommand("Delete Node")
            , m_view(view)
            , m_snap(std::move(snap))
            , m_connections(std::move(conns)) {}

        void undo() override {
            if (!m_view) {
                return;
            }
            SwNodeItem* node = m_view->restoreNode_(m_snap);
            for (const ConnectionSnapshot& c : m_connections) {
                (void)m_view->createConnection_(c);
            }
            if (node) {
                m_view->selectNode_(node);
            }
        }

        void redo() override {
            if (m_view) {
                (void)m_view->removeNodeById_(m_snap.id);
            }
        }

    private:
        SwNodeEditorView* m_view{nullptr};
        NodeSnapshot m_snap;
        std::vector<ConnectionSnapshot> m_connections;
    };

    class ConnectCommand final : public SwUndoCommand {
    public:
        explicit ConnectCommand(SwNodeEditorView* view, ConnectionSnapshot snap)
            : SwUndoCommand("Connect")
            , m_view(view)
            , m_snap(snap) {}

        void undo() override {
            if (m_view) {
                (void)m_view->removeConnection_(m_snap);
            }
        }

        void redo() override {
            if (m_view) {
                (void)m_view->createConnection_(m_snap);
            }
        }

    private:
        SwNodeEditorView* m_view{nullptr};
        ConnectionSnapshot m_snap;
    };

    class DeleteConnectionCommand final : public SwUndoCommand {
    public:
        explicit DeleteConnectionCommand(SwNodeEditorView* view, ConnectionSnapshot snap)
            : SwUndoCommand("Delete Connection")
            , m_view(view)
            , m_snap(snap) {}

        void undo() override {
            if (m_view) {
                (void)m_view->createConnection_(m_snap);
            }
        }

        void redo() override {
            if (m_view) {
                (void)m_view->removeConnection_(m_snap);
            }
        }

    private:
        SwNodeEditorView* m_view{nullptr};
        ConnectionSnapshot m_snap;
    };

    class ReconnectCommand final : public SwUndoCommand {
    public:
        ReconnectCommand(SwNodeEditorView* view, ConnectionSnapshot before, ConnectionSnapshot after)
            : SwUndoCommand("Reconnect")
            , m_view(view)
            , m_before(before)
            , m_after(after) {}

        void undo() override {
            if (!m_view) {
                return;
            }
            (void)m_view->removeConnection_(m_after);
            (void)m_view->createConnection_(m_before);
        }

        void redo() override {
            if (!m_view) {
                return;
            }
            (void)m_view->removeConnection_(m_before);
            (void)m_view->createConnection_(m_after);
        }

    private:
        SwNodeEditorView* m_view{nullptr};
        ConnectionSnapshot m_before;
        ConnectionSnapshot m_after;
    };

    class MoveNodeCommand final : public SwUndoCommand {
    public:
        MoveNodeCommand(SwNodeEditorView* view, int nodeId, const SwPointF& before, const SwPointF& after)
            : SwUndoCommand("Move Node")
            , m_view(view)
            , m_nodeId(nodeId)
            , m_before(before)
            , m_after(after) {}

        void undo() override {
            if (!m_view) {
                return;
            }
            if (SwNodeItem* node = m_view->nodeById_(m_nodeId)) {
                node->setPos(m_before);
                m_view->setModelNodePos_(m_nodeId, m_before);
                if (m_view->scene()) {
                    m_view->scene()->changed();
                }
            }
        }

        void redo() override {
            if (!m_view) {
                return;
            }
            if (SwNodeItem* node = m_view->nodeById_(m_nodeId)) {
                node->setPos(m_after);
                m_view->setModelNodePos_(m_nodeId, m_after);
                if (m_view->scene()) {
                    m_view->scene()->changed();
                }
            }
        }

    private:
        SwNodeEditorView* m_view{nullptr};
        int m_nodeId{0};
        SwPointF m_before{};
        SwPointF m_after{};
    };

    class MoveNodesCommand final : public SwUndoCommand {
    public:
        struct Entry {
            int nodeId{0};
            SwPointF before{};
            SwPointF after{};
        };

        MoveNodesCommand(SwNodeEditorView* view, std::vector<Entry> moved)
            : SwUndoCommand(moved.size() > 1 ? "Move Nodes" : "Move Node")
            , m_view(view)
            , m_moved(std::move(moved)) {}

        void undo() override { apply_(true); }
        void redo() override { apply_(false); }

    private:
        void apply_(bool before) {
            if (!m_view) {
                return;
            }
            for (const Entry& e : m_moved) {
                SwNodeItem* node = m_view->nodeById_(e.nodeId);
                if (!node) {
                    continue;
                }
                const SwPointF pos = before ? e.before : e.after;
                node->setPos(pos);
                m_view->setModelNodePos_(e.nodeId, pos);
            }
            if (m_view->scene()) {
                m_view->scene()->changed();
            }
        }

        SwNodeEditorView* m_view{nullptr};
        std::vector<Entry> m_moved;
    };

    void cancelPendingLink_() {
        if (!m_pendingLink.active) {
            return;
        }
        if (m_pendingLink.fromReconnect && m_pendingLink.before.outNodeId != 0) {
            m_undo.push(new DeleteConnectionCommand(this, m_pendingLink.before));
        }
        m_pendingLink = {};
    }

    bool tryConnectPendingLinkToNode_(int nodeId) {
        if (!m_pendingLink.active || nodeId <= 0) {
            return false;
        }
        SwNodeItem* outNode = nodeById_(m_pendingLink.outNodeId);
        SwNodeItem* inNode = nodeById_(nodeId);
        if (!outNode || !inNode) {
            return false;
        }
        if (m_pendingLink.outPort < 0 || m_pendingLink.outPort >= outNode->outputCount()) {
            return false;
        }

        if (!ensureModelNodeForUiNode_(outNode) || !ensureModelNodeForUiNode_(inNode)) {
            return false;
        }

        int bestInPort = -1;
        for (int i = 0; i < inNode->inputCount(); ++i) {
            SwizioNodes::ConnectionId cid;
            cid.outNodeId = static_cast<SwizioNodes::NodeId>(outNode->id());
            cid.outPortIndex = static_cast<SwizioNodes::PortIndex>(m_pendingLink.outPort);
            cid.inNodeId = static_cast<SwizioNodes::NodeId>(inNode->id());
            cid.inPortIndex = static_cast<SwizioNodes::PortIndex>(i);
            if (m_graph && m_graph->connectionPossible(cid)) {
                bestInPort = i;
                break;
            }
        }
        if (bestInPort < 0) {
            return false;
        }

        ConnectionSnapshot snap;
        snap.outNodeId = outNode->id();
        snap.outPort = m_pendingLink.outPort;
        snap.inNodeId = inNode->id();
        snap.inPort = bestInPort;

        if (m_pendingLink.fromReconnect) {
            const ConnectionSnapshot before = m_pendingLink.before;
            m_undo.push(new ReconnectCommand(this, before, snap));
        } else {
            m_undo.push(new ConnectCommand(this, snap));
        }
        return true;
    }

    double closestPortDistanceView_(const SwPointF& viewPos) const {
        double best = std::numeric_limits<double>::infinity();
        for (SwNodeItem* node : m_nodes) {
            if (!node) {
                continue;
            }
            for (int i = 0; i < node->inputCount(); ++i) {
                const SwPointF p = mapFromScene(node->inputPortScenePos(i));
                best = std::min(best, std::hypot(viewPos.x - p.x, viewPos.y - p.y));
            }
            for (int i = 0; i < node->outputCount(); ++i) {
                const SwPointF p = mapFromScene(node->outputPortScenePos(i));
                best = std::min(best, std::hypot(viewPos.x - p.x, viewPos.y - p.y));
            }
        }
        return best;
    }

    bool shouldOpenConnectionDropMenu_(const SwPointF& scenePos, const SwPointF& viewPos) const {
        if (topNodeAt_(scenePos)) {
            return false;
        }
        const double threshold = 40.0;
        return closestPortDistanceView_(viewPos) >= threshold;
    }

    SwString clipboardText_() const {
        SwGuiApplication* app = SwGuiApplication::instance(false);
        auto* platform = app ? app->platformIntegration() : nullptr;
        return platform ? platform->clipboardText() : SwString();
    }

    void setClipboardText_(const SwString& text) const {
        SwGuiApplication* app = SwGuiApplication::instance(false);
        auto* platform = app ? app->platformIntegration() : nullptr;
        if (platform) {
            platform->setClipboardText(text);
        }
    }

    static SwString clipboardMagic_() { return SwString("SWNODE_NODE_V1"); }

    SwString serializeSelectedForClipboard_() const {
        if (!m_selectedNode) {
            return SwString();
        }
        const NodeSnapshot snap = snapshotNode_(m_selectedNode);
        SwString out = clipboardMagic_();
        out.append("\n");
        out.append(snap.typeKey);
        out.append("\n");
        out.append(snap.title);
        out.append("\n");
        out.append(snap.hasInlineLineEdit ? snap.inlineLineEditText : SwString());
        return out;
    }

    bool parseClipboardNode_(const SwString& text, SwString* outTypeKey, SwString* outTitle, SwString* outInlineText) const {
        if (!outTypeKey || !outTitle || !outInlineText) {
            return false;
        }
        const SwList<SwString> parts = text.split('\n');
        if (parts.size() < 3) {
            return false;
        }
        if (parts[0] != clipboardMagic_()) {
            return false;
        }
        *outTypeKey = parts[1];
        *outTitle = parts[2];
        *outInlineText = (parts.size() >= 4) ? parts[3] : SwString();
        return !outTypeKey->isEmpty();
    }

    bool hasTextInputFocus_() const {
        for (SwObject* objChild : children()) {
            auto* w = dynamic_cast<SwWidget*>(objChild);
            if (!w) {
                continue;
            }
            if (!w->getFocus()) {
                continue;
            }
            if (dynamic_cast<SwLineEdit*>(w)) {
                return true;
            }
        }
        return false;
    }

    void undo_() { m_undo.undo(); }
    void redo_() { m_undo.redo(); }

    int addNodeUndoable_(const SwString& typeKey,
                         const SwPointF& scenePos,
                         const SwString& titleOverride = SwString(),
                         const SwString& inlineTextOverride = SwString()) {
        NodeSnapshot snap = makeNewNodeSnapshot_(typeKey, scenePos, titleOverride, inlineTextOverride);
        const int id = snap.id;
        m_undo.push(new AddNodeCommand(this, std::move(snap)));
        return id;
    }

    void copySelected_() {
        const SwString payload = serializeSelectedForClipboard_();
        if (payload.isEmpty()) {
            return;
        }
        setClipboardText_(payload);
        m_pasteSerial = 0;
    }

    void deleteSelected_() {
        if (m_selectedConnection) {
            const ConnectionSnapshot snap = snapshotConnection_(m_selectedConnection);
            if (snap.outNodeId != 0) {
                m_undo.push(new DeleteConnectionCommand(this, snap));
            }
            return;
        }
        if (m_selectedNode) {
            const NodeSnapshot snap = snapshotNode_(m_selectedNode);
            const std::vector<ConnectionSnapshot> conns = snapshotConnectionsForNode_(m_selectedNode);
            m_undo.push(new DeleteNodeCommand(this, snap, conns));
        }
    }

    void cutSelected_() {
        copySelected_();
        deleteSelected_();
    }

    void pasteAt_(const SwPointF& scenePos) {
        SwString typeKey;
        SwString title;
        SwString inlineText;
        if (!parseClipboardNode_(clipboardText_(), &typeKey, &title, &inlineText)) {
            return;
        }

        const int next = std::min(12, m_pasteSerial + 1);
        const int dx = 20 * next;
        const int dy = 20 * next;
        m_pasteSerial = (next >= 12) ? 0 : next;

        const SwPointF pos(scenePos.x + static_cast<double>(dx), scenePos.y + static_cast<double>(dy));
        NodeSnapshot snap = makeNewNodeSnapshot_(typeKey, pos, title, inlineText);
        m_undo.push(new AddNodeCommand(this, std::move(snap)));
    }

    void paste_() {
        pasteAt_(mapToScene(m_lastMouseView));
    }

    void clearHover_(bool updateCursor = true) {
        if (m_hoverPortNode) {
            m_hoverPortNode->clearHoveredPort();
        }
        m_hoverPortNode = nullptr;
        m_hoverPort = SwNodeItem::PortHit{};

        if (m_hoverConnection) {
            m_hoverConnection->setHovered(false);
        }
        m_hoverConnection = nullptr;

        if (updateCursor) {
            setCursor(CursorType::Arrow);
        }
    }

    static double connectionDistanceView_(const SwPointF& viewPos, const SwPointF& a, const SwPointF& b, double s) {
        const double dx = std::max(80.0 * s, std::abs(b.x - a.x) * 0.5);
        const SwPointF c1(a.x + dx, a.y);
        const SwPointF c2(b.x - dx, b.y);

        const double dist = std::hypot(b.x - a.x, b.y - a.y);
        const int segments = clampInt(static_cast<int>(std::lround(dist / 10.0)), 24, 160);

        double best = std::numeric_limits<double>::infinity();
        SwPointF last = a;
        for (int i = 1; i <= segments; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(segments);
            const SwPointF p = cubicBezier(a, c1, c2, b, t);
            best = std::min(best, pointSegmentDistance(viewPos, last, p));
            last = p;
        }
        return best;
    }

    SwConnectionItem* connectionForInput_(SwNodeItem* node, int inPort) const {
        if (!node) {
            return nullptr;
        }
        for (SwConnectionItem* c : m_connections) {
            if (c && c->inNode() == node && c->inPort() == inPort) {
                return c;
            }
        }
        return nullptr;
    }

    SwConnectionItem* closestConnectionForInput_(SwNodeItem* node, int inPort, const SwPointF& viewPos) const {
        if (!node) {
            return nullptr;
        }
        const double s = std::max(0.0001, scale());
        SwConnectionItem* best = nullptr;
        double bestDist = std::numeric_limits<double>::infinity();
        for (SwConnectionItem* c : m_connections) {
            if (!c || c->inNode() != node || c->inPort() != inPort || !c->outNode()) {
                continue;
            }
            const SwPointF a = mapFromScene(c->outNode()->outputPortScenePos(c->outPort()));
            const SwPointF b = mapFromScene(c->inNode()->inputPortScenePos(c->inPort()));
            const double d = connectionDistanceView_(viewPos, a, b, s);
            if (d < bestDist) {
                bestDist = d;
                best = c;
            }
        }
        return best;
    }

    SwConnectionItem* closestConnectionForOutput_(SwNodeItem* node, int outPort, const SwPointF& viewPos) const {
        if (!node) {
            return nullptr;
        }
        const double s = std::max(0.0001, scale());
        SwConnectionItem* best = nullptr;
        double bestDist = std::numeric_limits<double>::infinity();
        for (SwConnectionItem* c : m_connections) {
            if (!c || c->outNode() != node || c->outPort() != outPort || !c->inNode()) {
                continue;
            }
            const SwPointF a = mapFromScene(c->outNode()->outputPortScenePos(c->outPort()));
            const SwPointF b = mapFromScene(c->inNode()->inputPortScenePos(c->inPort()));
            const double d = connectionDistanceView_(viewPos, a, b, s);
            if (d < bestDist) {
                bestDist = d;
                best = c;
            }
        }
        return best;
    }

    SwConnectionItem* connectionUnderCursor_(const SwPointF& viewPos) const {
        const double s = std::max(0.0001, scale());
        const int w = std::max(1, roundToInt(4.0 * s));
        const double threshold = std::max(10.0, static_cast<double>(w) * 0.5 + 6.0);

        SwConnectionItem* best = nullptr;
        double bestDist = threshold;
        for (SwConnectionItem* c : m_connections) {
            if (!c || !c->outNode() || !c->inNode()) {
                continue;
            }
            const SwPointF a = mapFromScene(c->outNode()->outputPortScenePos(c->outPort()));
            const SwPointF b = mapFromScene(c->inNode()->inputPortScenePos(c->inPort()));
            const double d = connectionDistanceView_(viewPos, a, b, s);
            if (d <= bestDist) {
                bestDist = d;
                best = c;
            }
        }
        return best;
    }

    void setHoverPort_(SwNodeItem* node, const SwNodeItem::PortHit& port) {
        const bool same =
            (m_hoverPortNode == node) && (m_hoverPort.hit == port.hit) &&
            (!port.hit || (m_hoverPort.type == port.type && m_hoverPort.index == port.index));
        if (same) {
            return;
        }

        if (m_hoverPortNode) {
            m_hoverPortNode->clearHoveredPort();
        }
        m_hoverPortNode = node;
        m_hoverPort = port;
        if (m_hoverPortNode) {
            m_hoverPortNode->setHoveredPort(port);
        }
    }

    void setHoverConnection_(SwConnectionItem* conn) {
        if (m_hoverConnection == conn) {
            return;
        }
        if (m_hoverConnection) {
            m_hoverConnection->setHovered(false);
        }
        m_hoverConnection = conn;
        if (m_hoverConnection) {
            m_hoverConnection->setHovered(true);
        }
    }

    void updateHover_(const SwPointF& scenePos, const SwPointF& viewPos, bool includeConnections) {
        // Avoid fighting with embedded widgets (line edits, etc.).
        SwWidget* child = getChildUnderCursor(static_cast<int>(std::lround(viewPos.x)), static_cast<int>(std::lround(viewPos.y)));
        if (child && child != this) {
            clearHover_(false);
            return;
        }

        SwNodeItem* hitNode = topNodeAt_(scenePos);
        SwNodeItem::PortHit port{};
        if (hitNode) {
            port = hitNode->hitTestPort(scenePos);
        }
        setHoverPort_(hitNode, port);

        SwConnectionItem* hoveredConn = nullptr;
        if (includeConnections) {
            if (hitNode && port.hit) {
                hoveredConn = (port.type == SwNodeItem::InPort) ? closestConnectionForInput_(hitNode, port.index, viewPos)
                                                                : closestConnectionForOutput_(hitNode, port.index, viewPos);
            }
            if (!hoveredConn) {
                hoveredConn = connectionUnderCursor_(viewPos);
            }
        }
        setHoverConnection_(hoveredConn);

        setCursor((port.hit || hoveredConn) ? CursorType::Hand : CursorType::Arrow);
    }

    bool beginReconnection_(SwConnectionItem* conn, const SwPointF& scenePos) {
        if (!conn || !conn->outNode() || !conn->inNode()) {
            return false;
        }

        if (m_reconnectActive) {
            (void)createConnection_(m_reconnectOld);
            m_reconnectActive = false;
        }

        ConnectionSnapshot old;
        old.outNodeId = conn->outNode()->id();
        old.outPort = conn->outPort();
        old.inNodeId = conn->inNode()->id();
        old.inPort = conn->inPort();

        clearHover_();
        m_reconnectActive = true;
        m_reconnectOld = old;

        if (!removeConnection_(old)) {
            m_reconnectActive = false;
            return false;
        }

        SwNodeItem* outNode = nodeById_(old.outNodeId);
        if (!outNode) {
            m_reconnectActive = false;
            return false;
        }

        beginConnection_(outNode, old.outPort, scenePos);
        return true;
    }

    SwNodeItem* topNodeAt_(const SwPointF& scenePos) const {
        SwNodeItem* best = nullptr;
        double bestZ = -std::numeric_limits<double>::infinity();
        for (SwNodeItem* node : m_nodes) {
            if (!node || !node->isVisible()) {
                continue;
            }
            if (!node->containsScenePoint(scenePos)) {
                continue;
            }
            if (node->zValue() >= bestZ) {
                best = node;
                bestZ = node->zValue();
            }
        }
        return best;
    }

    static bool rectsIntersectF_(const SwRectF& a, const SwRectF& b) {
        const SwRectF ar = a.normalized();
        const SwRectF br = b.normalized();
        if (ar.isEmpty() || br.isEmpty()) {
            return false;
        }
        return !(ar.right() < br.left() || ar.left() > br.right() || ar.bottom() < br.top() || ar.top() > br.bottom());
    }

    void ensurePrimarySelectedNode_() {
        if (m_selectedNode && m_selectedNode->isSelected()) {
            return;
        }
        m_selectedNode = nullptr;
        for (SwNodeItem* n : m_nodes) {
            if (n && n->isSelected()) {
                m_selectedNode = n;
                break;
            }
        }
    }

    void selectNode_(SwNodeItem* node) {
        if (m_selectedConnection) {
            m_selectedConnection->setSelected(false);
            m_selectedConnection = nullptr;
        }
        for (SwNodeItem* n : m_nodes) {
            if (!n) {
                continue;
            }
            n->setSelected(n == node);
        }
        m_selectedNode = node;
        if (scene()) {
            scene()->changed();
        }
    }

    void ensureNodeSelected_(SwNodeItem* node) {
        if (!node) {
            return;
        }
        if (m_selectedConnection) {
            m_selectedConnection->setSelected(false);
            m_selectedConnection = nullptr;
        }
        if (!node->isSelected()) {
            node->setSelected(true);
        }
        m_selectedNode = node;
        if (scene()) {
            scene()->changed();
        }
    }

    void toggleNodeSelection_(SwNodeItem* node) {
        if (!node) {
            return;
        }
        if (m_selectedConnection) {
            m_selectedConnection->setSelected(false);
            m_selectedConnection = nullptr;
        }
        node->setSelected(!node->isSelected());
        if (node->isSelected()) {
            m_selectedNode = node;
        } else {
            ensurePrimarySelectedNode_();
        }
        if (scene()) {
            scene()->changed();
        }
    }

    void beginRubberBand_(const SwPointF& scenePos, bool additive) {
        if (!scene()) {
            return;
        }

        clearHover_();

        if (m_selectedConnection) {
            m_selectedConnection->setSelected(false);
            m_selectedConnection = nullptr;
        }

        m_rubberBandBaseSelectedIds.clear();
        m_rubberBandAdditive = additive;
        if (m_rubberBandAdditive) {
            for (SwNodeItem* n : m_nodes) {
                if (n && n->isSelected()) {
                    m_rubberBandBaseSelectedIds.insert(n->id());
                }
            }
        } else {
            for (SwNodeItem* n : m_nodes) {
                if (n && n->isSelected()) {
                    n->setSelected(false);
                }
            }
            m_selectedNode = nullptr;
        }

        if (m_rubberBandRect) {
            scene()->removeItem(m_rubberBandRect);
            m_rubberBandRect = nullptr;
        }

        m_rubberBandRect = new SwGraphicsRectItem();
        m_rubberBandRect->setZValue(1000.0);
        SwPen pen;
        pen.setColor(SwColor{59, 130, 246});
        pen.setWidth(2);
        m_rubberBandRect->setPen(pen);
        scene()->addItem(m_rubberBandRect);

        m_rubberBandActive = true;
        m_rubberBandStartScene = scenePos;
        updateRubberBand_(scenePos);
    }

    void updateRubberBand_(const SwPointF& scenePos) {
        if (!m_rubberBandActive) {
            return;
        }

        const SwRectF selectionRect(std::min(m_rubberBandStartScene.x, scenePos.x),
                                    std::min(m_rubberBandStartScene.y, scenePos.y),
                                    std::abs(scenePos.x - m_rubberBandStartScene.x),
                                    std::abs(scenePos.y - m_rubberBandStartScene.y));

        if (m_rubberBandRect) {
            m_rubberBandRect->setRect(selectionRect);
        }

        for (SwNodeItem* n : m_nodes) {
            if (!n) {
                continue;
            }
            const bool inBase = m_rubberBandAdditive && (m_rubberBandBaseSelectedIds.find(n->id()) != m_rubberBandBaseSelectedIds.end());
            const bool inRect = rectsIntersectF_(n->sceneBoundingRect(), selectionRect);
            n->setSelected(inBase || inRect);
        }

        ensurePrimarySelectedNode_();
    }

    void endRubberBand_() {
        if (!m_rubberBandActive) {
            return;
        }
        m_rubberBandActive = false;
        m_rubberBandAdditive = false;
        m_rubberBandBaseSelectedIds.clear();

        if (scene() && m_rubberBandRect) {
            scene()->removeItem(m_rubberBandRect);
            m_rubberBandRect = nullptr;
        }

        ensurePrimarySelectedNode_();
        if (scene()) {
            scene()->changed();
        }
    }

    void selectConnection_(SwConnectionItem* conn) {
        if (m_selectedConnection == conn) {
            return;
        }
        for (SwNodeItem* n : m_nodes) {
            if (n && n->isSelected()) {
                n->setSelected(false);
            }
        }
        m_selectedNode = nullptr;
        if (m_selectedConnection) {
            m_selectedConnection->setSelected(false);
        }
        m_selectedConnection = conn;
        if (m_selectedConnection) {
            m_selectedConnection->setSelected(true);
        }
        if (scene()) {
            scene()->changed();
        }
    }

    void selectNodeFromProxy_(int nodeId) {
        SwNodeItem* node = nodeById_(nodeId);
        if (!node) {
            return;
        }
        selectNode_(node);
    }

    void beginConnection_(SwNodeItem* outNode, int outPort, const SwPointF& scenePos) {
        if (!outNode) {
            return;
        }
        // Cancel previous preview if any.
        if (m_previewConnection) {
            scene()->removeItem(m_previewConnection);
            m_previewConnection = nullptr;
        }
        m_connectionOutNode = outNode;
        m_connectionOutPort = outPort;
        m_previewConnection = new SwConnectionItem(outNode, outPort);
        m_previewConnection->setSceneEndPoint(scenePos);
        scene()->addItem(m_previewConnection);
    }

    void finishConnection_(const SwPointF& scenePos) {
        SwConnectionItem* preview = m_previewConnection;
        m_previewConnection = nullptr;

        auto restoreReconnection = [&]() {
            if (m_reconnectActive) {
                (void)createConnection_(m_reconnectOld);
                m_reconnectActive = false;
            }
        };

        if (!preview) {
            restoreReconnection();
            return;
        }

        SwNodeItem* outNode = m_connectionOutNode;
        const int outPort = m_connectionOutPort;

        SwNodeItem* targetNode = topNodeAt_(scenePos);
        if (outNode && targetNode) {
            const SwNodeItem::PortHit port = targetNode->hitTestPort(scenePos);
            if (port.hit && port.type == SwNodeItem::InPort) {
                // "Single connection per input" â€“ if occupied, treat as an invalid drop.
                if (!targetNode->allowsMultipleConnections(SwNodeItem::InPort, port.index) && connectionForInput_(targetNode, port.index)) {
                    if (scene()) {
                        scene()->removeItem(preview);
                    }
                    restoreReconnection();
                    return;
                }

                const int outId = outNode->id();
                const int inId = targetNode->id();
                if (scene()) {
                    scene()->removeItem(preview);
                }

                if (!ensureModelNodeForUiNode_(outNode) || !ensureModelNodeForUiNode_(targetNode)) {
                    restoreReconnection();
                    return;
                }
                if (m_graph) {
                    SwizioNodes::ConnectionId cid;
                    cid.outNodeId = static_cast<SwizioNodes::NodeId>(outId);
                    cid.outPortIndex = static_cast<SwizioNodes::PortIndex>(outPort);
                    cid.inNodeId = static_cast<SwizioNodes::NodeId>(inId);
                    cid.inPortIndex = static_cast<SwizioNodes::PortIndex>(port.index);
                    if (!m_graph->connectionPossible(cid)) {
                        restoreReconnection();
                        return;
                    }
                }
                ConnectionSnapshot snap;
                snap.outNodeId = outId;
                snap.outPort = outPort;
                snap.inNodeId = inId;
                snap.inPort = port.index;
                if (m_reconnectActive) {
                    const ConnectionSnapshot before = m_reconnectOld;
                    m_reconnectActive = false;
                    if (before.outNodeId == snap.outNodeId && before.outPort == snap.outPort &&
                        before.inNodeId == snap.inNodeId && before.inPort == snap.inPort) {
                        (void)createConnection_(before);
                        return;
                    }
                    m_undo.push(new ReconnectCommand(this, before, snap));
                } else {
                    m_undo.push(new ConnectCommand(this, snap));
                }
                return;
            }
        }

        if (scene()) {
            scene()->removeItem(preview);
        }
        if (outNode && shouldOpenConnectionDropMenu_(scenePos, m_lastMouseView)) {
            m_pendingLink = {};
            m_pendingLink.active = true;
            m_pendingLink.outNodeId = outNode->id();
            m_pendingLink.outPort = outPort;
            if (m_reconnectActive) {
                m_pendingLink.fromReconnect = true;
                m_pendingLink.before = m_reconnectOld;
                m_reconnectActive = false;
            }
            showRegistryPopup_(roundToInt(m_lastMouseView.x), roundToInt(m_lastMouseView.y), scenePos);
            return;
        }
        restoreReconnection();
    }

    void deleteNode_(SwNodeItem* node) {
        if (!node) {
            return;
        }
        (void)removeNodeById_(node->id());
    }

    std::vector<SwNodeItem*> m_nodes;
    std::vector<SwConnectionItem*> m_connections;
    SwTimer* m_flowTimer{nullptr};
    std::unordered_map<long long, EvalValue> m_lastOutputs;

    std::shared_ptr<SwizioNodes::NodeDelegateModelRegistry> m_graphRegistry;
    std::unique_ptr<SwizioNodes::DataFlowGraphModel> m_graph;
    std::chrono::steady_clock::time_point m_flowArmedUntil{};

    std::vector<NodeType> m_nodeTypes;
    SwNodeRegistryOverlay* m_registryOverlay{nullptr};
    SwNodeRegistryPopup* m_registryPopup{nullptr};
    SwPointF m_registryScenePos{};
    SwUndoStack m_undo;
    int m_pasteSerial{0};

    SwNodeItem* m_selectedNode{nullptr};
    SwConnectionItem* m_selectedConnection{nullptr};

    struct PendingLink {
        bool active{false};
        int outNodeId{0};
        int outPort{0};
        bool fromReconnect{false};
        ConnectionSnapshot before{};
    };
    PendingLink m_pendingLink{};

    bool m_draggingNodes{false};
    SwPointF m_dragStartScene{};
    std::vector<SwNodeItem*> m_dragNodes;
    std::vector<SwPointF> m_dragNodesStartPos;

    bool m_rubberBandActive{false};
    bool m_rubberBandAdditive{false};
    SwPointF m_rubberBandStartScene{};
    SwGraphicsRectItem* m_rubberBandRect{nullptr};
    std::unordered_set<int> m_rubberBandBaseSelectedIds;

    bool m_panning{false};
    SwPointF m_panStartView{};
    SwPointF m_panStartScroll{};
    SwPointF m_scroll{0.0, 0.0};

    SwPointF m_lastMouseView{};

    SwNodeItem* m_hoverPortNode{nullptr};
    SwNodeItem::PortHit m_hoverPort{};
    SwConnectionItem* m_hoverConnection{nullptr};

    bool m_reconnectActive{false};
    ConnectionSnapshot m_reconnectOld{};

    SwNodeItem* m_connectionOutNode{nullptr};
    int m_connectionOutPort{0};
    SwConnectionItem* m_previewConnection{nullptr};

    int m_nextNodeId{0};
};

} // namespace swnodeeditor

