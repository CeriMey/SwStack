#include "SwCreatorStringListDialog.h"

#include "SwLayout.h"
#include "SwLineEdit.h"
#include "SwListWidget.h"
#include "SwPushButton.h"
#include "SwToolButton.h"

#include <algorithm>

namespace {
SwString uniqueDefaultItemName(const SwVector<SwString>& items) {
    const SwString base = "Item";
    if (items.isEmpty()) {
        return base;
    }
    int i = items.size() + 1;
    while (true) {
        SwString candidate = SwString("%1 %2").arg(base).arg(SwString::number(i));
        bool exists = false;
        for (int k = 0; k < items.size(); ++k) {
            if (items[k] == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            return candidate;
        }
        ++i;
    }
}

SwString previewButtonCss() {
    return R"(
        SwToolButton {
            background-color: rgb(248, 250, 252);
            border-color: rgb(226, 232, 240);
            border-width: 1px;
            border-radius: 10px;
            padding: 0px;
        }
    )";
}
} // namespace

SwCreatorStringListDialog::SwCreatorStringListDialog(SwWidget* parent)
    : SwCreatorDockDialog(parent) {
    setWindowTitle("Edit items");
    setMinimumSize(520, 420);
    resize(640, 520);
    buildUi_();
}

void SwCreatorStringListDialog::setItems(const SwVector<SwString>& items) {
    m_items = items;
    rebuildList_(m_items.isEmpty() ? -1 : 0);
}

SwVector<SwString> SwCreatorStringListDialog::items() const {
    return m_items;
}

void SwCreatorStringListDialog::setOnApply(const std::function<void(const SwVector<SwString>&)>& handler) {
    m_onApply = handler;
}

void SwCreatorStringListDialog::buildUi_() {
    if (m_list) {
        return;
    }

    if (auto* content = contentWidget()) {
        auto* layout = new SwVerticalLayout(content);
        layout->setMargin(0);
        layout->setSpacing(10);
        content->setLayout(layout);

        auto* row = new SwWidget(content);
        row->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        auto* rowLayout = new SwHorizontalLayout(row);
        rowLayout->setMargin(0);
        rowLayout->setSpacing(8);
        row->setLayout(rowLayout);

        m_edit = new SwLineEdit(row);
        m_edit->setPlaceholder("Item text");
        m_edit->setStyleSheet(R"(
            SwLineEdit {
                background-color: rgb(248, 250, 252);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 10px;
                padding: 4px 8px;
                color: rgb(15, 23, 42);
            }
        )");
        SwObject::connect(m_edit, &SwLineEdit::FocusChanged, this, [this](bool focus) {
            if (focus) {
                return;
            }
            commitCurrentEdit_();
        });
        rowLayout->addWidget(m_edit, 1, 0);

        m_add = new SwToolButton("+", row);
        m_add->resize(34, 34);
        m_add->setStyleSheet(previewButtonCss());
        SwObject::connect(m_add, &SwToolButton::clicked, this, [this](bool) {
            commitCurrentEdit_();
            const int current = currentRow_();
            const int insertAt = (current >= 0) ? (current + 1) : m_items.size();
            SwString text = m_edit ? m_edit->getText().trimmed() : SwString();
            if (text.isEmpty()) {
                text = uniqueDefaultItemName(m_items);
            }
            insertItem_(insertAt, text);
            rebuildList_(insertAt);
        });
        rowLayout->addWidget(m_add, 0, m_add->width());

        m_remove = new SwToolButton("-", row);
        m_remove->resize(34, 34);
        m_remove->setStyleSheet(previewButtonCss());
        SwObject::connect(m_remove, &SwToolButton::clicked, this, [this](bool) {
            commitCurrentEdit_();
            const int row = currentRow_();
            if (row < 0 || row >= m_items.size()) {
                return;
            }
            m_items.removeAt(row);
            const int next = std::min(row, m_items.size() - 1);
            rebuildList_(next);
        });
        rowLayout->addWidget(m_remove, 0, m_remove->width());

        m_up = new SwToolButton("↑", row);
        m_up->resize(34, 34);
        m_up->setStyleSheet(previewButtonCss());
        SwObject::connect(m_up, &SwToolButton::clicked, this, [this](bool) {
            commitCurrentEdit_();
            const int row = currentRow_();
            if (row <= 0 || row >= m_items.size()) {
                return;
            }
            std::swap(m_items[row - 1], m_items[row]);
            rebuildList_(row - 1);
        });
        rowLayout->addWidget(m_up, 0, m_up->width());

        m_down = new SwToolButton("↓", row);
        m_down->resize(34, 34);
        m_down->setStyleSheet(previewButtonCss());
        SwObject::connect(m_down, &SwToolButton::clicked, this, [this](bool) {
            commitCurrentEdit_();
            const int row = currentRow_();
            if (row < 0 || row >= (m_items.size() - 1)) {
                return;
            }
            std::swap(m_items[row], m_items[row + 1]);
            rebuildList_(row + 1);
        });
        rowLayout->addWidget(m_down, 0, m_down->width());

        layout->addWidget(row, 0, 38);

        m_list = new SwListWidget(content);
        m_list->setStyleSheet(R"(
            SwListView {
                background-color: rgb(255, 255, 255);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 12px;
                padding: 6px;
            }
        )");
        layout->addWidget(m_list, 1, 0);

        if (m_list && m_list->selectionModel()) {
            SwObject::connect(m_list->selectionModel(),
                              &SwItemSelectionModel::currentChanged,
                              this,
                              [this](const SwModelIndex&, const SwModelIndex&) { syncEditorFromSelection_(); });
        }
    }

    if (auto* bar = buttonBarWidget()) {
        auto* barLayout = new SwHorizontalLayout(bar);
        barLayout->setMargin(0);
        barLayout->setSpacing(10);
        bar->setLayout(barLayout);

        auto* spacer = new SwWidget(bar);
        spacer->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        barLayout->addWidget(spacer, 1, 0);

        m_apply = new SwPushButton("Apply", bar);
        m_apply->resize(120, 36);
        SwObject::connect(m_apply, &SwPushButton::clicked, this, [this]() {
            commitCurrentEdit_();
            if (m_onApply) {
                m_onApply(m_items);
            }
            applied(m_items);
        });
        barLayout->addWidget(m_apply, 0, m_apply->width());

        m_close = new SwPushButton("Close", bar);
        m_close->resize(120, 36);
        SwObject::connect(m_close, &SwPushButton::clicked, this, [this]() { closeDocked(); });
        barLayout->addWidget(m_close, 0, m_close->width());
    }
}

