/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

/**
 * @file SwMenu.h
 * @ingroup core_gui
 * @brief Declares `SwMenu`, a popup menu container built from Sw actions and widgets.
 *
 * @details
 * `SwMenu` implements a menu system entirely inside the toolkit instead of deferring to platform
 * menu APIs. The menu owns a list of `SwAction` objects and materializes them into two internal
 * widgets when displayed:
 * - a transparent overlay that captures outside interaction,
 * - a popup list that renders rows, highlights, check marks, icons, and submenu arrows.
 *
 * This structure gives the toolkit full control over painting, keyboard navigation, submenu
 * placement, and event forwarding to root widgets such as menu bars.
 */

#include "SwAction.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"

#include "core/object/SwPointer.h"
#include "core/runtime/SwTimer.h"
#include "core/types/SwVector.h"

#include <algorithm>
#include <functional>

/**
 * @class SwMenu
 * @brief Action container that can show itself as a transient popup menu.
 *
 * @details
 * A `SwMenu` stores actions independently from presentation and only builds the popup widgets on
 * demand. This lazy approach keeps the data model lightweight while still supporting:
 * - regular clickable actions,
 * - separators,
 * - nested submenus,
 * - keyboard navigation,
 * - outside-click dismissal through a transparent overlay,
 * - "hover keep alive" behavior for menu-bar style interactions.
 *
 * `popup()` anchors the menu relative to a root widget, `hide()` collapses the current menu branch,
 * and `hideAll()` dismisses the whole root menu chain. The `triggered` signal is emitted with the
 * originating action when the user activates an entry.
 */
class SwMenu : public SwObject {
    SW_OBJECT(SwMenu, SwObject)

public:
    /**
     * @brief Constructs an empty menu.
     * @param parent Optional owning object or parent menu.
     */
    explicit SwMenu(SwObject* parent = nullptr)
        : SwObject(parent) {}

    /**
     * @brief Creates a text-only action, adds it to the menu, and returns it.
     * @param text Visible label shown in the popup row.
     * @return Newly created action owned by the menu.
     */
    SwAction* addAction(const SwString& text) {
        auto* action = new SwAction(text, this);
        addAction(action);
        return action;
    }

    /**
     * @brief Creates an action with an icon and adds it to the menu.
     * @param icon Icon painted in the action row.
     * @param text Visible action label.
     * @return Newly created action owned by the menu.
     */
    SwAction* addAction(const SwImage& icon, const SwString& text) {
        SwAction* action = addAction(text);
        if (action) {
            action->setIcon(icon);
        }
        return action;
    }

    /**
     * @brief Creates an action and wires a callback to its `triggered` signal.
     * @param text Visible action label.
     * @param callback Function invoked when the action is triggered.
     * @return Newly created action owned by the menu.
     */
    SwAction* addAction(const SwString& text, const std::function<void()>& callback) {
        SwAction* action = addAction(text);
        if (callback) {
            SwObject::connect(action, &SwAction::triggered, this, [callback](bool) { callback(); });
        }
        return action;
    }

    /**
     * @brief Creates an action with both icon and callback.
     * @param icon Icon shown beside the action text.
     * @param text Visible action label.
     * @param callback Function invoked when the action is triggered.
     * @return Newly created action owned by the menu.
     */
    SwAction* addAction(const SwImage& icon, const SwString& text, const std::function<void()>& callback) {
        SwAction* action = addAction(text, callback);
        if (action) {
            action->setIcon(icon);
        }
        return action;
    }

    /**
     * @brief Adds an already constructed action to the menu.
     * @param action Action instance to append.
     *
     * @details
     * The method adopts the action when it does not already have a parent and hooks its `changed`
     * signal so the popup geometry and painting stay in sync with runtime updates.
     */
    void addAction(SwAction* action) {
        if (!action) {
            return;
        }
        if (!action->parent()) {
            action->setParent(this);
        }
        m_actions.push_back(action);

        SwObject::connect(action, &SwAction::changed, this, [this]() {
            if (m_popup) {
                m_popup->updateGeometryFromActions();
                m_popup->update();
            }
        });

        if (m_popup) {
            m_popup->updateGeometryFromActions();
            m_popup->update();
        }
    }

    /**
     * @brief Appends a non-interactive separator row.
     * @return Separator action object owned by the menu.
     */
    SwAction* addSeparator() {
        auto* action = new SwAction(SwString(), this);
        action->setSeparator(true);
        addAction(action);
        return action;
    }

