#include "SwCreatorFormCanvas.h"

#include "SwLayout.h"
#include "SwUiLoader.h"

#include "SwMenu.h"

#include "theme/SwCreatorTheme.h"

#include "SwGroupBox.h"
#include "SwLineEdit.h"
#include "SwListWidget.h"
#include "SwScrollArea.h"
#include "SwSpacer.h"
#include "SwSplitter.h"
#include "SwStackedWidget.h"
#include "SwTabWidget.h"
#include "SwWidgetPlatformAdapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <set>

namespace {
std::string toLowerFirst(std::string s) {
    if (!s.empty()) {
        s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    }
    return s;
}

int clampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

SwColor blendColor_(const SwColor& from, const SwColor& to, float amount) {
    const float t = (std::max)(0.0f, (std::min)(1.0f, amount));
    auto blendChannel = [t](int a, int b) -> int {
        return static_cast<int>(std::lround(a + (b - a) * t));
    };
    return SwColor{
        blendChannel(from.r, to.r),
        blendChannel(from.g, to.g),
        blendChannel(from.b, to.b)
    };
}

SwRect intersectRects_(const SwRect& a, const SwRect& b) {
    const int left = std::max(a.x, b.x);
    const int top = std::max(a.y, b.y);
    const int right = std::min(a.x + a.width, b.x + b.width);
    const int bottom = std::min(a.y + a.height, b.y + b.height);
    if (right <= left || bottom <= top) {
        return {0, 0, 0, 0};
    }
    return {left, top, right - left, bottom - top};
}

SwRect insetRect_(const SwRect& rect, int inset) {
    const int safeInset = std::max(0, inset);
    const int width = std::max(0, rect.width - 2 * safeInset);
    const int height = std::max(0, rect.height - 2 * safeInset);
    if (width <= 0 || height <= 0) {
        return {0, 0, 0, 0};
    }
    return {rect.x + safeInset, rect.y + safeInset, width, height};
}

SwWidget* findRootWidget(SwObject* start) {
    SwWidget* lastWidget = nullptr;
    for (SwObject* p = start; p; p = p->parent()) {
        if (auto* w = dynamic_cast<SwWidget*>(p)) {
            lastWidget = w;
        }
    }
    return lastWidget;
}

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool containsCaseInsensitive(const SwString& haystack, const SwString& needle) {
    if (needle.isEmpty()) {
        return true;
    }
    const std::string h = toLowerAscii(haystack.toStdString());
    const std::string n = toLowerAscii(needle.toStdString());
    return h.find(n) != std::string::npos;
}

SwRect widgetRectInCanvas_(const SwWidget* canvas, const SwWidget* widget) {
    if (!canvas || !widget) {
        return SwRect{0, 0, 0, 0};
    }
    const SwPoint topLeft = widget->mapTo(canvas, SwPoint{0, 0});
    return SwRect{topLeft.x, topLeft.y, widget->width(), widget->height()};
}

SwRect visibleWidgetRectInCanvas_(const SwWidget* canvas, const SwWidget* widget) {
    if (!canvas || !widget) {
        return {0, 0, 0, 0};
    }

    SwRect visible = widgetRectInCanvas_(canvas, widget);
    for (const SwWidget* ancestor = dynamic_cast<const SwWidget*>(widget->parent());
         ancestor && ancestor != canvas;
         ancestor = dynamic_cast<const SwWidget*>(ancestor->parent())) {
        visible = intersectRects_(visible, widgetRectInCanvas_(canvas, ancestor));
        if (visible.width <= 0 || visible.height <= 0) {
            return {0, 0, 0, 0};
        }
    }

    return intersectRects_(visible, canvas->rect());
}

bool shouldDrawLayoutContainerOverlay_(const SwWidget* widget) {
    return widget
           && (widget->layout() != nullptr
               || dynamic_cast<const SwGroupBox*>(widget) != nullptr
               || dynamic_cast<const SwScrollArea*>(widget) != nullptr
               || dynamic_cast<const SwSplitter*>(widget) != nullptr
               || dynamic_cast<const SwStackedWidget*>(widget) != nullptr
               || dynamic_cast<const SwTabWidget*>(widget) != nullptr);
}

SwRect layoutContainerOverlayRect_(const SwWidget* canvas, const SwWidget* widget) {
    SwRect rect = visibleWidgetRectInCanvas_(canvas, widget);
    if (rect.width <= 0 || rect.height <= 0) {
        return rect;
    }

    if (widget && widget->layout()) {
        rect = insetRect_(rect, widget->layout()->margin());
    }

    return rect;
}

SwPoint canvasPointToParent_(const SwWidget* canvas, const SwWidget* parent, const SwPoint& canvasPoint) {
    if (!canvas || !parent) {
        return canvasPoint;
    }
    return parent->mapFrom(canvas, canvasPoint);
}

constexpr int kDefaultFormWidth = 960;
constexpr int kDefaultFormHeight = 720;
constexpr int kBaseMinimumFormWidth = 320;
constexpr int kBaseMinimumFormHeight = 240;
constexpr int kCanvasResizeGripSize = 36;
} // namespace


class SwCreatorFormCanvas::DesignOverlay final : public SwWidget {
    SW_OBJECT(DesignOverlay, SwWidget)

public:
    explicit DesignOverlay(SwCreatorFormCanvas* owner, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_owner(owner) {
        setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setFocusPolicy(FocusPolicyEnum::Strong);
        setCursor(CursorType::Arrow);
    }

protected:
    void paintEvent(PaintEvent* event) override {
        SW_UNUSED(event);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event || !m_owner) {
            return;
        }

        if (event->button() == SwMouseButton::Right) {
            SwWidget* hit = m_owner->getChildUnderCursor(event->x(), event->y(), this);
            SwWidget* w = m_owner->designWidgetFromHit_(hit);
            m_owner->setSelectedWidget(w);
            m_owner->selectionChanged(w);
            m_owner->showContextMenu_(event->x(), event->y());
            event->accept();
            return;
        }

        if (event->button() != SwMouseButton::Left) {
            SwWidget::mousePressEvent(event);
            return;
        }

        if (hitCanvasResizeGrip_(event->x(), event->y())) {
            m_pressed = true;
            m_dragging = false;
            m_resizing = false;
            m_resizingCanvas = true;
            m_resizeMask = ResizeNone;
            m_pressX = event->x();
            m_pressY = event->y();
            m_dragWidget = nullptr;
            m_canvasResizeStartSize = m_owner->formSize();
            m_owner->m_dropTarget = nullptr;
            setCursor(CursorType::SizeNWSE);
            event->accept();
            return;
        }

        const SwString createClass = m_owner->createClass();
        const SwRect canvasRect = m_owner->rect();
        const bool insideCanvas = event->x() >= canvasRect.x && event->x() <= (canvasRect.x + canvasRect.width) &&
                                  event->y() >= canvasRect.y && event->y() <= (canvasRect.y + canvasRect.height);
        if (!createClass.isEmpty() && insideCanvas) {
            if (createClass.startsWith("layout:")) {
                const SwString layoutName = createClass.mid(7);
                (void)m_owner->createLayoutContainerAt(layoutName, event->x(), event->y());
                m_owner->setCreateClass(SwString());
            } else {
                (void)m_owner->createWidgetAt(createClass, event->x(), event->y());
                m_owner->setCreateClass(SwString());
            }
            event->accept();
            return;
        }

        // If we already have a selection, allow resizing from its handles even if the cursor isn't
        // strictly inside the widget (handles can slightly extend outside).
        if (SwWidget* selected = m_owner->selectedWidget()) {
            if (selected != m_owner && !managedByKnownLayout_(selected)) {
                const int mask = hitTestResizeHandle_(selectionRectInOwner_(selected), event->x(), event->y(), kResizeHandleHitSlop);
                if (mask != ResizeNone) {
                    m_pressed = true;
                    m_dragging = false;
                    m_pressX = event->x();
                    m_pressY = event->y();
                    m_dragWidget = selected;
                    m_resizing = true;
                    m_resizeMask = mask;
                    m_resizeStartRect = selected->geometry();
                    m_owner->m_dropTarget = nullptr;
                    setCursor(cursorForResizeMask_(mask));
                    event->accept();
                    return;
                }
            }
        }

        SwWidget* hit = m_owner->getChildUnderCursor(event->x(), event->y(), this);
        SwWidget* w = m_owner->designWidgetFromHit_(hit);
        m_owner->setSelectedWidget(w);
        m_owner->selectionChanged(w);
        setCursor(cursorForSelectionAt_(event->x(), event->y()));

        m_pressed = true;
        m_dragging = false;
        m_pressX = event->x();
        m_pressY = event->y();
        m_dragWidget = nullptr;
        m_resizing = false;
        m_resizeMask = ResizeNone;
        m_owner->m_dropTarget = nullptr;

        if (w) {
            if (!managedByKnownLayout_(w)) {
                const SwRect r = selectionRectInOwner_(w);
                const int mask = hitTestResizeHandle_(r, m_pressX, m_pressY, kResizeHandleHitSlop);
                if (mask != ResizeNone) {
                    m_dragWidget = w;
                    m_resizing = true;
                    m_resizeMask = mask;
                    m_resizeStartRect = w->geometry();
                    setCursor(cursorForResizeMask_(mask));
                } else {
                    const SwPoint pressInParent = ownerToParentPos_(w, m_pressX, m_pressY);
                    const SwRect localRect = w->geometry();
                    m_dragWidget = w;
                    m_offsetX = pressInParent.x - localRect.x;
                    m_offsetY = pressInParent.y - localRect.y;
                    m_startX = localRect.x;
                    m_startY = localRect.y;
                    setCursor(CursorType::SizeAll);
                }
            }
        }

        event->accept();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!event || !m_owner) {
            return;
        }

