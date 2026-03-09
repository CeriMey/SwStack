#pragma once

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

#include "SwListView.h"
#include "SwSortFilterProxyModel.h"
#include "SwStandardItemModel.h"
#include "SwWidgetPlatformAdapter.h"

class SwCompleter : public SwObject {
    SW_OBJECT(SwCompleter, SwObject)

public:
    enum CompletionMode {
        PopupCompletion,
        InlineCompletion,
        UnfilteredPopupCompletion
    };

    enum ModelSorting {
        UnsortedModel,
        CaseSensitivelySortedModel,
        CaseInsensitivelySortedModel
    };

    explicit SwCompleter(SwObject* parent = nullptr)
        : SwObject(parent)
        , m_proxyModel(new SwSortFilterProxyModel(this))
        , m_ownedModel(new SwStandardItemModel(0, 1, this)) {
        m_proxyModel->setFilterCaseSensitivity(m_caseSensitivity);
        m_proxyModel->setFilterRole(m_completionRole);
        m_proxyModel->setFilterKeyColumn(m_completionColumn);
        setModel(m_ownedModel);
    }

    void setWidget(SwWidget* widget) {
        if (m_widget == widget) {
            return;
        }
        m_widget = widget;
        if (m_popup) {
            ensurePopupParent_();
        }
    }

    SwWidget* widget() const { return m_widget; }

    void setModel(SwAbstractItemModel* model) {
        if (m_sourceModel == model) {
            return;
        }
        m_sourceModel = model;
        m_proxyModel->setSourceModel(m_sourceModel);
        if (m_popup) {
            refreshPopupModel_();
        }
    }

    SwAbstractItemModel* model() const { return m_sourceModel; }

    void setStringList(const SwList<SwString>& values) {
        m_ownedModel->clear();
        for (int i = 0; i < values.size(); ++i) {
            m_ownedModel->appendRow(new SwStandardItem(values[i]));
        }
        if (m_sourceModel == m_ownedModel) {
            m_proxyModel->invalidate();
        }
    }

    void setPopup(SwListView* popup) {
        if (m_popup == popup) {
            return;
        }
        if (m_popup && m_popupOwnedByCompleter) {
            delete m_popup;
        }
        m_popup = popup;
        m_popupOwnedByCompleter = false;
        if (!m_popup) {
            hidePopup_();
            return;
        }
        ensurePopupParent_();
        configurePopup_();
    }

    SwListView* popup() {
        ensurePopup_();
        return m_popup;
    }

    const SwListView* popup() const {
        return m_popup;
    }

    void setCompletionPrefix(const SwString& prefix) {
        if (m_completionPrefix == prefix) {
            return;
        }
        m_completionPrefix = prefix;
        updateFiltering_();
    }

    SwString completionPrefix() const { return m_completionPrefix; }

    void setCompletionMode(CompletionMode mode) { m_completionMode = mode; }
    CompletionMode completionMode() const { return m_completionMode; }

    void setCaseSensitivity(Sw::CaseSensitivity cs) {
        if (m_caseSensitivity == cs) {
            return;
        }
        m_caseSensitivity = cs;
        m_proxyModel->setFilterCaseSensitivity(cs);
    }

    Sw::CaseSensitivity caseSensitivity() const { return m_caseSensitivity; }

    void setCompletionRole(SwItemDataRole role) {
        if (m_completionRole == role) {
            return;
        }
        m_completionRole = role;
        m_proxyModel->setFilterRole(role);
    }

    SwItemDataRole completionRole() const { return m_completionRole; }

    void setCompletionColumn(int column) {
        if (m_completionColumn == column) {
            return;
        }
        m_completionColumn = column;
        m_proxyModel->setFilterKeyColumn(column);
    }

    int completionColumn() const { return m_completionColumn; }

    void setMaxVisibleItems(int maxItems) {
        m_maxVisibleItems = std::max(1, maxItems);
    }

    int maxVisibleItems() const { return m_maxVisibleItems; }

    void setWrapAround(bool on) { m_wrapAround = on; }
    bool wrapAround() const { return m_wrapAround; }

    void setModelSorting(ModelSorting sorting) { m_modelSorting = sorting; }
    ModelSorting modelSorting() const { return m_modelSorting; }