    /**
     * @brief Creates a submenu entry and returns the nested menu instance.
     * @param text Visible label of the parent row.
     * @return Newly created submenu owned by this menu.
     */
    SwMenu* addMenu(const SwString& text) {
        auto* sub = new SwMenu(this);
        SwAction* a = addAction(text);
        if (a) {
            a->setMenu(sub);
        }
        return sub;
    }

    /**
     * @brief Creates a submenu entry with an icon.
     * @param icon Icon displayed for the submenu row.
     * @param text Visible label of the parent row.
     * @return Newly created submenu owned by this menu.
     */
    SwMenu* addMenu(const SwImage& icon, const SwString& text) {
        SwMenu* sub = addMenu(text);
        SwAction* a = m_actions.isEmpty() ? nullptr : m_actions.back();
        if (a) {
            a->setIcon(icon);
        }
        return sub;
    }

    /**
     * @brief Returns the actions currently stored in the menu.
     * @return Snapshot of the action list in insertion order.
     */
    SwVector<SwAction*> actions() const { return m_actions; }

    /**
     * @brief Shows the menu popup near the provided root-widget coordinates.
     * @param x Horizontal popup origin in the root widget coordinate space.
     * @param y Vertical popup origin in the root widget coordinate space.
     *
     * @details
     * The menu ensures that its overlay and popup widgets exist, clamps the requested geometry to
     * the visible root widget bounds, updates the popup size from the current action list, and then
     * shows both the overlay and popup widgets.
     */
    void popup(int x, int y) {
        ensurePopup();
        if (!m_root || !m_overlay || !m_popup) {
            return;
        }

        aboutToShow();

        m_overlay->move(0, 0);
        m_overlay->resize(m_root->width(), m_root->height());

        m_popup->updateGeometryFromActions();
        const SwSize size = m_popup->sizeHint();

        const int maxX = std::max(0, m_root->width() - size.width - 2);
        const int maxY = std::max(0, m_root->height() - size.height - 2);
        const int px = clampInt(x, 2, maxX);
        const int py = clampInt(y, 2, maxY);

        m_popup->move(px, py);
        m_popup->resize(size.width, size.height);

        m_overlay->show();
        m_popup->show();
        m_overlay->update();
        m_popup->update();
    }

    /**
     * @brief Shows the menu and preserves an additional hover-safe rectangle.
     * @param x Horizontal popup origin in root-widget coordinates.
     * @param y Vertical popup origin in root-widget coordinates.
     * @param hoverKeepAliveRect Rectangle that should not immediately close the menu when crossed.
     *
     * @details
     * This overload is primarily used by menu bars so pointer travel between the source widget and
     * the popup does not collapse the menu chain too aggressively.
     */
    void popup(int x, int y, const SwRect& hoverKeepAliveRect) {
        m_hoverCloseEnabled = true;
        m_hoverKeepAliveRect = hoverKeepAliveRect;
        popup(x, y);
    }

    /**
     * @brief Hides this menu and any currently open submenu branch.
     *
     * @details
     * The call clears hover-close state, closes nested submenus, emits `aboutToHide` when the popup
     * was visible, and hides the overlay when this menu owns it.
     */
    void hide() {
        closeSubMenu_();
        stopHoverCloseTimer_();
        m_hoverCloseEnabled = false;
        m_hoverKeepAliveRect = SwRect();

        if (m_popup && m_popup->getVisible()) {
            aboutToHide();
        }
        if (m_popup) {
            m_popup->hide();
        }
        if (m_overlay && m_ownsOverlay) {
            m_overlay->hide();
        }
    }

    /**
     * @brief Hides the entire root menu chain.
     *
     * @details
     * This is the preferred dismissal path when an outside click or Escape key should close all
     * nested menus rather than only the current branch.
     */
    void hideAll() {
        if (SwMenu* root = rootMenu_()) {
            root->hide();
        }
    }

    /**
     * @brief Returns whether the popup widget for this menu is currently visible.
     * @return `true` when the popup exists and is shown.
     */
    bool isVisible() const { return m_popup && m_popup->getVisible(); }

signals:
    DECLARE_SIGNAL_VOID(aboutToShow);   ///< Emitted just before the popup becomes visible.
    DECLARE_SIGNAL_VOID(aboutToHide);   ///< Emitted just before the popup is hidden.
    DECLARE_SIGNAL(triggered, SwAction*); ///< Emitted when an action from this menu tree is activated.

private:
    class PopupList;