void SwCreatorStringListDialog::rebuildList_(int selectRow) {
    if (!m_list) {
        return;
    }

    if (selectRow < -1) {
        selectRow = -1;
    }
    if (selectRow >= m_items.size()) {
        selectRow = m_items.size() - 1;
    }

    m_list->clear();
    for (int i = 0; i < m_items.size(); ++i) {
        m_list->addItem(m_items[i]);
    }

    setCurrentRow_(selectRow);
    syncEditorFromSelection_();
    if (m_list) {
        m_list->update();
    }
}

void SwCreatorStringListDialog::syncEditorFromSelection_() {
    if (!m_edit) {
        return;
    }

    const int row = currentRow_();
    if (row < 0 || row >= m_items.size()) {
        m_edit->setText("");
    } else {
        m_edit->setText(m_items[row]);
    }

    if (m_remove) {
        m_remove->setEnable(row >= 0 && row < m_items.size());
    }
    if (m_up) {
        m_up->setEnable(row > 0 && row < m_items.size());
    }
    if (m_down) {
        m_down->setEnable(row >= 0 && row < (m_items.size() - 1));
    }
}

void SwCreatorStringListDialog::commitCurrentEdit_() {
    if (!m_edit) {
        return;
    }

    const int row = currentRow_();
    if (row < 0 || row >= m_items.size()) {
        return;
    }

    const SwString newText = m_edit->getText();
    if (m_items[row] == newText) {
        return;
    }
    m_items[row] = newText;
    rebuildList_(row);
}

int SwCreatorStringListDialog::currentRow_() const {
    if (!m_list || !m_list->selectionModel() || !m_list->model()) {
        return -1;
    }
    const SwModelIndex idx = m_list->selectionModel()->currentIndex();
    if (!idx.isValid()) {
        return -1;
    }
    return idx.row();
}

void SwCreatorStringListDialog::setCurrentRow_(int row) {
    if (!m_list || !m_list->selectionModel() || !m_list->model()) {
        return;
    }
    if (row < 0) {
        m_list->selectionModel()->clear();
        return;
    }
    SwModelIndex idx = m_list->model()->index(row, 0, SwModelIndex());
    if (!idx.isValid()) {
        return;
    }
    m_list->selectionModel()->setCurrentIndex(idx);
}

void SwCreatorStringListDialog::insertItem_(int index, const SwString& text) {
    const int n = m_items.size();
    if (index < 0) {
        index = 0;
    }
    if (index > n) {
        index = n;
    }

    SwVector<SwString> copy;
    copy.reserve(static_cast<SwVector<SwString>::size_type>(n + 1));
    for (int i = 0; i < n + 1; ++i) {
        if (i == index) {
            copy.push_back(text);
        } else {
            const int src = (i < index) ? i : (i - 1);
            if (src >= 0 && src < n) {
                copy.push_back(m_items[src]);
            }
        }
    }
    m_items = copy;
}