    int completionCount() const {
        return activeModel_() ? activeModel_()->rowCount() : 0;
    }

    SwModelIndex currentIndex() const {
        if (!m_popup || !m_popup->selectionModel()) {
            return SwModelIndex();
        }
        return m_popup->selectionModel()->currentIndex();
    }

    SwString currentCompletion() const {
        const SwModelIndex index = currentIndex();
        if (!index.isValid()) {
            return SwString();
        }
        return activeModel_()->data(index, m_completionRole).toString();
    }

    bool popupVisible() const {
        return m_popup && m_popup->getVisible();
    }

    void complete(const SwRect& rect = SwRect{0, 0, 0, 0}) {
        if (!m_widget) {
            return;
        }

        ensurePopup_();
        refreshPopupModel_();
        updateFiltering_();

        if (completionCount() <= 0) {
            hidePopup_();
            return;
        }

        ensurePopupParent_();

        SwRect anchorRect = rect;
        if (anchorRect.width <= 0) {
            anchorRect = SwRect{0, 0, m_widget->width(), m_widget->height()};
        }

        const SwPoint globalOrigin = m_widget->mapToGlobal(SwPoint{anchorRect.x, anchorRect.y + anchorRect.height});
        SwWidget* root = topLevelWidget_(m_widget);
        if (!root) {
            return;
        }
        const SwPoint popupPos = root->mapFromGlobal(globalOrigin);

        const int rowCount = std::min(completionCount(), m_maxVisibleItems);
        const int rowHeight = m_popup ? std::max(18, m_popup->rowHeight()) : 26;
        const int width = std::max(168, anchorRect.width + 18);
        const int height = std::max(rowHeight + 6, rowCount * rowHeight + 6);

        m_popup->move(popupPos.x, popupPos.y);
        m_popup->resize(width, height);
        m_popup->setVisible(true);
        m_popup->update();

        if (m_popup->selectionModel() && completionCount() > 0) {
            const SwModelIndex first = activeModel_()->index(0, 0, SwModelIndex());
            m_popup->selectionModel()->setCurrentIndex(first, true, true);
            highlighted(currentCompletion());
        }
    }

    void hidePopup() {
        hidePopup_();
    }

    bool handleEditorKeyPress(KeyEvent* event) {
        if (!event || !popupVisible() || !m_popup || !m_popup->selectionModel()) {
            return false;
        }

        const SwAbstractItemModel* model = activeModel_();
        if (!model || model->rowCount() <= 0) {
            hidePopup_();
            return false;
        }

        SwModelIndex current = m_popup->selectionModel()->currentIndex();
        int row = current.isValid() ? current.row() : 0;

        if (SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
            hidePopup_();
            event->accept();
            return true;
        }

        if (SwWidgetPlatformAdapter::isReturnKey(event->key())) {
            activateCurrent_();
            event->accept();
            return true;
        }

        if (isTabKey_(event->key())) {
            activateCurrent_();
            event->accept();
            return true;
        }

        if (SwWidgetPlatformAdapter::isDownArrowKey(event->key())) {
            row = nextRow_(row, 1, model->rowCount());
            const SwModelIndex next = model->index(row, 0, SwModelIndex());
            m_popup->selectionModel()->setCurrentIndex(next, true, true);
            highlighted(currentCompletion());
            event->accept();
            return true;
        }

        if (SwWidgetPlatformAdapter::isUpArrowKey(event->key())) {
            row = nextRow_(row, -1, model->rowCount());
            const SwModelIndex prev = model->index(row, 0, SwModelIndex());
            m_popup->selectionModel()->setCurrentIndex(prev, true, true);
            highlighted(currentCompletion());
            event->accept();
            return true;
        }

        return false;
    }

    DECLARE_SIGNAL(activated, const SwString&)
    DECLARE_SIGNAL(highlighted, const SwString&)

private:
    SwAbstractItemModel* activeModel_() const {
        if (m_completionMode == UnfilteredPopupCompletion) {
            return m_sourceModel;
        }
        return m_proxyModel;
    }

    void ensurePopup_() {
        if (m_popup) {
            return;
        }
        SwWidget* root = topLevelWidget_(m_widget);
        m_popup = new SwListView(root);
        m_popupOwnedByCompleter = true;
        configurePopup_();
    }