        if (!m_pressed) {
            const CursorType desired = cursorForSelectionAt_(event->x(), event->y());
            SwWidget::mouseMoveEvent(event);
            SwWidgetPlatformAdapter::setCursor(desired);
            event->accept();
            return;
        }

        if (m_resizingCanvas) {
            SwWidget::mouseMoveEvent(event);

            const SwSize minSize = m_owner->minimumFormSize();
            const int newWidth = std::max(minSize.width, m_canvasResizeStartSize.width + (event->x() - m_pressX));
            const int newHeight = std::max(minSize.height, m_canvasResizeStartSize.height + (event->y() - m_pressY));

            m_dragging = true;
            setCursor(CursorType::SizeNWSE);
            m_owner->setFormSize(newWidth, newHeight);
            event->accept();
            return;
        }

        if (m_resizing) {
            setCursor(cursorForResizeMask_(m_resizeMask));
        }
        SwWidget::mouseMoveEvent(event);

        if (!m_dragWidget) {
            return;
        }

        const int dx = std::abs(event->x() - m_pressX);
        const int dy = std::abs(event->y() - m_pressY);
        const int threshold = 4;
        if (!m_dragging && (dx + dy) >= threshold) {
            m_dragging = true;
            if (!m_resizing) {
                setCursor(CursorType::SizeAll);
            }
        }

        if (!m_dragging) {
            return;
        }

        if (m_resizing) {
            SwRect r = m_resizeStartRect;
            int newX = r.x;
            int newY = r.y;
            int newW = r.width;
            int newH = r.height;

            const SwPoint pressInParent = ownerToParentPos_(m_dragWidget, m_pressX, m_pressY);
            const SwPoint currentInParent = ownerToParentPos_(m_dragWidget, event->x(), event->y());
            const int deltaX = currentInParent.x - pressInParent.x;
            const int deltaY = currentInParent.y - pressInParent.y;

            const int startRight = r.x + r.width;
            const int startBottom = r.y + r.height;

            if (m_resizeMask & ResizeLeft) {
                newX = r.x + deltaX;
                newW = r.width - deltaX;
            }
            if (m_resizeMask & ResizeRight) {
                newW = r.width + deltaX;
            }
            if (m_resizeMask & ResizeTop) {
                newY = r.y + deltaY;
                newH = r.height - deltaY;
            }
            if (m_resizeMask & ResizeBottom) {
                newH = r.height + deltaY;
            }

            constexpr int kMinSize = 16;
            if (m_resizeMask & ResizeLeft) {
                if (newW < kMinSize) {
                    newW = kMinSize;
                    newX = startRight - kMinSize;
                }
            } else {
                newW = std::max(kMinSize, newW);
            }
            if (m_resizeMask & ResizeTop) {
                if (newH < kMinSize) {
                    newH = kMinSize;
                    newY = startBottom - kMinSize;
                }
            } else {
                newH = std::max(kMinSize, newH);
            }

            SwWidget* parentWidget = dynamic_cast<SwWidget*>(m_dragWidget->parent());
            const SwRect bounds = parentWidget ? parentWidget->rect() : m_owner->rect();
            const int pad = 6;

            const int minX = bounds.x + pad;
            const int minY = bounds.y + pad;
            const int maxRight = bounds.x + std::max(pad, bounds.width - pad);
            const int maxBottom = bounds.y + std::max(pad, bounds.height - pad);

            if (m_resizeMask & ResizeLeft) {
                if (newX < minX) {
                    newX = minX;
                    newW = startRight - newX;
                }
            } else {
                newX = std::max(minX, newX);
            }

            if (m_resizeMask & ResizeTop) {
                if (newY < minY) {
                    newY = minY;
                    newH = startBottom - newY;
                }
            } else {
                newY = std::max(minY, newY);
            }

            if (newX + newW > maxRight) {
                if (m_resizeMask & ResizeRight) {
                    newW = maxRight - newX;
                } else if (m_resizeMask & ResizeLeft) {
                    newX = maxRight - newW;
                }
            }
            if (newY + newH > maxBottom) {
                if (m_resizeMask & ResizeBottom) {
                    newH = maxBottom - newY;
                } else if (m_resizeMask & ResizeTop) {
                    newY = maxBottom - newH;
                }
            }

            newW = std::max(kMinSize, newW);
            newH = std::max(kMinSize, newH);

            m_dragWidget->move(newX, newY);
            m_dragWidget->resize(newW, newH);
            event->accept();
            return;
        }

        SwWidget* parentWidget = dynamic_cast<SwWidget*>(m_dragWidget->parent());
        const SwRect bounds = parentWidget ? parentWidget->rect() : m_owner->rect();
        const SwPoint currentInParent = ownerToParentPos_(m_dragWidget, event->x(), event->y());
        int newX = currentInParent.x - m_offsetX;
        int newY = currentInParent.y - m_offsetY;
        const int pad = 6;

        newX = clampInt(newX,
                        bounds.x + pad,
                        bounds.x + std::max(pad, bounds.width - m_dragWidget->width() - pad));
        newY = clampInt(newY,
                        bounds.y + pad,
                        bounds.y + std::max(pad, bounds.height - m_dragWidget->height() - pad));

        m_dragWidget->move(newX, newY);

        SwWidget* container = m_owner->findContainerAt_(event->x(), event->y(), m_dragWidget);
        if (container == m_owner->m_dropTarget) {
            event->accept();
            return;
        }
        m_owner->m_dropTarget = container;
        m_owner->update();
        event->accept();
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event || !m_owner) {
            return;
        }

        const bool wasDragging = m_dragging;
        const bool wasResizing = m_resizing;
        const bool wasResizingCanvas = m_resizingCanvas;
        SwWidget* dragged = m_dragWidget;
        SwWidget* dropTarget = m_owner->m_dropTarget;

        m_pressed = false;
        m_dragging = false;
        m_resizing = false;
        m_resizingCanvas = false;
        m_resizeMask = ResizeNone;
        m_dragWidget = nullptr;
        m_owner->m_dropTarget = nullptr;
        m_owner->update();
        setCursor(cursorForSelectionAt_(event->x(), event->y()));

        if (wasResizingCanvas) {
            event->accept();
            return;
        }

        if (!wasDragging || !dragged) {
            event->accept();
            return;
        }

        if (wasResizing) {
            m_owner->selectionChanged(dragged);
            event->accept();
            return;
        }

        SwWidget* oldParent = dynamic_cast<SwWidget*>(dragged->parent());
        SwWidget* effectiveDropTarget = dropTarget ? dropTarget : m_owner;
        const SwRect ownerRect = widgetRectInCanvas_(m_owner, dragged);

        auto detachFromLayout = [](SwWidget* parent, SwWidget* w) {
            if (!parent || !w) {
                return;
            }
            if (auto* l = parent->layout()) {
                if (auto* box = dynamic_cast<SwBoxLayout*>(l)) {
                    box->removeWidget(w);
                } else if (auto* grid = dynamic_cast<SwGridLayout*>(l)) {
                    grid->removeWidget(w);
                } else if (auto* form = dynamic_cast<SwFormLayout*>(l)) {
                    form->removeWidget(w);
                }
            }
        };

        auto attachToLayout = [](SwWidget* parent, SwWidget* w) {
            if (!parent || !w) {
                return;
            }
            if (auto* l = parent->layout()) {
                if (auto* box = dynamic_cast<SwBoxLayout*>(l)) {
                    box->addWidget(w);
                } else if (auto* form = dynamic_cast<SwFormLayout*>(l)) {
                    form->addWidget(w);
                } else if (auto* grid = dynamic_cast<SwGridLayout*>(l)) {
                    int count = 0;
                    for (SwObject* obj : parent->children()) {
                        auto* child = dynamic_cast<SwWidget*>(obj);
                        if (!child || child->parent() != parent || child == w) {
                            continue;
                        }
                        ++count;
                    }
                    const int idx = count;
                    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(idx + 1)))));
                    grid->addWidget(w, idx / cols, idx % cols);
                }
            }
        };

        if (auto* splitter = dynamic_cast<SwSplitter*>(effectiveDropTarget)) {
            if (oldParent != splitter) {
                detachFromLayout(oldParent, dragged);
                splitter->addWidget(dragged);
                attachToLayout(splitter, dragged);
                m_owner->designWidgetsChanged();
            }
            event->accept();
            return;
        }

        SwWidget* newParent = (effectiveDropTarget == m_owner) ? m_owner : m_owner->dropContentParent_(effectiveDropTarget);
        if (!newParent) {
            event->accept();
            return;
        }

        if (oldParent != newParent) {
            detachFromLayout(oldParent, dragged);
            dragged->setParent(newParent);
            attachToLayout(newParent, dragged);
            m_owner->designWidgetsChanged();
        }

        const SwRect bounds = newParent->rect();
        const SwPoint localTopLeft = canvasPointToParent_(m_owner, newParent, SwPoint{ownerRect.x, ownerRect.y});
        const int pad = 6;
        const int x = clampInt(localTopLeft.x,
                               bounds.x + pad,
                               bounds.x + std::max(pad, bounds.width - ownerRect.width - pad));
        const int y = clampInt(localTopLeft.y,
                               bounds.y + pad,
                               bounds.y + std::max(pad, bounds.height - ownerRect.height - pad));
        dragged->move(x, y);

        m_owner->selectionChanged(dragged);
        event->accept();
    }