    /**
     * @brief Full-screen transparent widget that dismisses menus and forwards outside events.
     *
     * @details
     * The overlay sits on top of the root widget while a menu is open. It detects clicks outside
     * the visible popup chain, closes the menu hierarchy, and optionally forwards the original event
     * back to the underlying root widget so controls such as menu bars can immediately react.
     */
    class PopupOverlay final : public SwWidget {
        SW_OBJECT(PopupOverlay, SwWidget)

    public:
        /**
         * @brief Constructs the overlay that captures interaction outside the popup.
         * @param owner Menu tree controlled by this overlay.
         * @param root Root widget that receives forwarded outside-click events when needed.
         * @param parent Parent widget, typically the same root widget.
         */
        PopupOverlay(SwMenu* owner, SwWidget* root, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner)
            , m_root(root) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
            setFocusPolicy(FocusPolicyEnum::Strong);
        }

        /**
         * @brief Handles outside clicks by closing menus or forwarding the event to the root widget.
         * @param event Mouse press event expressed in root-widget coordinates.
         */
        void mousePressEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }

            const int px = event->x();
            const int py = event->y();

            if (m_owner->isPointInsideAnyPopup_(px, py)) {
                SwWidget::mousePressEvent(event);
                return;
            }

            if (m_owner->isPointInsideHoverKeepAlive_(px, py)) {
                if (m_root) {
                    // Hide self first so the forwarded event reaches the actual widget
                    // (menubar) instead of looping back through this overlay.
                    hide();
                    MouseEvent forwarded(EventType::MousePressEvent,
                                         px,
                                         py,
                                         event->button(),
                                         event->isCtrlPressed(),
                                         event->isShiftPressed(),
                                         event->isAltPressed());
                    m_root->dispatchMouseEventFromRoot(forwarded);
                }
                event->accept();
                return;
            }

            if (!m_owner->isPointInsideAnyPopup_(px, py)) {
                m_owner->hideAll();
                if (m_root) {
                    MouseEvent forwarded(EventType::MousePressEvent,
                                         px,
                                         py,
                                         event->button(),
                                         event->isCtrlPressed(),
                                         event->isShiftPressed(),
                                         event->isAltPressed());
                    m_root->dispatchMouseEventFromRoot(forwarded);
                }
                event->accept();
                return;
            }

            SwWidget::mousePressEvent(event);
        }

        /**
         * @brief Reports pointer movement back to the owning menu for hover-close tracking.
         * @param event Mouse move event expressed in root-widget coordinates.
         */
        void mouseMoveEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            SwWidget::mouseMoveEvent(event);
            m_owner->onOverlayMouseMove_(event->x(), event->y());
        }

        /**
         * @brief Handles overlay keyboard shortcuts such as Escape-to-close.
         * @param event Key event received by the overlay widget.
         */
        void keyPressEvent(KeyEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            SwWidget::keyPressEvent(event);
            if (event->isAccepted()) {
                return;
            }
            if (SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
                m_owner->hideAll();
                event->accept();
            }
        }

    private:
        SwMenu* m_owner{nullptr};
        SwWidget* m_root{nullptr};
    };

    /**
     * @brief Internal popup surface that paints menu rows and handles keyboard or pointer selection.
     *
     * @details
     * `PopupList` is responsible for geometry calculation, row hit testing, hover tracking, and
     * action activation. It stays deliberately focused on presentation and delegates higher-level
     * submenu management back to its owning `SwMenu`.
     */
    class PopupList final : public SwWidget {
        SW_OBJECT(PopupList, SwWidget)

    public:
        /**
         * @brief Constructs the popup list that renders the menu action rows.
         * @param owner Menu whose actions are painted by this popup.
         * @param parent Parent widget, usually the shared overlay.
         */
        explicit PopupList(SwMenu* owner, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner) {
            setCursor(CursorType::Hand);
            setFocusPolicy(FocusPolicyEnum::Strong);
            setStyleSheet(R"(
                PopupList {
                    background-color: rgb(255, 255, 255);
                    border-color: rgb(220, 224, 232);
                    border-width: 1px;
                    border-radius: 0px;
                    padding: 4px;
                    color: rgb(24, 28, 36);
                    font-size: 13px;
                }
            )");
        }

        /**
         * @brief Recomputes cached popup dimensions from the current action list.
         *
         * @details
         * Text width, action count, icon gutters, checkmark space, and submenu arrow space all
         * contribute to the geometry cached for `sizeHint()` and popup placement.
         */
        void updateGeometryFromActions() {
            const int n = m_owner ? m_owner->m_actions.size() : 0;
            const int h = m_padding * 2 + n * m_itemHeight;
            m_cachedHeight = std::max(0, h);

            const int kRowPad = 8;
            const int kCheckAreaW = 18;
            const int kIconAreaW = 18;
            const int kIconTextGap = 6;
            const int kArrowAreaW = 14;
            const int kRightPad = 8;
            m_textLeftPad = kRowPad + kCheckAreaW + kIconAreaW + kIconTextGap;
            m_textRightPad = kRightPad + kArrowAreaW;

            int w = m_minWidth;
            if (m_owner) {
                const SwFont font = getFont();
                const auto handle = nativeWindowHandle();
                for (SwAction* action : m_owner->m_actions) {
                    if (!action || action->getSeparator()) {
                        continue;
                    }
                    const SwString t = action->getText();
                    const int tw = SwWidgetPlatformAdapter::textWidthUntil(handle, t, font, t.size(), 5000);
                    w = std::max(w, tw + m_textLeftPad + m_textRightPad);
                }
            }
            m_cachedWidth = std::max(m_minWidth, w);
        }

        /**
         * @brief Returns the preferred popup size derived from cached geometry.
         * @return Rectangle whose width and height match the currently measured content.
         */
        SwSize sizeHint() const override {
            return SwSize{m_cachedWidth, m_cachedHeight};
        }

        /**
         * @brief Paints the popup frame and all visible rows.
         * @param event Paint event providing the active painter.
         */
        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }

            StyleSheet* sheet = getToolSheet();
            SwColor bg{255, 255, 255};
            float bgAlpha = 1.0f;
            bool paintBg = true;

            SwColor border{220, 224, 232};
            int borderWidth = 1;
            int radius = 12;

            resolveBackground(sheet, bg, bgAlpha, paintBg);
            resolveBorder(sheet, border, borderWidth, radius);

            const SwRect bounds = rect();
            painter->fillRoundedRect(bounds, radius, bg, border, borderWidth);

            const SwColor textColor = resolveTextColor(sheet, SwColor{24, 28, 36});
            const SwColor disabledText{148, 163, 184};
            const SwColor hoverFill{241, 245, 249};
            const SwColor hoverBorder{241, 245, 249};
            const SwColor sep{226, 232, 240};

            const int kRowPad = 8;
            const int kCheckAreaW = 18;
            const int kIconAreaW = 18;
            const int kIconSize = 16;
            const int kArrowAreaW = 14;
            const int kRightPad = 8;

            const int startY = bounds.y + m_padding;
            const int startX = bounds.x + m_padding;
            const int contentW = std::max(0, bounds.width - 2 * m_padding);

            for (int i = 0; m_owner && i < m_owner->m_actions.size(); ++i) {
                SwAction* action = m_owner->m_actions[i];
                SwRect row{startX, startY + i * m_itemHeight, contentW, m_itemHeight};

                if (!action) {
                    continue;
                }

                if (action->getSeparator()) {
                    const int y = row.y + row.height / 2;
                    painter->drawLine(row.x + 6, y, row.x + row.width - 6, y, sep, 1);
                    continue;
                }

                const bool enabled = action->getEnabled();
                const bool hovered = (i == m_hoverIndex);

                if (hovered && enabled) {
                    SwRect hi{row.x + 2, row.y + 2, std::max(0, row.width - 4), std::max(0, row.height - 4)};
                    painter->fillRoundedRect(hi, 10, hoverFill, hoverBorder, 0);
                }

                if (action->getCheckable() && action->getChecked()) {
                    const int cx = row.x + kRowPad + kCheckAreaW / 2;
                    const int cy = row.y + row.height / 2;
                    painter->drawLine(cx - 4, cy, cx - 1, cy + 3, enabled ? textColor : disabledText, 2);
                    painter->drawLine(cx - 1, cy + 3, cx + 5, cy - 3, enabled ? textColor : disabledText, 2);
                }

                if (action->hasIcon()) {
                    const SwRect iconRect{row.x + kRowPad + kCheckAreaW,
                                          row.y + (row.height - kIconSize) / 2,
                                          kIconSize,
                                          kIconSize};
                    painter->drawImage(iconRect, action->icon(), nullptr);
                }

                SwRect textRect{row.x + m_textLeftPad,
                                row.y,
                                std::max(0, row.width - m_textLeftPad - m_textRightPad),
                                row.height};
                painter->drawText(textRect,
                                  action->getText(),
                                  DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  enabled ? textColor : disabledText,
                                  getFont());

                if (action->hasMenu()) {
                    const int midY = row.y + row.height / 2;
                    const int tipX = row.x + row.width - kRightPad - 2;
                    const int leftX = tipX - 6;
                    painter->drawLine(leftX, midY - 5, tipX, midY, enabled ? textColor : disabledText, 2);
                    painter->drawLine(leftX, midY + 5, tipX, midY, enabled ? textColor : disabledText, 2);
                }
            }
        }

        /**
         * @brief Updates row hover state as the pointer moves inside the popup.
         * @param event Mouse move event in popup coordinates.
         */
        void mouseMoveEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }

            const int px = event->x();
            const int py = event->y();
            const int idx = indexAt(px, py);

            if (idx >= 0) {
                if (idx != m_hoverIndex) {
                    m_hoverIndex = idx;
                    update();
                    m_owner->onPopupHoverChanged_(idx, rowRectForIndex_(idx));
                }
                event->accept();
                return;
            }

            if (m_owner->isPointInsideOpenSubMenuBridge_(px, py)) {
                event->accept();
                return;
            }

            if (m_hoverIndex != -1) {
                m_hoverIndex = -1;
                update();
                m_owner->onPopupHoverChanged_(-1, SwRect());
            }
            event->accept();
        }

        /**
         * @brief Activates the pointed action or opens its submenu.
         * @param event Mouse press event in popup coordinates.
         */
        void mousePressEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            const int idx = indexAt(event->x(), event->y());
            SwAction* action = actionAt(idx);
            if (action && !action->getSeparator() && action->getEnabled()) {
                if (action->hasMenu()) {
                    m_hoverIndex = idx;
                    update();
                    m_owner->onPopupHoverChanged_(idx, rowRectForIndex_(idx));
                    event->accept();
                    return;
                }

                m_owner->activateLeafAction_(action);
                event->accept();
                return;
            }
            SwWidget::mousePressEvent(event);
        }

        /**
         * @brief Handles keyboard navigation and activation within the popup.
         * @param event Key event targeting the popup.
         */
        void keyPressEvent(KeyEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            SwWidget::keyPressEvent(event);
            if (event->isAccepted()) {
                return;
            }

            const int key = event->key();
            if (SwWidgetPlatformAdapter::isEscapeKey(key)) {
                m_owner->hideAll();
                event->accept();
                return;
            }

            if (SwWidgetPlatformAdapter::isUpArrowKey(key)) {
                moveHover(-1);
                if (m_hoverIndex >= 0) {
                    m_owner->onPopupHoverChanged_(m_hoverIndex, rowRectForIndex_(m_hoverIndex));
                }
                event->accept();
                return;
            }
            if (SwWidgetPlatformAdapter::isDownArrowKey(key)) {
                moveHover(1);
                if (m_hoverIndex >= 0) {
                    m_owner->onPopupHoverChanged_(m_hoverIndex, rowRectForIndex_(m_hoverIndex));
                }
                event->accept();
                return;
            }
            if (SwWidgetPlatformAdapter::isReturnKey(key)) {
                SwAction* action = actionAt(m_hoverIndex);
                if (action && !action->getSeparator() && action->getEnabled()) {
                    if (action->hasMenu()) {
                        m_owner->onPopupHoverChanged_(m_hoverIndex, rowRectForIndex_(m_hoverIndex));
                    } else {
                        m_owner->activateLeafAction_(action);
                    }
                }
                event->accept();
                return;
            }
        }

    private:
        /**
         * @brief Returns the action-row index located at the given popup coordinates.
         * @param px Horizontal coordinate in popup space.
         * @param py Vertical coordinate in popup space.
         * @return Zero-based row index, or `-1` when the point is outside actionable rows.
         */
        int indexAt(int px, int py) const {
            const SwRect bounds = rect();
            const int y0 = bounds.y + m_padding;
            const int x0 = bounds.x + m_padding;
            if (py < y0 || px < x0 || px > (x0 + std::max(0, bounds.width - 2 * m_padding))) {
                return -1;
            }
            const int idx = (py - y0) / std::max(1, m_itemHeight);
            if (!m_owner || idx < 0 || idx >= m_owner->m_actions.size()) {
                return -1;
            }
            return idx;
        }

        /**
         * @brief Returns the action associated with a row index.
         * @param idx Zero-based row index.
         * @return Matching action, or `nullptr` when the index is invalid.
         */
        SwAction* actionAt(int idx) const {
            if (!m_owner || idx < 0 || idx >= m_owner->m_actions.size()) {
                return nullptr;
            }
            return m_owner->m_actions[idx];
        }

        /**
         * @brief Moves keyboard / hover selection while skipping separator rows.
         * @param delta Positive to move downward, negative to move upward.
         */
        void moveHover(int delta) {
            if (!m_owner || m_owner->m_actions.size() == 0) {
                return;
            }
            int idx = m_hoverIndex;
            if (idx < 0 || idx >= m_owner->m_actions.size()) {
                idx = (delta >= 0) ? -1 : m_owner->m_actions.size();
            }
            for (int step = 0; step < m_owner->m_actions.size(); ++step) {
                idx += delta;
                if (idx < 0) idx = m_owner->m_actions.size() - 1;
                if (idx >= m_owner->m_actions.size()) idx = 0;
                SwAction* a = m_owner->m_actions[idx];
                if (a && !a->getSeparator()) {
                    m_hoverIndex = idx;
                    update();
                    break;
                }
            }
        }

        /**
         * @brief Returns the popup-space rectangle occupied by one row.
         * @param idx Zero-based row index.
         * @return Rectangle covering the requested row.
         */
        SwRect rowRectForIndex_(int idx) const {
            const SwRect bounds = rect();
            const int startY = bounds.y + m_padding;
            const int startX = bounds.x + m_padding;
            const int contentW = std::max(0, bounds.width - 2 * m_padding);
            return SwRect{startX, startY + idx * m_itemHeight, contentW, m_itemHeight};
        }

        SwMenu* m_owner{nullptr};
        int m_hoverIndex{-1};
        int m_itemHeight{28};
        int m_padding{4};
        int m_minWidth{220};
        int m_textLeftPad{28};
        int m_textRightPad{12};
        int m_cachedWidth{220};
        int m_cachedHeight{0};
    };

    /**
     * @brief Clamps an integer value into an inclusive range.
     */
    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    /**
     * @brief Walks up the object hierarchy and returns the last widget ancestor found.
     * @param start Object from which the search begins.
     * @return Top-most widget ancestor, or `nullptr` when none exists.
     */
    static SwWidget* findRootWidget(SwObject* start) {
        SwWidget* lastWidget = nullptr;
        for (SwObject* p = start; p; p = p->parent()) {
            if (auto* w = dynamic_cast<SwWidget*>(p)) {
                lastWidget = w;
            }
        }
        return lastWidget;
    }

    /**
     * @brief Returns whether a point lies inside a rectangle.
     */
    static bool containsPoint_(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    /**
     * @brief Expands a rectangle uniformly in all directions.
     * @param rect Source rectangle.
     * @param margin Margin applied on each side.
     * @return Inflated rectangle clamped to non-negative width and height.
     */
    static SwRect inflate_(SwRect rect, int margin) {
        rect.x -= margin;
        rect.y -= margin;
        rect.width = std::max(0, rect.width + margin * 2);
        rect.height = std::max(0, rect.height + margin * 2);
        return rect;
    }

    /**
     * @brief Returns the parent menu when this menu is nested inside another menu.
     */
    SwMenu* parentMenu_() const { return dynamic_cast<SwMenu*>(parent()); }

    /**
     * @brief Returns the root menu at the top of the current submenu chain.
     */
    SwMenu* rootMenu_() const {
        SwMenu* m = const_cast<SwMenu*>(this);
        while (m) {
            SwMenu* p = dynamic_cast<SwMenu*>(m->parent());
            if (!p) {
                break;
            }
            m = p;
        }
        return m;
    }

    /**
     * @brief Returns whether the pointer lies inside the configured hover keep-alive rectangle.
     */
    bool isPointInsideHoverKeepAlive_(int px, int py) const {
        if (!m_hoverCloseEnabled) {
            return false;
        }
        return containsPoint_(m_hoverKeepAliveRect, px, py);
    }

    /**
     * @brief Recursively tests whether a point lies inside this popup or any open submenu popup.
     */
    bool isPointInsideAnyPopupRecursive_(int px, int py) const {
        if (m_popup && m_popup->getVisible()) {
            if (containsPoint_(m_popup->frameGeometry(), px, py)) {
                return true;
            }
        }
        if (m_openSubMenu) {
            return m_openSubMenu->isPointInsideAnyPopupRecursive_(px, py);
        }
        return false;
    }

    /**
     * @brief Tests the full root menu tree for popup containment.
     */
    bool isPointInsideAnyPopup_(int px, int py) const {
        const SwMenu* root = rootMenu_();
        return root ? root->isPointInsideAnyPopupRecursive_(px, py) : false;
    }

    /**
     * @brief Tests the bridge zone between a popup and its currently open submenu chain.
     *
     * @details
     * This helper prevents immediate submenu dismissal while the pointer is traveling from the
     * parent row toward the submenu popup.
     */
    bool isPointInsideOpenSubMenuBridge_(int px, int py) const {
        if (!m_popup || !m_openSubMenu) {
            return false;
        }

        SwRect bounds = m_popup->frameGeometry();
        int minX = bounds.x;
        int minY = bounds.y;
        int maxX = bounds.x + bounds.width;
        int maxY = bounds.y + bounds.height;

        const SwMenu* cur = m_openSubMenu;
        while (cur && cur->m_popup && cur->isVisible()) {
            const SwRect r = cur->m_popup->frameGeometry();
            minX = std::min(minX, r.x);
            minY = std::min(minY, r.y);
            maxX = std::max(maxX, r.x + r.width);
            maxY = std::max(maxY, r.y + r.height);
            cur = cur->m_openSubMenu;
        }

        bounds = SwRect{minX, minY, std::max(0, maxX - minX), std::max(0, maxY - minY)};
        return containsPoint_(inflate_(bounds, 8), px, py);
    }

    /**
     * @brief Closes the currently open submenu, if any.
     */
    void closeSubMenu_() {
        if (!m_openSubMenu) {
            return;
        }
        m_openSubMenu->hide();
        m_openSubMenu = nullptr;
        m_openSubMenuIndex = -1;
    }

    /**
     * @brief Opens or repositions the submenu associated with an action row.
     * @param action Action owning the submenu.
     * @param row Rectangle occupied by the source row.
     * @param index Index of the hovered or activated row.
     */
    void openSubMenu_(SwAction* action, const SwRect& row, int index) {
        if (!action || !action->hasMenu()) {
            closeSubMenu_();
            return;
        }

        SwMenu* sub = action->menu();
        if (!sub) {
            closeSubMenu_();
            return;
        }

        if (m_openSubMenu != sub) {
            closeSubMenu_();
            m_openSubMenu = sub;
            m_openSubMenuIndex = index;
        }

        sub->ensurePopup();
        if (!sub->m_popup || !m_popup) {
            return;
        }
        sub->m_popup->updateGeometryFromActions();
        const SwSize size = sub->m_popup->sizeHint();
        const SwRect parentPopup = m_popup->frameGeometry();

        int px = parentPopup.x + parentPopup.width + 4;
        const int leftCandidate = parentPopup.x - size.width - 4;
        if (m_root) {
            const int maxX = std::max(0, m_root->width() - size.width - 2);
            if (px > maxX && leftCandidate >= 2) {
                px = leftCandidate;
            }
        }

        sub->popup(px, row.y);
    }

    /**
     * @brief Reacts to hover changes and decides whether a submenu should remain open.
     * @param idx Hovered row index.
     * @param row Rectangle occupied by the hovered row.
     */
    void onPopupHoverChanged_(int idx, const SwRect& row) {
        if (idx < 0 || idx >= m_actions.size()) {
            closeSubMenu_();
            return;
        }
        SwAction* action = m_actions[idx];
        if (!action || action->getSeparator() || !action->getEnabled()) {
            closeSubMenu_();
            return;
        }
        if (action->hasMenu()) {
            openSubMenu_(action, row, idx);
        } else {
            closeSubMenu_();
        }
    }

    /**
     * @brief Closes the full menu chain before triggering a leaf action.
     *
     * @details
     * This matches Qt behavior and prevents menus from staying visible while the triggered slot
     * opens a modal dialog or another popup. Lifetime guards cover slots that destroy the action
     * or the owning menu during activation.
     */
    void activateLeafAction_(SwAction* action) {
        if (!action || action->getSeparator() || !action->getEnabled() || action->hasMenu()) {
            return;
        }

        const SwPointer<SwMenu> liveMenu(this);
        const SwPointer<SwAction> liveAction(action);

        hideAll();

        if (!liveAction) {
            return;
        }

        liveAction->trigger();

        if (!liveMenu || !liveAction) {
            return;
        }

        liveMenu->triggered(liveAction.data());
    }

    /**
     * @brief Lazily creates the timer used to close menu trees after hover leaves the safe region.
     */
    void ensureHoverCloseTimer_() {
        if (m_hoverCloseTimer) {
            return;
        }
        m_hoverCloseTimer = new SwTimer(180, this);
        m_hoverCloseTimer->setSingleShot(true);
        SwObject::connect(m_hoverCloseTimer, &SwTimer::timeout, this, [this]() {
            if (!m_hoverCloseEnabled) {
                return;
            }
            if (isPointInsideAnyPopup_(m_lastMouseX, m_lastMouseY) || isPointInsideHoverKeepAlive_(m_lastMouseX, m_lastMouseY)) {
                return;
            }
            hideAll();
        });
    }

    /**
     * @brief Starts the delayed hover-close timer when hover-close mode is enabled.
     */
    void startHoverCloseTimer_() {
        if (!m_hoverCloseEnabled) {
            return;
        }
        ensureHoverCloseTimer_();
        if (m_hoverCloseTimer && !m_hoverCloseTimer->isActive()) {
            m_hoverCloseTimer->start();
        }
    }

    /**
     * @brief Stops the delayed hover-close timer if it is currently running.
     */
    void stopHoverCloseTimer_() {
        if (m_hoverCloseTimer && m_hoverCloseTimer->isActive()) {
            m_hoverCloseTimer->stop();
        }
    }

    /**
     * @brief Updates the last known pointer position and hover-close timer state.
     * @param px Horizontal pointer position in root-widget coordinates.
     * @param py Vertical pointer position in root-widget coordinates.
     */
    void onOverlayMouseMove_(int px, int py) {
        m_lastMouseX = px;
        m_lastMouseY = py;

        if (!m_hoverCloseEnabled) {
            return;
        }

        if (isPointInsideAnyPopup_(px, py) || isPointInsideHoverKeepAlive_(px, py)) {
            stopHoverCloseTimer_();
            return;
        }
        startHoverCloseTimer_();
    }

    /**
     * @brief Ensures that the popup and overlay widgets required by this menu exist.
     *
     * @details
     * Root menus own the shared overlay. Submenus reuse that overlay and only create their own
     * popup surface, keeping the whole menu tree within a single transient widget hierarchy.
     */
    void ensurePopup() {
        if (m_popup && m_overlay && m_root) {
            return;
        }

        SwMenu* rootMenu = rootMenu_();
        if (rootMenu && rootMenu != this) {
            rootMenu->ensurePopup();
            m_root = rootMenu->m_root;
            m_overlay = rootMenu->m_overlay;
            m_ownsOverlay = false;
            if (!m_popup && m_overlay) {
                m_popup = new PopupList(this, m_overlay);
            }
            return;
        }

        m_root = findRootWidget(this);
        if (!m_root) {
            return;
        }

        if (!m_overlay) {
            m_overlay = new PopupOverlay(this, m_root, m_root);
            m_overlay->move(0, 0);
            m_overlay->resize(m_root->width(), m_root->height());
            m_ownsOverlay = true;
        }
        if (!m_popup) {
            m_popup = new PopupList(this, m_overlay);
        }

        if (!m_rootResizeHooked) {
            m_rootResizeHooked = true;
            SwObject::connect(m_root, &SwWidget::resized, this, [this](int w, int h) {
                if (m_overlay) {
                    m_overlay->move(0, 0);
                    m_overlay->resize(w, h);
                }
            });
        }
    }

    SwVector<SwAction*> m_actions;
    SwWidget* m_root{nullptr};
    PopupOverlay* m_overlay{nullptr};
    PopupList* m_popup{nullptr};

    bool m_ownsOverlay{true};
    bool m_rootResizeHooked{false};

    SwMenu* m_openSubMenu{nullptr};
    int m_openSubMenuIndex{-1};

    bool m_hoverCloseEnabled{false};
    SwRect m_hoverKeepAliveRect;
    SwTimer* m_hoverCloseTimer{nullptr};
    int m_lastMouseX{0};
    int m_lastMouseY{0};
};