    void ensurePopupParent_() {
        if (!m_popup || !m_widget) {
            return;
        }
        SwWidget* root = topLevelWidget_(m_widget);
        if (!root) {
            return;
        }
        if (m_popup->parent() != root) {
            m_popup->setParent(root);
        }
    }

    void configurePopup_() {
        if (!m_popup) {
            return;
        }
        m_popup->setVisible(false);
        m_popup->setStyleSheet(R"(
            SwListView {
                background-color: rgb(255, 255, 255);
                border-color: rgb(210, 214, 224);
                border-width: 1px;
                border-radius: 8px;
                color: rgb(24, 28, 36);
            }
        )");
        refreshPopupModel_();
        SwObject::connect(m_popup, &SwListView::activated, this, [this](const SwModelIndex&) {
            activateCurrent_();
        });
        if (m_popup->selectionModel()) {
            SwObject::connect(m_popup->selectionModel(), &SwItemSelectionModel::currentChanged, this,
                              [this](const SwModelIndex&, const SwModelIndex&) {
                                  if (popupVisible()) {
                                      highlighted(currentCompletion());
                                  }
                              });
        }
    }

    void refreshPopupModel_() {
        if (!m_popup) {
            return;
        }
        m_popup->setModel(activeModel_());
    }

    void updateFiltering_() {
        if (m_completionMode == UnfilteredPopupCompletion) {
            return;
        }
        if (m_completionPrefix.isEmpty()) {
            m_proxyModel->setFilterFixedString(SwString());
            return;
        }

        SwString pattern("^");
        pattern += regexEscape_(m_completionPrefix);
        m_proxyModel->setFilterRegularExpression(SwRegularExpression(pattern));
    }

    void hidePopup_() {
        if (m_popup) {
            m_popup->setVisible(false);
        }
    }

    void activateCurrent_() {
        const SwString completion = currentCompletion();
        if (completion.isEmpty()) {
            hidePopup_();
            return;
        }
        hidePopup_();
        activated(completion);
    }

    int nextRow_(int currentRow, int delta, int rowCount) const {
        if (rowCount <= 0) {
            return 0;
        }
        int next = currentRow + delta;
        if (m_wrapAround) {
            if (next < 0) {
                next = rowCount - 1;
            } else if (next >= rowCount) {
                next = 0;
            }
        } else {
            next = std::max(0, std::min(next, rowCount - 1));
        }
        return next;
    }

    static bool isTabKey_(int keyCode) {
        return keyCode == 9;
    }

    static SwString regexEscape_(const SwString& text) {
        SwString escaped;
        escaped.reserve(text.size() * 2);
        for (size_t i = 0; i < text.size(); ++i) {
            switch (text[i]) {
                case '\\':
                case '.':
                case '^':
                case '$':
                case '|':
                case '(':
                case ')':
                case '[':
                case ']':
                case '{':
                case '}':
                case '*':
                case '+':
                case '?':
                    escaped.append('\\');
                    break;
                default:
                    break;
            }
            escaped.append(text[i]);
        }
        return escaped;
    }

    static SwWidget* topLevelWidget_(SwWidget* widget) {
        SwWidget* current = widget;
        while (current) {
            SwWidget* parentWidget = dynamic_cast<SwWidget*>(current->parent());
            if (!parentWidget) {
                break;
            }
            current = parentWidget;
        }
        return current;
    }

    SwWidget* m_widget{nullptr};
    SwAbstractItemModel* m_sourceModel{nullptr};
    SwSortFilterProxyModel* m_proxyModel{nullptr};
    SwStandardItemModel* m_ownedModel{nullptr};
    SwListView* m_popup{nullptr};
    SwString m_completionPrefix;
    CompletionMode m_completionMode{PopupCompletion};
    ModelSorting m_modelSorting{UnsortedModel};
    Sw::CaseSensitivity m_caseSensitivity{Sw::CaseInsensitive};
    SwItemDataRole m_completionRole{SwItemDataRole::DisplayRole};
    int m_completionColumn{0};
    int m_maxVisibleItems{8};
    bool m_wrapAround{true};
    bool m_popupOwnedByCompleter{false};
};