private:
    enum ResizeMask {
        ResizeNone = 0,
        ResizeLeft = 1 << 0,
        ResizeTop = 1 << 1,
        ResizeRight = 1 << 2,
        ResizeBottom = 1 << 3,
    };

    static constexpr int kResizeHandleSize = 8;
    static constexpr int kResizeHandleHitSlop = 3;

    static SwRect expandRect_(SwRect rect, int amount) {
        rect.x -= amount;
        rect.y -= amount;
        rect.width += amount * 2;
        rect.height += amount * 2;
        return rect;
    }

    static bool pointInRectInclusive_(const SwRect& rect, int x, int y) {
        return x >= rect.x &&
               x <= (rect.x + rect.width) &&
               y >= rect.y &&
               y <= (rect.y + rect.height);
    }

    static int hitTestResizeHandle_(const SwRect& rect, int x, int y, int slop) {
        const int half = kResizeHandleSize / 2;
        const int left = rect.x;
        const int top = rect.y;
        const int right = rect.x + rect.width;
        const int bottom = rect.y + rect.height;
        const int midX = (left + right) / 2;
        const int midY = (top + bottom) / 2;

        auto handleRect = [&](int cx, int cy) {
            return expandRect_(SwRect{cx - half, cy - half, kResizeHandleSize, kResizeHandleSize}, slop);
        };

        if (pointInRectInclusive_(handleRect(left, top), x, y)) return ResizeLeft | ResizeTop;
        if (pointInRectInclusive_(handleRect(right, top), x, y)) return ResizeRight | ResizeTop;
        if (pointInRectInclusive_(handleRect(left, bottom), x, y)) return ResizeLeft | ResizeBottom;
        if (pointInRectInclusive_(handleRect(right, bottom), x, y)) return ResizeRight | ResizeBottom;
        if (pointInRectInclusive_(handleRect(midX, top), x, y)) return ResizeTop;
        if (pointInRectInclusive_(handleRect(left, midY), x, y)) return ResizeLeft;
        if (pointInRectInclusive_(handleRect(right, midY), x, y)) return ResizeRight;
        if (pointInRectInclusive_(handleRect(midX, bottom), x, y)) return ResizeBottom;
        return ResizeNone;
    }

    static CursorType cursorForResizeMask_(int mask) {
        const bool left = (mask & ResizeLeft) != 0;
        const bool right = (mask & ResizeRight) != 0;
        const bool top = (mask & ResizeTop) != 0;
        const bool bottom = (mask & ResizeBottom) != 0;

        if ((left && top) || (right && bottom)) {
            return CursorType::SizeNWSE;
        }
        if ((right && top) || (left && bottom)) {
            return CursorType::SizeNESW;
        }
        if (left || right) {
            return CursorType::SizeWE;
        }
        if (top || bottom) {
            return CursorType::SizeNS;
        }
        return CursorType::Arrow;
    }

    static bool managedByKnownLayout_(const SwWidget* widget) {
        const SwWidget* parentWidget = widget ? dynamic_cast<const SwWidget*>(widget->parent()) : nullptr;
        SwAbstractLayout* activeLayout = parentWidget ? parentWidget->layout() : nullptr;
        return activeLayout &&
               (dynamic_cast<SwBoxLayout*>(activeLayout) ||
                dynamic_cast<SwGridLayout*>(activeLayout) ||
                dynamic_cast<SwFormLayout*>(activeLayout));
    }

    SwRect widgetRectInOwner_(const SwWidget* widget) const {
        return widgetRectInCanvas_(m_owner, widget);
    }

    SwRect selectionRectInOwner_(const SwWidget* widget) const {
        return expandRect_(widgetRectInOwner_(widget), 2);
    }

    SwPoint ownerToParentPos_(const SwWidget* widget, int ownerX, int ownerY) const {
        if (!widget || !m_owner) {
            return SwPoint{ownerX, ownerY};
        }
        const SwWidget* parentWidget = dynamic_cast<const SwWidget*>(widget->parent());
        if (!parentWidget) {
            return SwPoint{ownerX, ownerY};
        }
        return parentWidget->mapFrom(m_owner, SwPoint{ownerX, ownerY});
    }

    bool hitCanvasResizeGrip_(int x, int y) const {
        if (!m_owner) {
            return false;
        }

        const SwRect canvasRect = m_owner->rect();
        const int left = std::max(canvasRect.x, canvasRect.x + canvasRect.width - kCanvasResizeGripSize);
        const int top = std::max(canvasRect.y, canvasRect.y + canvasRect.height - kCanvasResizeGripSize);
        return x >= left && x <= (canvasRect.x + canvasRect.width) &&
               y >= top && y <= (canvasRect.y + canvasRect.height);
    }

    CursorType cursorForSelectionAt_(int x, int y) const {
        if (!m_owner) {
            return CursorType::Arrow;
        }

        if (hitCanvasResizeGrip_(x, y)) {
            return CursorType::SizeNWSE;
        }

        SwWidget* selected = m_owner->selectedWidget();
        if (!selected || managedByKnownLayout_(selected)) {
            return CursorType::Arrow;
        }

        if (selected == m_owner) {
            return CursorType::Arrow;
        }

        const SwRect r = selectionRectInOwner_(selected);
        const int resizeMask = hitTestResizeHandle_(r, x, y, kResizeHandleHitSlop);
        if (resizeMask != ResizeNone) {
            return cursorForResizeMask_(resizeMask);
        }

        const SwRect body = widgetRectInOwner_(selected);
        if (x >= body.x && x <= (body.x + body.width) &&
            y >= body.y && y <= (body.y + body.height)) {
            return CursorType::SizeAll;
        }

        return CursorType::Arrow;
    }

    SwCreatorFormCanvas* m_owner{nullptr};
    bool m_pressed{false};
    bool m_dragging{false};
    bool m_resizing{false};
    bool m_resizingCanvas{false};
    int m_resizeMask{ResizeNone};
    SwRect m_resizeStartRect;
    SwSize m_canvasResizeStartSize;
    int m_pressX{0};
    int m_pressY{0};
    int m_offsetX{0};
    int m_offsetY{0};
    int m_startX{0};
    int m_startY{0};
    SwWidget* m_dragWidget{nullptr};
};

class SwCreatorFormCanvas::RegistryPopup final : public SwFrame {
    SW_OBJECT(RegistryPopup, SwFrame)

public:
    enum class ItemKind {
        Command,
        Widget,
        Layout,
        BreakLayout,
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
        ItemKind kind{ItemKind::Widget};
        CommandId command{CommandId::Undo};
        SwString display;
        SwString payload; // widget class or layout name
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

    explicit RegistryPopup(SwCreatorFormCanvas* owner, SwWidget* parent = nullptr)
        : SwFrame(parent)
        , m_owner(owner) {
        setFrameShape(SwFrame::Shape::StyledPanel);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setStyleSheet(R"(
            RegistryPopup {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 14px;
            }
        )");

        m_search = new SearchLineEdit("Search widgets/actions...", this);
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
        SwObject::connect(m_list, &SwListView::activated, this, [this](const SwModelIndex& idx) { activateIndex_(idx); });

        m_search->setEscapeHandler([this]() { closeRequested(); });
        m_search->setReturnHandler([this]() { activateCurrent_(); });
        m_search->setDownHandler([this]() {
            if (m_list) {
                m_list->setFocus(true);
            }
        });
    }

    void setContextPoint(int globalX, int globalY) {
        m_contextX = globalX;
        m_contextY = globalY;
    }

    void setItems(std::vector<Item> items) {
        m_items = std::move(items);
        applyFilter_(m_search ? m_search->getText() : SwString());
    }

    void focusSearch() {
        if (!m_search) {
            return;
        }
        m_search->setFocus(true);
    }

