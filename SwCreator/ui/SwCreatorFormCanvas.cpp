#include "SwCreatorFormCanvas.h"

#include "SwUiLoader.h"

#include "SwMenu.h"

#include "SwGroupBox.h"
#include "SwLineEdit.h"
#include "SwListWidget.h"
#include "SwScrollArea.h"
#include "SwSplitter.h"
#include "SwStackedWidget.h"
#include "SwTabWidget.h"
#include "SwWidgetPlatformAdapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

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
    void paintEvent(PaintEvent*) override {}

    void mousePressEvent(MouseEvent* event) override {
        if (!event || !m_owner) {
            return;
        }

        if (event->button() == SwMouseButton::Right) {
            SwWidget* hit = m_owner->getChildUnderCursor(event->x(), event->y(), this);
            SwWidget* w = m_owner->designWidgetFromHit_(hit);
            m_owner->setSelectedWidget(w);
            m_owner->selectionChanged(w);
            if (w) {
                w->setFocus(true);
            }
            m_owner->showContextMenu_(event->x(), event->y());
            event->accept();
            return;
        }

        if (event->button() != SwMouseButton::Left) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const SwString createClass = m_owner->createClass();
        const SwRect canvasRect = m_owner->getRect();
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
            SwWidget* parentWidget = dynamic_cast<SwWidget*>(selected->parent());
            SwAbstractLayout* activeLayout = parentWidget ? parentWidget->layout() : nullptr;
            const bool managedByKnownLayout = activeLayout &&
                                              (dynamic_cast<SwBoxLayout*>(activeLayout) ||
                                               dynamic_cast<SwGridLayout*>(activeLayout) ||
                                               dynamic_cast<SwFormLayout*>(activeLayout));
            if (!managedByKnownLayout) {
                const int mask = hitTestResizeMask_(selected->getRect(), event->x(), event->y(), kResizeHandleMargin);
                if (mask != ResizeNone) {
                    m_pressed = true;
                    m_dragging = false;
                    m_pressX = event->x();
                    m_pressY = event->y();
                    m_dragWidget = selected;
                    m_resizing = true;
                    m_resizeMask = mask;
                    m_resizeStartRect = selected->getRect();
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
        if (w) {
            w->setFocus(true);
        }

        m_pressed = true;
        m_dragging = false;
        m_pressX = event->x();
        m_pressY = event->y();
        m_dragWidget = nullptr;
        m_resizing = false;
        m_resizeMask = ResizeNone;
        m_owner->m_dropTarget = nullptr;

        if (w) {
            SwWidget* parentWidget = dynamic_cast<SwWidget*>(w->parent());
            SwAbstractLayout* activeLayout = parentWidget ? parentWidget->layout() : nullptr;
            const bool managedByKnownLayout = activeLayout &&
                                              (dynamic_cast<SwBoxLayout*>(activeLayout) ||
                                               dynamic_cast<SwGridLayout*>(activeLayout) ||
                                               dynamic_cast<SwFormLayout*>(activeLayout));
            if (!managedByKnownLayout) {
                const SwRect r = w->getRect();
                const int mask = hitTestResizeMask_(r, m_pressX, m_pressY, kResizeHandleMargin);
                if (mask != ResizeNone) {
                    m_dragWidget = w;
                    m_resizing = true;
                    m_resizeMask = mask;
                    m_resizeStartRect = r;
                    setCursor(cursorForResizeMask_(mask));
                } else {
                    m_dragWidget = w;
                    m_offsetX = m_pressX - r.x;
                    m_offsetY = m_pressY - r.y;
                    m_startX = r.x;
                    m_startY = r.y;
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
            CursorType desired = CursorType::Arrow;
            if (SwWidget* selected = m_owner->selectedWidget()) {
                SwWidget* parentWidget = dynamic_cast<SwWidget*>(selected->parent());
                SwAbstractLayout* activeLayout = parentWidget ? parentWidget->layout() : nullptr;
                const bool managedByKnownLayout = activeLayout &&
                                                  (dynamic_cast<SwBoxLayout*>(activeLayout) ||
                                                   dynamic_cast<SwGridLayout*>(activeLayout) ||
                                                   dynamic_cast<SwFormLayout*>(activeLayout));
                if (!managedByKnownLayout) {
                    const int mask = hitTestResizeMask_(selected->getRect(), event->x(), event->y(), kResizeHandleMargin);
                    if (mask != ResizeNone) {
                        desired = cursorForResizeMask_(mask);
                    }
                }
            }
            setCursor(desired);
            SwWidget::mouseMoveEvent(event);
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

            const int deltaX = event->x() - m_pressX;
            const int deltaY = event->y() - m_pressY;

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
            const SwRect bounds = parentWidget ? parentWidget->getRect() : m_owner->getRect();
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

        const SwRect bounds = m_owner->getRect();
        int newX = event->x() - m_offsetX;
        int newY = event->y() - m_offsetY;
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
        SwWidget* dragged = m_dragWidget;
        SwWidget* dropTarget = m_owner->m_dropTarget;

        m_pressed = false;
        m_dragging = false;
        m_resizing = false;
        m_resizeMask = ResizeNone;
        m_dragWidget = nullptr;
        m_owner->m_dropTarget = nullptr;
        m_owner->update();
        setCursor(CursorType::Arrow);

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
                    for (SwObject* obj : parent->getChildren()) {
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

        const SwRect bounds = newParent->getRect();
        SwRect r = dragged->getRect();
        const int pad = 6;
        const int x = clampInt(r.x,
                               bounds.x + pad,
                               bounds.x + std::max(pad, bounds.width - r.width - pad));
        const int y = clampInt(r.y,
                               bounds.y + pad,
                               bounds.y + std::max(pad, bounds.height - r.height - pad));
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

    static constexpr int kResizeHandleMargin = 6;

    static int hitTestResizeMask_(const SwRect& rect, int x, int y, int margin) {
        const int left = rect.x;
        const int top = rect.y;
        const int right = rect.x + rect.width;
        const int bottom = rect.y + rect.height;

        const bool nearLeft = std::abs(x - left) <= margin;
        const bool nearRight = std::abs(x - right) <= margin;
        const bool nearTop = std::abs(y - top) <= margin;
        const bool nearBottom = std::abs(y - bottom) <= margin;

        const bool withinY = (y >= top - margin) && (y <= bottom + margin);
        const bool withinX = (x >= left - margin) && (x <= right + margin);

        int mask = ResizeNone;
        if (withinY && nearLeft) mask |= ResizeLeft;
        if (withinY && nearRight) mask |= ResizeRight;
        if (withinX && nearTop) mask |= ResizeTop;
        if (withinX && nearBottom) mask |= ResizeBottom;
        return mask;
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

    SwCreatorFormCanvas* m_owner{nullptr};
    bool m_pressed{false};
    bool m_dragging{false};
    bool m_resizing{false};
    int m_resizeMask{ResizeNone};
    SwRect m_resizeStartRect;
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
        const SwRect r = getRect();
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
            const SwRect r = m_popup->getRect();
            const bool inside = event->x() >= r.x && event->x() <= (r.x + r.width) &&
                                event->y() >= r.y && event->y() <= (r.y + r.height);
            if (!inside) {
                m_owner->hideRegistryPopup_();
                if (m_root) {
                    MouseEvent forwarded(EventType::MousePressEvent, event->x(), event->y());
                    static_cast<SwWidgetInterface*>(m_root)->mousePressEvent(&forwarded);
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

    SwObject::connect(this, &SwCreatorFormCanvas::FocusChanged, this, [this](bool focus) {
        if (focus) {
            setSelectedWidget(nullptr);
            selectionChanged(nullptr);
        }
    });

    m_designOverlay = new DesignOverlay(this, this);
    const SwRect r = getRect();
    m_designOverlay->move(r.x, r.y);
    m_designOverlay->resize(r.width, r.height);

    SwObject::connect(this, &SwWidget::resized, this, [this](int, int) {
        if (!m_designOverlay) {
            return;
        }
        const SwRect r = getRect();
        m_designOverlay->move(r.x, r.y);
        m_designOverlay->resize(r.width, r.height);
    });
    SwObject::connect(this, &SwWidget::moved, this, [this](int, int) {
        if (!m_designOverlay) {
            return;
        }
        const SwRect r = getRect();
        m_designOverlay->move(r.x, r.y);
        m_designOverlay->resize(r.width, r.height);
    });
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
                for (SwObject* obj : parentWidget->getChildren()) {
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
                for (SwObject* obj : parent->getChildren()) {
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

    const SwRect bounds = newParent->getRect();
    SwRect r = w->getRect();
    const int pad = 6;
    const int nx = clampInt(r.x,
                            bounds.x + pad,
                            bounds.x + std::max(pad, bounds.width - r.width - pad));
    const int ny = clampInt(r.y,
                            bounds.y + pad,
                            bounds.y + std::max(pad, bounds.height - r.height - pad));
    w->move(nx, ny);

    update();
    return true;
}

SwWidget* SwCreatorFormCanvas::layoutTarget_() const {
    if (m_selected) {
        // If the selected widget has direct widget children, allow applying layout to it.
        bool hasDirectChild = false;
        for (SwObject* obj : m_selected->getChildren()) {
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
    for (SwObject* obj : target->getChildren()) {
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
    w->setFocus(true);
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
    w->setFocus(true);
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

    if (m_dropTarget && m_dropTarget != this) {
        SwRect r = m_dropTarget->getRect();
        r.x -= 2;
        r.y -= 2;
        r.width += 4;
        r.height += 4;
        painter->drawRect(r, SwColor{59, 130, 246}, 3);
    }

    if (m_selected) {
        SwRect r = m_selected->getRect();
        r.x -= 2;
        r.y -= 2;
        r.width += 4;
        r.height += 4;
        painter->drawRect(r, SwColor{99, 102, 241}, 2);

        SwWidget* parentWidget = dynamic_cast<SwWidget*>(m_selected->parent());
        SwAbstractLayout* activeLayout = parentWidget ? parentWidget->layout() : nullptr;
        const bool managedByKnownLayout = activeLayout &&
                                          (dynamic_cast<SwBoxLayout*>(activeLayout) ||
                                           dynamic_cast<SwGridLayout*>(activeLayout) ||
                                           dynamic_cast<SwFormLayout*>(activeLayout));
        if (!managedByKnownLayout) {
            const int handleSize = 8;
            const int half = handleSize / 2;
            const int left = r.x;
            const int top = r.y;
            const int right = r.x + r.width;
            const int bottom = r.y + r.height;
            const int midX = (left + right) / 2;
            const int midY = (top + bottom) / 2;

            auto handleRect = [&](int cx, int cy) {
                return SwRect{cx - half, cy - half, handleSize, handleSize};
            };

            const SwColor handleBorder{99, 102, 241};
            const SwColor handleFill{255, 255, 255};
            painter->fillRoundedRect(handleRect(left, top), 3, handleFill, handleBorder, 1);
            painter->fillRoundedRect(handleRect(midX, top), 3, handleFill, handleBorder, 1);
            painter->fillRoundedRect(handleRect(right, top), 3, handleFill, handleBorder, 1);

            painter->fillRoundedRect(handleRect(left, midY), 3, handleFill, handleBorder, 1);
            painter->fillRoundedRect(handleRect(right, midY), 3, handleFill, handleBorder, 1);

            painter->fillRoundedRect(handleRect(left, bottom), 3, handleFill, handleBorder, 1);
            painter->fillRoundedRect(handleRect(midX, bottom), 3, handleFill, handleBorder, 1);
            painter->fillRoundedRect(handleRect(right, bottom), 3, handleFill, handleBorder, 1);
        }
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
        const SwRect r = w->getRect();
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

    w->setFocusPolicy(FocusPolicyEnum::Accept);

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
    if (!managedByKnownLayout) {
        const SwRect bounds = layoutHost->getRect();
        int x = globalX;
        int y = globalY;

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
    frame->setFocusPolicy(FocusPolicyEnum::Accept);
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
        const SwRect bounds = layoutHost->getRect();
        int x = globalX;
        int y = globalY;

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