    void clearSearch() {
        if (!m_search) {
            return;
        }
        m_search->setText(SwString());
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
        const SwRect r = rect();
        const int pad = 10;
        const int searchH = 34;
        const int gap = 8;

        if (m_search) {
            m_search->move(pad, pad);
            m_search->resize(std::max(0, r.width - 2 * pad), searchH);
        }

        if (m_list) {
            const int y = pad + searchH + gap;
            m_list->move(pad, y);
            m_list->resize(std::max(0, r.width - 2 * pad), std::max(0, r.height - y - pad));
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
        if (!m_owner) {
            closeRequested();
            return;
        }

        switch (it.kind) {
        case ItemKind::Command:
            switch (it.command) {
            case CommandId::Undo: m_owner->requestUndo(); break;
            case CommandId::Redo: m_owner->requestRedo(); break;
            case CommandId::Cut: m_owner->requestCut(); break;
            case CommandId::Copy: m_owner->requestCopy(); break;
            case CommandId::Paste: m_owner->requestPaste(); break;
            case CommandId::Delete: m_owner->requestDelete(); break;
            }
            closeRequested();
            return;

        case ItemKind::Widget:
            (void)m_owner->createWidgetAt(it.payload, m_contextX, m_contextY);
            closeRequested();
            return;

        case ItemKind::Layout:
            m_owner->applyLayout_(it.payload);
            closeRequested();
            return;

        case ItemKind::BreakLayout:
            m_owner->breakLayout_();
            closeRequested();
            return;
        }

        closeRequested();
    }

    SwCreatorFormCanvas* m_owner{nullptr};
    SearchLineEdit* m_search{nullptr};
    SwListWidget* m_list{nullptr};
    std::vector<Item> m_items;
    std::vector<Item> m_filtered;
    int m_contextX{0};
    int m_contextY{0};
};

class SwCreatorFormCanvas::RegistryOverlay final : public SwWidget {
    SW_OBJECT(RegistryOverlay, SwWidget)

public:
    RegistryOverlay(SwCreatorFormCanvas* owner, SwWidget* root, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_owner(owner)
        , m_root(root) {
        setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setFocusPolicy(FocusPolicyEnum::Strong);
    }

    void setPopup(RegistryPopup* popup) { m_popup = popup; }

protected:
    void mousePressEvent(MouseEvent* event) override {
        if (!event || !m_owner) {
            return;
        }

        if (m_popup) {
            const SwRect r = m_popup->frameGeometry();
            const bool inside = event->x() >= r.x && event->x() <= (r.x + r.width) &&
                                event->y() >= r.y && event->y() <= (r.y + r.height);
            if (!inside) {
                m_owner->hideRegistryPopup_();
                if (m_root) {
                    const SwPoint rootPos = m_root->mapFrom(this, event->pos());
                    MouseEvent forwarded(EventType::MousePressEvent,
                                         rootPos.x,
                                         rootPos.y,
                                         event->button(),
                                         event->isCtrlPressed(),
                                         event->isShiftPressed(),
                                         event->isAltPressed());
                    forwarded.setGlobalPos(event->globalPos());
                    if (m_root->dispatchMouseEventFromRoot(forwarded)) {
                        event->accept();
                    }
                }
                event->accept();
                return;
            }
        }

        SwWidget::mousePressEvent(event);
    }

private:
    SwCreatorFormCanvas* m_owner{nullptr};
    SwWidget* m_root{nullptr};
    RegistryPopup* m_popup{nullptr};
};

SwSize SwCreatorFormCanvas::defaultFormSize() {
    return SwSize{kDefaultFormWidth, kDefaultFormHeight};
}

SwCreatorFormCanvas::SwCreatorFormCanvas(SwWidget* parent)
    : SwFrame(parent) {
    setFrameShape(SwFrame::Shape::StyledPanel);
    setStyleSheet(R"(
            SwCreatorFormCanvas {
                background-color: rgb(255, 255, 255);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 14px;
            }
        )");
    setMinimumSize(kBaseMinimumFormWidth, kBaseMinimumFormHeight);
    resize(kDefaultFormWidth, kDefaultFormHeight);

    SwObject::connect(this, &SwCreatorFormCanvas::FocusChanged, this, [this](bool focus) {
        if (focus) {
            setSelectedWidget(nullptr);
            selectionChanged(nullptr);
        }
    });

    m_designOverlay = new DesignOverlay(this, this);
    const SwRect r = rect();
    m_designOverlay->move(0, 0);
    m_designOverlay->resize(r.width, r.height);

    SwObject::connect(this, &SwWidget::resized, this, [this](int w, int h) {
        if (!m_designOverlay) {
            return;
        }
        m_designOverlay->move(0, 0);
        m_designOverlay->resize(w, h);
    });
    SwObject::connect(this, &SwWidget::moved, this, [this](int, int) {
        if (!m_designOverlay) {
            return;
        }
        const SwRect r = rect();
        m_designOverlay->move(0, 0);
        m_designOverlay->resize(r.width, r.height);
    });
}

void SwCreatorFormCanvas::setFormSize(int width, int height) {
    const SwSize minSize = minimumFormSize();
    resize(std::max(minSize.width, width), std::max(minSize.height, height));
}

SwSize SwCreatorFormCanvas::formSize() const {
    return SwSize{width(), height()};
}

SwSize SwCreatorFormCanvas::minimumFormSize() const {
    return computeMinimumFormSize_();
}

SwSize SwCreatorFormCanvas::computeMinimumFormSize_() const {
    int minWidth = kBaseMinimumFormWidth;
    int minHeight = kBaseMinimumFormHeight;
    const int pad = 12;

    for (SwWidget* widget : m_designWidgets) {
        if (!widget || widget->parent() != this) {
            continue;
        }

        const SwRect r = widget->geometry();
        minWidth = std::max(minWidth, r.x + r.width + pad);
        minHeight = std::max(minHeight, r.y + r.height + pad);
    }

    return SwSize{minWidth, minHeight};
}

void SwCreatorFormCanvas::registerDesignWidget(SwWidget* w) {
    registerDesignWidget_(w, true);
}

void SwCreatorFormCanvas::registerDesignWidgetNoLayout(SwWidget* w) {
    registerDesignWidget_(w, false);
}

void SwCreatorFormCanvas::registerDesignWidget_(SwWidget* w, bool attachToParentLayout) {
    if (!w) {
        return;
    }

    if (w->parent() == this && m_designOverlay) {
        // Keep the overlay above direct children of the canvas.
        m_designOverlay->setParent(this);
        const SwRect overlayRect = rect();
        m_designOverlay->move(0, 0);
        m_designOverlay->resize(overlayRect.width, overlayRect.height);
    }

    if (std::find(m_designWidgets.begin(), m_designWidgets.end(), w) != m_designWidgets.end()) {
        return;
    }

    if (attachToParentLayout) {
        // If a known layout is active on the widget's parent, also register the widget with it.
        if (auto* parentWidget = dynamic_cast<SwWidget*>(w->parent())) {
            auto* l = parentWidget->layout();
            if (auto* box = dynamic_cast<SwBoxLayout*>(l)) {
                box->addWidget(w);
            } else if (auto* grid = dynamic_cast<SwGridLayout*>(l)) {
                int count = 0;
                for (SwObject* obj : parentWidget->children()) {
                    auto* child = dynamic_cast<SwWidget*>(obj);
                    if (!child || child->parent() != parentWidget || child == w) {
                        continue;
                    }
                    ++count;
                }
                const int idx = count;
                const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(idx + 1)))));
                grid->addWidget(w, idx / cols, idx % cols);
            } else if (auto* form = dynamic_cast<SwFormLayout*>(l)) {
                form->addWidget(w);
            }
        }
    }

    m_designWidgets.push_back(w);
    designWidgetsChanged();

    SwObject::connect(w, &SwWidget::FocusChanged, this, [this, w](bool focus) {
        if (!focus) {
            return;
        }
        setSelectedWidget(w);
        selectionChanged(w);
    });
}

bool SwCreatorFormCanvas::removeDesignWidget(SwWidget* w) {
    if (!w) {
        return false;
    }

    auto it = std::find(m_designWidgets.begin(), m_designWidgets.end(), w);
    if (it == m_designWidgets.end()) {
        return false;
    }

    auto isDescendantOf = [](const SwObject* child, const SwObject* ancestor) -> bool {
        if (!child || !ancestor) {
            return false;
        }
        for (const SwObject* p = child; p; p = p->parent()) {
            if (p == ancestor) {
                return true;
            }
        }
        return false;
    };

    if (auto* parentWidget = dynamic_cast<SwWidget*>(w->parent())) {
        if (auto* l = parentWidget->layout()) {
            if (auto* box = dynamic_cast<SwBoxLayout*>(l)) {
                box->removeWidget(w);
            } else if (auto* grid = dynamic_cast<SwGridLayout*>(l)) {
                grid->removeWidget(w);
            } else if (auto* form = dynamic_cast<SwFormLayout*>(l)) {
                form->removeWidget(w);
            }
        }
    }

    if (m_selected == w || (m_selected && isDescendantOf(m_selected, w))) {
        setSelectedWidget(nullptr);
        selectionChanged(nullptr);
    }

    if (m_dropTarget == w || (m_dropTarget && isDescendantOf(m_dropTarget, w))) {
        m_dropTarget = nullptr;
    }

    m_designWidgets.erase(std::remove_if(m_designWidgets.begin(),
                                        m_designWidgets.end(),
                                        [&](SwWidget* candidate) { return candidate == w || isDescendantOf(candidate, w); }),
                          m_designWidgets.end());
    widgetRemoved(w);
    designWidgetsChanged();
    delete w;
    update();
    return true;
}

bool SwCreatorFormCanvas::reparentDesignWidget(SwWidget* w, SwWidget* container) {
    if (!w) {
        return false;
    }
    if (w == this) {
        return false;
    }

    auto it = std::find(m_designWidgets.begin(), m_designWidgets.end(), w);
    if (it == m_designWidgets.end()) {
        return false;
    }

    if (container == w) {
        return false;
    }

    if (container) {
        for (SwObject* p = container; p; p = p->parent()) {
            if (p == w) {
                return false;
            }
        }
    }

    SwWidget* oldParent = dynamic_cast<SwWidget*>(w->parent());
    SwWidget* effectiveDropTarget = container ? container : this;
    const SwRect ownerRect = widgetRectInCanvas_(this, w);

    auto detachFromLayout = [](SwWidget* parent, SwWidget* ww) {
        if (!parent || !ww) {
            return;
        }
        if (auto* l = parent->layout()) {
            if (auto* box = dynamic_cast<SwBoxLayout*>(l)) {
                box->removeWidget(ww);
            } else if (auto* grid = dynamic_cast<SwGridLayout*>(l)) {
                grid->removeWidget(ww);
            } else if (auto* form = dynamic_cast<SwFormLayout*>(l)) {
                form->removeWidget(ww);
            }
        }
    };

    auto attachToLayout = [](SwWidget* parent, SwWidget* ww) {
        if (!parent || !ww) {
            return;
        }
        if (auto* l = parent->layout()) {
            if (auto* box = dynamic_cast<SwBoxLayout*>(l)) {
                box->addWidget(ww);
            } else if (auto* form = dynamic_cast<SwFormLayout*>(l)) {
                form->addWidget(ww);
            } else if (auto* grid = dynamic_cast<SwGridLayout*>(l)) {
                int count = 0;
                for (SwObject* obj : parent->children()) {
                    auto* child = dynamic_cast<SwWidget*>(obj);
                    if (!child || child->parent() != parent || child == ww) {
                        continue;
                    }
                    ++count;
                }
                const int idx = count;
                const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(idx + 1)))));
                grid->addWidget(ww, idx / cols, idx % cols);
            }
        }
    };

    if (auto* splitter = dynamic_cast<SwSplitter*>(effectiveDropTarget)) {
        if (oldParent != splitter) {
            detachFromLayout(oldParent, w);
            splitter->addWidget(w);
            attachToLayout(splitter, w);
            designWidgetsChanged();
        }
        update();
        return true;
    }

    SwWidget* newParent = (effectiveDropTarget == this) ? this : dropContentParent_(effectiveDropTarget);
    if (!newParent) {
        return false;
    }

    if (oldParent != newParent) {
        detachFromLayout(oldParent, w);
        w->setParent(newParent);
        attachToLayout(newParent, w);
        designWidgetsChanged();
    }

    const SwRect bounds = newParent->rect();
    const SwPoint localTopLeft = canvasPointToParent_(this, newParent, SwPoint{ownerRect.x, ownerRect.y});
    const int pad = 6;
    const int nx = clampInt(localTopLeft.x,
                            bounds.x + pad,
                            bounds.x + std::max(pad, bounds.width - ownerRect.width - pad));
    const int ny = clampInt(localTopLeft.y,
                            bounds.y + pad,
                            bounds.y + std::max(pad, bounds.height - ownerRect.height - pad));
    w->move(nx, ny);

    update();
    return true;
}

SwWidget* SwCreatorFormCanvas::layoutTarget_() const {
    if (m_selected) {
        // If the selected widget has direct widget children, allow applying layout to it.
        bool hasDirectChild = false;
        for (SwObject* obj : m_selected->children()) {
            auto* w = dynamic_cast<SwWidget*>(obj);
            if (w && w->parent() == m_selected) {
                hasDirectChild = true;
                break;
            }
        }
        if (hasDirectChild) {
            return m_selected;
        }
    }
    return const_cast<SwCreatorFormCanvas*>(this);
}

void SwCreatorFormCanvas::ensureRegistryPopup_() {
    if (m_registryOverlay && m_registryPopup) {
        return;
    }

    SwWidget* root = findRootWidget(this);
    if (!root) {
        return;
    }

    if (!m_registryOverlay) {
        m_registryOverlay = new RegistryOverlay(this, root, root);
        m_registryOverlay->move(0, 0);
        m_registryOverlay->resize(root->width(), root->height());
    }

    if (!m_registryPopup) {
        m_registryPopup = new RegistryPopup(this, m_registryOverlay);
        m_registryPopup->resize(360, 420);
        updateRegistryPopupItems_();
        SwObject::connect(m_registryPopup, &RegistryPopup::closeRequested, this, [this]() { hideRegistryPopup_(); });
    }

    m_registryOverlay->setPopup(m_registryPopup);

    SwObject::connect(root, &SwWidget::resized, this, [this](int w, int h) {
        if (m_registryOverlay) {
            m_registryOverlay->move(0, 0);
            m_registryOverlay->resize(w, h);
        }
    });
}

void SwCreatorFormCanvas::showRegistryPopup_(int globalX, int globalY) {
    ensureRegistryPopup_();
    if (!m_registryOverlay || !m_registryPopup) {
        return;
    }

    updateRegistryPopupItems_();
    m_registryPopup->setContextPoint(globalX, globalY);
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

void SwCreatorFormCanvas::hideRegistryPopup_() {
    if (m_registryPopup) {
        m_registryPopup->hide();
    }
    if (m_registryOverlay) {
        m_registryOverlay->hide();
    }
}

void SwCreatorFormCanvas::updateRegistryPopupItems_() {
    if (!m_registryPopup) {
        return;
    }

    std::vector<RegistryPopup::Item> items;
    items.reserve(64);

    auto addCmd = [&](const char* label, RegistryPopup::CommandId id) {
        RegistryPopup::Item it;
        it.kind = RegistryPopup::ItemKind::Command;
        it.command = id;
        it.display = SwString(label);
        items.push_back(it);
    };

    addCmd("Undo", RegistryPopup::CommandId::Undo);
    addCmd("Redo", RegistryPopup::CommandId::Redo);
    addCmd("Cut", RegistryPopup::CommandId::Cut);
    addCmd("Copy", RegistryPopup::CommandId::Copy);
    addCmd("Paste", RegistryPopup::CommandId::Paste);
    addCmd("Delete", RegistryPopup::CommandId::Delete);

    {
        RegistryPopup::Item it;
        it.kind = RegistryPopup::ItemKind::Layout;
        it.display = "Layout: Vertical";
        it.payload = "SwVerticalLayout";
        items.push_back(it);
    }
    {
        RegistryPopup::Item it;
        it.kind = RegistryPopup::ItemKind::Layout;
        it.display = "Layout: Horizontal";
        it.payload = "SwHorizontalLayout";
        items.push_back(it);
    }
    {
        RegistryPopup::Item it;
        it.kind = RegistryPopup::ItemKind::Layout;
        it.display = "Layout: Grid";
        it.payload = "SwGridLayout";
        items.push_back(it);
    }
    {
        RegistryPopup::Item it;
        it.kind = RegistryPopup::ItemKind::Layout;
        it.display = "Layout: Form";
        it.payload = "SwFormLayout";
        items.push_back(it);
    }
    {
        RegistryPopup::Item it;
        it.kind = RegistryPopup::ItemKind::BreakLayout;
        it.display = "Break Layout";
        items.push_back(it);
    }

    // Widget "registry" (same list as the palette, MVP).
    const char* widgetClasses[] = {
        "SwPushButton",
        "SwToolButton",
        "SwLabel",
        "SwLineEdit",
        "SwCheckBox",
        "SwRadioButton",
        "SwProgressBar",
        "SwComboBox",
        "SwSpinBox",
        "SwDoubleSpinBox",
        "SwSlider",
        "SwPlainTextEdit",
        "SwTextEdit",
        "SwFrame",
        "SwGroupBox",
        "SwScrollArea",
        "SwTabWidget",
        "SwStackedWidget",
        "SwSplitter",
        "SwTableWidget",
        "SwTreeWidget",
        "SwTableView",
        "SwTreeView",
    };

    for (const char* cls : widgetClasses) {
        RegistryPopup::Item it;
        it.kind = RegistryPopup::ItemKind::Widget;
        it.display = SwString(cls);
        it.payload = SwString(cls);
        items.push_back(it);
    }

    m_registryPopup->setItems(std::move(items));
}

void SwCreatorFormCanvas::applyLayout_(const SwString& layoutName) {
    SwWidget* target = layoutTarget_();
    if (!target) {
        return;
    }

    std::vector<SwWidget*> children;
    children.reserve(32);
    for (SwObject* obj : target->children()) {
        auto* w = dynamic_cast<SwWidget*>(obj);
        if (w && w->parent() == target) {
            children.push_back(w);
        }
    }

    if (layoutName == "SwVerticalLayout") {
        auto* l = new SwVerticalLayout(target);
        target->setLayout(l);
        for (SwWidget* w : children) {
            l->addWidget(w);
        }
        return;
    }
    if (layoutName == "SwHorizontalLayout") {
        auto* l = new SwHorizontalLayout(target);
        target->setLayout(l);
        for (SwWidget* w : children) {
            l->addWidget(w);
        }
        return;
    }
    if (layoutName == "SwGridLayout") {
        auto* l = new SwGridLayout(target);
        target->setLayout(l);
        const int count = static_cast<int>(children.size());
        m_gridColumns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(std::max(1, count))))));
        for (int i = 0; i < count; ++i) {
            const int row = i / m_gridColumns;
            const int col = i % m_gridColumns;
            l->addWidget(children[static_cast<size_t>(i)], row, col);
        }
        return;
    }
    if (layoutName == "SwFormLayout") {
        auto* l = new SwFormLayout(target);
        target->setLayout(l);
        for (SwWidget* w : children) {
            l->addWidget(w);
        }
        return;
    }
}

void SwCreatorFormCanvas::breakLayout_() {
    SwWidget* target = layoutTarget_();
    if (!target) {
        return;
    }
    target->setLayout(nullptr);
}

void SwCreatorFormCanvas::setCreateClass(const SwString& className) {
    m_createClass = className;
}

SwString SwCreatorFormCanvas::createClass() const {
    return m_createClass;
}

SwWidget* SwCreatorFormCanvas::createWidgetAt(const SwString& className, int globalX, int globalY) {
    if (className.isEmpty()) {
        return nullptr;
    }
    SwWidget* w = createWidgetAt_(className, globalX, globalY);
    if (!w) {
        return nullptr;
    }
    widgetAdded(w);
    setSelectedWidget(w);
    selectionChanged(w);
    return w;
}

SwWidget* SwCreatorFormCanvas::createLayoutContainerAt(const SwString& layoutClass, int globalX, int globalY) {
    if (layoutClass.isEmpty()) {
        return nullptr;
    }

    SwWidget* w = createLayoutContainerAt_(layoutClass, globalX, globalY);
    if (!w) {
        return nullptr;
    }

    widgetAdded(w);
    setSelectedWidget(w);
    selectionChanged(w);
    return w;
}

void SwCreatorFormCanvas::updateDropPreview(int globalX, int globalY, const SwWidget* ignore) {
    SwWidget* container = findContainerAt_(globalX, globalY, ignore);
    if (container == m_dropTarget) {
        return;
    }
    m_dropTarget = container;
    update();
}

void SwCreatorFormCanvas::clearDropPreview() {
    if (!m_dropTarget) {
        return;
    }
    m_dropTarget = nullptr;
    update();
}

void SwCreatorFormCanvas::setSelectedWidget(SwWidget* widget) {
    if (m_selected == widget) {
        return;
    }
    m_selected = widget;
    update();
}

SwWidget* SwCreatorFormCanvas::selectedWidget() const {
    return m_selected;
}

const std::vector<SwWidget*>& SwCreatorFormCanvas::designWidgets() const {
    return m_designWidgets;
}

void SwCreatorFormCanvas::paintEvent(PaintEvent* event) {
    SwFrame::paintEvent(event);
    if (!event || !event->painter()) {
        return;
    }

    SwPainter* painter = event->painter();

    // --- Dot grid (Qt Designer style) ---
    {
        const SwRect cr = rect();
        const SwColor dotColor{185, 190, 200};
        const int step = 10;
        for (int y = cr.y; y < cr.y + cr.height; y += step) {
            for (int x = cr.x; x < cr.x + cr.width; x += step) {
                painter->drawLine(x, y, x + 1, y, dotColor, 1);
            }
        }
    }

    // Clip helper: intersect rect with canvas bounds so overlays never draw outside
    const SwRect cb = rect();
    auto clipToCanvas = [&](const SwRect& r) -> SwRect {
        return intersectRects_(r, cb);
    };

    // --- Layout container red dashed borders (Qt Designer style) ---
    {
        std::set<SwWidget*> containers;
        for (SwWidget* w : m_designWidgets) {
            if (!w) continue;
            SwWidget* pw = dynamic_cast<SwWidget*>(w->parent());
            if (pw && pw != this && shouldDrawLayoutContainerOverlay_(pw)) {
                containers.insert(pw);
            }
        }
        const SwColor layoutColor{227, 0, 0};
        for (SwWidget* c : containers) {
            const SwRect r = layoutContainerOverlayRect_(this, c);
            if (r.width > 0 && r.height > 0)
                painter->drawDashedRect(r, layoutColor, 1);
        }
    }

    // --- Spacer indicators (smooth continuous coil spring) ---
    {
        const SwString kIsSpacer("__SwCreator_IsSpacer");
        const SwColor springColor{80, 140, 210};
        for (SwWidget* w : m_designWidgets) {
            if (!w) continue;
            if (!w->isDynamicProperty(kIsSpacer)) continue;
            const SwAny v = w->property(kIsSpacer);
            const bool isSpacer = (v.metaType() == SwMetaType::Bool)
                                  ? (v.toString().toLower() == "true")
                                  : (!v.toString().isEmpty() && v.toString() != "0");
            if (!isSpacer) continue;
            const SwRect r = clipToCanvas(widgetRectInCanvas_(this, w));
            if (r.width <= 0 || r.height <= 0) continue;

            bool horiz = r.width >= r.height;
            if (auto* spacer = dynamic_cast<SwSpacer*>(w)) {
                horiz = spacer->direction() == SwSpacer::Direction::Horizontal;
            } else if (w->propertyExist("Orientation")) {
                const SwString orientation = w->property("Orientation").toString().trimmed().toLower();
                if (orientation.contains("vertical")) {
                    horiz = false;
                } else if (orientation.contains("horizontal")) {
                    horiz = true;
                }
            }

            // Number of coils adapts to available length
            const int capInset = 3;   // inset from edge to end-cap bar
            const int coilPad  = 2;   // gap between end-cap bar and coil region

            if (horiz) {
                const int cy = r.y + r.height / 2;
                const int halfCap = std::min(std::max(r.height / 2 - 2, 2), 10);
                const int amp = std::min(std::max(r.height / 2 - 3, 2), 7);

                // End-cap bars (thin vertical lines)
                painter->drawLine(r.x + capInset, cy - halfCap,
                                  r.x + capInset, cy + halfCap, springColor, 1);
                painter->drawLine(r.x + r.width - 1 - capInset, cy - halfCap,
                                  r.x + r.width - 1 - capInset, cy + halfCap, springColor, 1);

                // Coil region
                const int coilLeft  = r.x + capInset + 1 + coilPad;
                const int coilRight = r.x + r.width - 1 - capInset - 1 - coilPad;
                const int coilLen   = coilRight - coilLeft;
                if (coilLen > 4) {
                    // Determine number of half-periods (each half = one "bump")
                    int numHalves = std::max(2, coilLen / 7);
                    // Make it even for a symmetric look
                    if (numHalves % 2 != 0) numHalves++;
                    const double halfWidth = static_cast<double>(coilLen) / numHalves;

                    // Draw smooth sinusoidal coil via short line segments
                    const int segPerHalf = std::max(4, static_cast<int>(halfWidth));
                    int prevX = coilLeft;
                    int prevY = cy;
                    for (int h = 0; h < numHalves; ++h) {
                        const double baseX = coilLeft + h * halfWidth;
                        const int sign = (h % 2 == 0) ? -1 : 1;
                        for (int s = 1; s <= segPerHalf; ++s) {
                            const double t = static_cast<double>(s) / segPerHalf;
                            const double pi = 3.14159265358979;
                            int nx = static_cast<int>(baseX + t * halfWidth);
                            int ny = cy + static_cast<int>(sign * amp * std::sin(t * pi));
                            if (nx > coilRight) nx = coilRight;
                            painter->drawLine(prevX, prevY, nx, ny, springColor, 1);
                            prevX = nx;
                            prevY = ny;
                        }
                    }
                }
            } else {
                const int cx = r.x + r.width / 2;
                const int halfCap = std::min(std::max(r.width / 2 - 2, 2), 10);
                const int amp = std::min(std::max(r.width / 2 - 3, 2), 7);

                // End-cap bars (thin horizontal lines)
                painter->drawLine(cx - halfCap, r.y + capInset,
                                  cx + halfCap, r.y + capInset, springColor, 1);
                painter->drawLine(cx - halfCap, r.y + r.height - 1 - capInset,
                                  cx + halfCap, r.y + r.height - 1 - capInset, springColor, 1);

                // Coil region
                const int coilTop    = r.y + capInset + 1 + coilPad;
                const int coilBottom = r.y + r.height - 1 - capInset - 1 - coilPad;
                const int coilLen    = coilBottom - coilTop;
                if (coilLen > 4) {
                    int numHalves = std::max(2, coilLen / 7);
                    if (numHalves % 2 != 0) numHalves++;
                    const double halfHeight = static_cast<double>(coilLen) / numHalves;

                    const int segPerHalf = std::max(4, static_cast<int>(halfHeight));
                    int prevX = cx;
                    int prevY = coilTop;
                    for (int h = 0; h < numHalves; ++h) {
                        const double baseY = coilTop + h * halfHeight;
                        const int sign = (h % 2 == 0) ? -1 : 1;
                        for (int s = 1; s <= segPerHalf; ++s) {
                            const double t = static_cast<double>(s) / segPerHalf;
                            const double pi = 3.14159265358979;
                            int ny = static_cast<int>(baseY + t * halfHeight);
                            int nx = cx + static_cast<int>(sign * amp * std::sin(t * pi));
                            if (ny > coilBottom) ny = coilBottom;
                            painter->drawLine(prevX, prevY, nx, ny, springColor, 1);
                            prevX = nx;
                            prevY = ny;
                        }
                    }
                }
            }
        }
    }

    // --- Debug: per-class colored border + label on each design widget ---
    if (m_debugOverlay) {
        struct ClassColor { const char* prefix; SwColor color; };
        static const ClassColor kColors[] = {
            { "SwLabel",        {200, 180,   0} },
            { "SwPushButton",   {  0, 160,   0} },
            { "SwToolButton",   {  0, 160,   0} },
            { "SwLineEdit",     {  0, 180, 180} },
            { "SwTextEdit",     {  0, 180, 180} },
            { "SwPlainTextEdit",{  0, 180, 180} },
            { "SwComboBox",     {180,   0, 180} },
            { "SwSpinBox",      {180,   0, 180} },
            { "SwDoubleSpinBox",{180,   0, 180} },
            { "SwCheckBox",     {180,  80,   0} },
            { "SwRadioButton",  {180,  80,   0} },
            { "SwProgressBar",  {  0,  80, 200} },
            { "SwSlider",       {  0,  80, 200} },
            { "SwTabWidget",    {120,   0, 200} },
            { "SwSplitter",     {220, 100,   0} },
            { "SwGroupBox",     { 20, 100, 200} },
            { "SwScrollArea",   { 20, 100, 200} },
            { "SwFrame",        { 60, 140, 220} },
            { "SwTreeWidget",   { 40, 160,  80} },
            { "SwTableWidget",  { 40, 160,  80} },
            { nullptr,          {150, 150, 150} },
        };
        auto classColor = [&](const SwString& cls) -> SwColor {
            const std::string s = cls.toStdString();
            for (const ClassColor& cc : kColors) {
                if (!cc.prefix) return cc.color;
                if (s.find(cc.prefix) == 0) return cc.color;
            }
            return {150, 150, 150};
        };
        SwFont dbgFont(L"Segoe UI", 8);
        for (SwWidget* w : m_designWidgets) {
            if (!w) continue;
            const SwString cls = w->className();
            const SwColor col = classColor(cls);
            const SwRect wr = clipToCanvas(widgetRectInCanvas_(this, w));
            if (wr.width <= 0 || wr.height <= 0) continue;
            // thin colored border
            painter->drawRect(wr, col, 1);
            // class label in top-left corner
            if (wr.width >= 20 && wr.height >= 10) {
                // short name: strip "Sw" prefix
                SwString label = cls;
                if (label.startsWith("Sw")) label = label.mid(2);
                const SwRect labelRect{wr.x + 1, wr.y + 1,
                                       std::min(wr.width - 2, 90), std::min(wr.height - 2, 12)};
                painter->fillRect(labelRect, col, col, 0);
                painter->drawText(labelRect, label,
                                  DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::SingleLine),
                                  SwColor{255, 255, 255}, dbgFont);
            }
        }
    }

    if (m_dropTarget && m_dropTarget != this) {
        SwRect r = widgetRectInCanvas_(this, m_dropTarget);
        r.x -= 2;
        r.y -= 2;
        r.width += 4;
        r.height += 4;
        painter->drawRect(r, SwColor{59, 130, 246}, 3);
    }

    if (m_selected && m_selected != this) {
        const SwCreatorTheme& theme = SwCreatorTheme::current();
        const SwColor accentBlue = SwCreatorTheme::current().accentSecondary;
        const SwColor selColor = blendColor_(theme.borderStrong, accentBlue, 0.30f);
        const SwColor handleFill = blendColor_(theme.surface1, accentBlue, 0.12f);
        const SwColor handleBorder = blendColor_(theme.borderStrong, accentBlue, 0.68f);
        SwRect r = widgetRectInCanvas_(this, m_selected);
        r.x -= 1;
        r.y -= 1;
        r.width += 2;
        r.height += 2;
        painter->drawRect(r, selColor, 1);

        SwWidget* parentWidget = dynamic_cast<SwWidget*>(m_selected->parent());
        SwAbstractLayout* activeLayout = parentWidget ? parentWidget->layout() : nullptr;
        const bool managedByKnownLayout = activeLayout &&
                                          (dynamic_cast<SwBoxLayout*>(activeLayout) ||
                                           dynamic_cast<SwGridLayout*>(activeLayout) ||
                                           dynamic_cast<SwFormLayout*>(activeLayout));
        if (!managedByKnownLayout) {
            const int handleSize = 4;
            const int half = handleSize / 2;
            const int left = r.x;
            const int top = r.y;
            const int right = r.x + r.width - 1;
            const int bottom = r.y + r.height - 1;
            const int midX = (left + right) / 2;
            const int midY = (top + bottom) / 2;

            auto handleRect = [&](int cx, int cy) {
                return SwRect{cx - half, cy - half, handleSize, handleSize};
            };

            painter->fillRect(handleRect(left, top), handleFill, handleBorder, 1);
            painter->fillRect(handleRect(midX, top), handleFill, handleBorder, 1);
            painter->fillRect(handleRect(right, top), handleFill, handleBorder, 1);

            painter->fillRect(handleRect(left, midY), handleFill, handleBorder, 1);
            painter->fillRect(handleRect(right, midY), handleFill, handleBorder, 1);

            painter->fillRect(handleRect(left, bottom), handleFill, handleBorder, 1);
            painter->fillRect(handleRect(midX, bottom), handleFill, handleBorder, 1);
            painter->fillRect(handleRect(right, bottom), handleFill, handleBorder, 1);
        }
    }

    {
        const SwColor gripColor{148, 163, 184};
        const int right = rect().x + rect().width - 6;
        const int bottom = rect().y + rect().height - 6;
        painter->drawLine(right - 12, bottom, right, bottom - 12, gripColor, 1);
        painter->drawLine(right - 8, bottom, right, bottom - 8, gripColor, 1);
        painter->drawLine(right - 4, bottom, right, bottom - 4, gripColor, 1);
    }
}

void SwCreatorFormCanvas::mousePressEvent(MouseEvent* event) {
    if (!event) {
        return;
    }

    if (event->button() == SwMouseButton::Right) {
        showContextMenu_(event->x(), event->y());
        event->accept();
        return;
    }

    // Designer behavior: clicking an existing widget selects it (does not create a new one).
    SwWidget* hit = getChildUnderCursor(event->x(), event->y());
    SwWidget* topLevel = hit;
    while (topLevel && topLevel->parent() && topLevel->parent() != this) {
        topLevel = dynamic_cast<SwWidget*>(topLevel->parent());
    }
    if (topLevel && topLevel != this && topLevel->parent() == this) {
        setSelectedWidget(topLevel);
        selectionChanged(topLevel);
        event->accept();
        return;
    }

    if (!m_createClass.isEmpty() && isPointInside(event->x(), event->y())) {
        if (m_createClass.startsWith("layout:")) {
            const SwString layoutName = m_createClass.mid(7);
            (void)createLayoutContainerAt(layoutName, event->x(), event->y());
            m_createClass = SwString();
            event->accept();
            return;
        }
        (void)createWidgetAt(m_createClass, event->x(), event->y());
        m_createClass = SwString();
        event->accept();
        return;
    }

    setSelectedWidget(nullptr);
    selectionChanged(nullptr);
    SwFrame::mousePressEvent(event);
}

SwWidget* SwCreatorFormCanvas::designWidgetFromHit_(SwWidget* hit) const {
    SwWidget* w = hit;
    while (w && w != this) {
        auto it = std::find(m_designWidgets.begin(), m_designWidgets.end(), w);
        if (it != m_designWidgets.end()) {
            return w;
        }
        w = dynamic_cast<SwWidget*>(w->parent());
    }
    return nullptr;
}

bool SwCreatorFormCanvas::isContainerWidget_(const SwWidget* w) {
    if (!w) {
        return false;
    }
    return dynamic_cast<const SwFrame*>(w) || dynamic_cast<const SwGroupBox*>(w) || dynamic_cast<const SwScrollArea*>(w) ||
           dynamic_cast<const SwTabWidget*>(w) || dynamic_cast<const SwStackedWidget*>(w) || dynamic_cast<const SwSplitter*>(w);
}

SwWidget* SwCreatorFormCanvas::findContainerAt_(int globalX, int globalY, const SwWidget* ignore) const {
    SwWidget* best = nullptr;
    long long bestArea = 0;

    auto isDescendantOf = [](const SwObject* child, const SwObject* ancestor) -> bool {
        if (!child || !ancestor) {
            return false;
        }
        for (const SwObject* p = child; p; p = p->parent()) {
            if (p == ancestor) {
                return true;
            }
        }
        return false;
    };

    for (SwWidget* w : m_designWidgets) {
        if (!w) {
            continue;
        }
        if (w == ignore) {
            continue;
        }
        if (ignore && isDescendantOf(w, ignore)) {
            continue;
        }
        if (!isContainerWidget_(w)) {
            continue;
        }
        if (!w->isVisibleInHierarchy()) {
            continue;
        }
        const SwRect r = widgetRectInCanvas_(this, w);
        if (globalX < r.x || globalX > (r.x + r.width) || globalY < r.y || globalY > (r.y + r.height)) {
            continue;
        }
        const long long area = static_cast<long long>(std::max(0, r.width)) * static_cast<long long>(std::max(0, r.height));
        if (!best || area < bestArea) {
            best = w;
            bestArea = area;
        }
    }
    return best;
}

SwWidget* SwCreatorFormCanvas::dropContentParent_(SwWidget* container) {
    if (!container) {
        return nullptr;
    }

    if (auto* scroll = dynamic_cast<SwScrollArea*>(container)) {
        SwWidget* content = scroll->widget();
        if (!content) {
            auto* frame = new SwFrame(nullptr);
            frame->setStyleSheet("SwFrame { background-color: rgba(0,0,0,0); border-width: 0px; }");
            frame->resize(std::max(0, scroll->width() - 24), std::max(0, scroll->height() - 24));
            scroll->setWidget(frame);
            content = frame;
        }
        return content;
    }

    if (auto* tab = dynamic_cast<SwTabWidget*>(container)) {
        SwWidget* page = tab->currentWidget();
        if (!page) {
            auto* frame = new SwFrame(tab);
            frame->setStyleSheet("SwFrame { background-color: rgba(0,0,0,0); border-width: 0px; }");
            frame->resize(std::max(0, tab->width() - 24), std::max(0, tab->height() - 64));
            tab->addTab(frame, SwString("Page 1"));
            page = frame;
        }
        return page;
    }

    if (auto* stacked = dynamic_cast<SwStackedWidget*>(container)) {
        SwWidget* page = stacked->currentWidget();
        if (!page) {
            auto* frame = new SwFrame(stacked);
            frame->setStyleSheet("SwFrame { background-color: rgba(0,0,0,0); border-width: 0px; }");
            frame->resize(std::max(0, stacked->width() - 24), std::max(0, stacked->height() - 24));
            stacked->addWidget(frame);
            stacked->setCurrentIndex(0);
            page = frame;
        }
        return page;
    }

    return container;
}

void SwCreatorFormCanvas::showContextMenu_(int globalX, int globalY) {
    if (m_contextMenu) {
        m_contextMenu->hide();
        delete m_contextMenu;
        m_contextMenu = nullptr;
    }

    m_contextMenu = new SwMenu(this);

    const bool hasSelection = (m_selected != nullptr);

    auto* cut = m_contextMenu->addAction("Cut", [this]() { requestCut(); });
    auto* copy = m_contextMenu->addAction("Copy", [this]() { requestCopy(); });
    auto* paste = m_contextMenu->addAction("Paste", [this]() { requestPaste(); });
    auto* del = m_contextMenu->addAction("Delete", [this]() { requestDelete(); });

    cut->setEnabled(hasSelection);
    copy->setEnabled(hasSelection);
    del->setEnabled(hasSelection);

    m_contextMenu->addSeparator();
    m_contextMenu->addAction("Lay out Horizontally", [this]() { applyLayout_("SwHorizontalLayout"); });
    m_contextMenu->addAction("Lay out Vertically", [this]() { applyLayout_("SwVerticalLayout"); });
    m_contextMenu->addAction("Lay out in Grid", [this]() { applyLayout_("SwGridLayout"); });
    m_contextMenu->addAction("Lay out in Form", [this]() { applyLayout_("SwFormLayout"); });
    m_contextMenu->addAction("Break Layout", [this]() { breakLayout_(); });

    m_contextMenu->popup(globalX, globalY);
}

std::string SwCreatorFormCanvas::nextObjectNameForClass_(const SwString& className) {
    std::string cls = className.toStdString();
    if (cls.rfind("Sw", 0) == 0 && cls.size() > 2) {
        cls = cls.substr(2);
    }
    cls = toLowerFirst(std::move(cls));
    if (cls.empty()) {
        cls = "widget";
    }

    auto isTaken = [this](const std::string& candidate) -> bool {
        for (SwWidget* w : m_designWidgets) {
            if (!w) {
                continue;
            }
            const std::string existing = w->getObjectName().toStdString();
            if (!existing.empty() && existing == candidate) {
                return true;
            }
        }
        return false;
    };

    int& counter = m_classCounters[cls];
    int n = std::max(1, std::max(0, counter) + 1);
    std::string candidate = cls + "_" + std::to_string(n);
    while (isTaken(candidate)) {
        ++n;
        candidate = cls + "_" + std::to_string(n);
    }
    counter = n;
    return candidate;
}

void SwCreatorFormCanvas::defaultSizeFor_(SwWidget* w, const SwString& className) {
    if (!w) {
        return;
    }

    const std::string cls = className.toStdString();
    if (cls == "SwLabel") {
        w->resize(140, 28);
        return;
    }
    if (cls == "SwLineEdit") {
        w->resize(200, 34);
        return;
    }
    if (cls == "SwComboBox") {
        w->resize(180, 34);
        return;
    }
    if (cls == "SwCheckBox" || cls == "SwRadioButton") {
        w->resize(180, 28);
        return;
    }
    if (cls == "SwProgressBar") {
        w->resize(220, 20);
        return;
    }
    if (cls == "SwPlainTextEdit" || cls == "SwTextEdit") {
        w->resize(320, 180);
        return;
    }
    if (cls == "SwTabWidget") {
        w->resize(420, 260);
        return;
    }
    if (cls == "SwSpacer") {
        w->resize(26, 20);
        return;
    }
    if (cls == "SwTableView" || cls == "SwTreeView" || cls == "SwTableWidget" || cls == "SwTreeWidget") {
        w->resize(420, 240);
        return;
    }

    w->resize(160, 40);
}

SwWidget* SwCreatorFormCanvas::createWidgetAt_(const SwString& className, int globalX, int globalY) {
    SwWidget* parentForNewWidget = this;

    if (isPointInside(globalX, globalY)) {
        if (SwWidget* container = findContainerAt_(globalX, globalY, nullptr)) {
            if (SwWidget* contentParent = dropContentParent_(container)) {
                parentForNewWidget = contentParent;
            }
        }
    }

    SwWidget* w = swui::UiFactory::instance().createWidget(className.toStdString(), parentForNewWidget);
    if (!w) {
        return nullptr;
    }

    w->setFocusPolicy(FocusPolicyEnum::NoFocus);

    const std::string objectName = nextObjectNameForClass_(className);
    w->setObjectName(SwString(objectName));

    defaultSizeFor_(w, className);

    // If no (known) layout is active, place with absolute geometry.
    SwWidget* layoutHost = dynamic_cast<SwWidget*>(w->parent());
    if (!layoutHost) {
        layoutHost = this;
    }

    if (auto* splitter = dynamic_cast<SwSplitter*>(layoutHost)) {
        splitter->addWidget(w);
    }

    SwAbstractLayout* activeLayout = layoutHost->layout();
    const bool managedByKnownLayout = dynamic_cast<SwSplitter*>(layoutHost) ||
                                      (activeLayout &&
                                       (dynamic_cast<SwBoxLayout*>(activeLayout) ||
                                        dynamic_cast<SwGridLayout*>(activeLayout) ||
                                        dynamic_cast<SwFormLayout*>(activeLayout)));

    if (auto* spacer = dynamic_cast<SwSpacer*>(w)) {
        if (dynamic_cast<SwVerticalLayout*>(activeLayout)) {
            spacer->setDirection(SwSpacer::Direction::Vertical);
            spacer->changeSize(20, 26, SwSizePolicy::Minimum, SwSizePolicy::Expanding);
        } else if (dynamic_cast<SwHorizontalLayout*>(activeLayout)) {
            spacer->setDirection(SwSpacer::Direction::Horizontal);
            spacer->changeSize(26, 20, SwSizePolicy::Expanding, SwSizePolicy::Minimum);
        }
    }

    if (!managedByKnownLayout) {
        const SwRect bounds = layoutHost->rect();
        const SwPoint parentPos = layoutHost->mapFrom(this, SwPoint{globalX, globalY});
        int x = parentPos.x;
        int y = parentPos.y;

        const int pad = 10;
        x = std::max(bounds.x + pad, std::min(x, bounds.x + std::max(0, bounds.width - w->width() - pad)));
        y = std::max(bounds.y + pad, std::min(y, bounds.y + std::max(0, bounds.height - w->height() - pad)));

        w->move(x, y);
    }

    registerDesignWidget(w);

    return w;
}

SwWidget* SwCreatorFormCanvas::createLayoutContainerAt_(const SwString& layoutClass, int globalX, int globalY) {
    SwWidget* parentForNewWidget = this;

    if (isPointInside(globalX, globalY)) {
        if (SwWidget* container = findContainerAt_(globalX, globalY, nullptr)) {
            if (SwWidget* contentParent = dropContentParent_(container)) {
                parentForNewWidget = contentParent;
            }
        }
    }

    auto* frame = new SwFrame(parentForNewWidget);
    frame->setFocusPolicy(FocusPolicyEnum::NoFocus);
    frame->setFrameShape(SwFrame::Shape::Box);
    frame->setStyleSheet(R"(
        SwFrame {
            background-color: rgba(0,0,0,0);
            border-color: rgb(239, 68, 68);
            border-width: 2px;
            border-radius: 0px;
        }
    )");

    frame->resize(360, 240);
    frame->setObjectName(SwString(nextObjectNameForClass_(layoutClass)));

    // Place with absolute geometry unless the parent is a known layout.
    SwWidget* layoutHost = dynamic_cast<SwWidget*>(frame->parent());
    if (!layoutHost) {
        layoutHost = this;
    }
    SwAbstractLayout* activeLayout = layoutHost->layout();
    const bool managedByKnownLayout = dynamic_cast<SwSplitter*>(layoutHost) ||
                                      (activeLayout &&
                                       (dynamic_cast<SwBoxLayout*>(activeLayout) ||
                                        dynamic_cast<SwGridLayout*>(activeLayout) ||
                                        dynamic_cast<SwFormLayout*>(activeLayout)));
    if (!managedByKnownLayout) {
        const SwRect bounds = layoutHost->rect();
        const SwPoint parentPos = layoutHost->mapFrom(this, SwPoint{globalX, globalY});
        int x = parentPos.x;
        int y = parentPos.y;

        const int pad = 10;
        x = std::max(bounds.x + pad, std::min(x, bounds.x + std::max(0, bounds.width - frame->width() - pad)));
        y = std::max(bounds.y + pad, std::min(y, bounds.y + std::max(0, bounds.height - frame->height() - pad)));

        frame->move(x, y);
    }

    SwAbstractLayout* l = nullptr;
    if (layoutClass == "SwVerticalLayout") {
        l = new SwVerticalLayout(frame);
    } else if (layoutClass == "SwHorizontalLayout") {
        l = new SwHorizontalLayout(frame);
    } else if (layoutClass == "SwGridLayout") {
        l = new SwGridLayout(frame);
    } else if (layoutClass == "SwFormLayout") {
        l = new SwFormLayout(frame);
    }
    if (l) {
        l->setMargin(10);
        l->setSpacing(10);
        frame->setLayout(l);
    }

    registerDesignWidget(frame);
    return frame;
}
