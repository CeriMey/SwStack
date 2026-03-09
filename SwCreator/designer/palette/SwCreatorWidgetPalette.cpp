#include "SwCreatorWidgetPalette.h"

#include "SwCreatorPalettePage.h"

#include "SwLineEdit.h"
#include "SwScrollArea.h"
#include "SwToolBox.h"

#include <algorithm>

namespace {
SwCreatorPaletteEntry makeWidget(const char* category, const char* className) {
    SwCreatorPaletteEntry e;
    e.category = SwString(category);
    e.className = SwString(className);
    e.displayName = SwString(className);
    e.isLayout = false;
    return e;
}

SwCreatorPaletteEntry makeLayout(const char* category, const char* layoutName) {
    SwCreatorPaletteEntry e;
    e.category = SwString(category);
    e.className = SwString(layoutName);
    e.displayName = SwString(layoutName);
    e.isLayout = true;
    return e;
}
} // namespace

SwCreatorWidgetPalette::SwCreatorWidgetPalette(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwCreatorWidgetPalette { background-color: rgb(255, 255, 255); border-width: 0px; border-radius: 0px; }");
    buildUi_();
    buildEntries_();
    buildPages_();
    applyFilter_(SwString());
}

void SwCreatorWidgetPalette::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void SwCreatorWidgetPalette::buildUi_() {
    m_search = new SwLineEdit(SwString("Search..."), this);
    m_search->setStyleSheet(R"(
            SwLineEdit {
                background-color: rgb(244, 246, 248);
                border-color: rgb(224, 228, 232);
                border-width: 1px;
                border-radius: 8px;
                padding: 4px 10px;
                color: rgb(15, 23, 42);
            }
        )");

    m_toolBoxScroll = new SwScrollArea(this);
    m_toolBoxScroll->setWidgetResizable(true);
    m_toolBoxScroll->setStyleSheet(R"(
            SwScrollArea {
                background-color: rgb(255, 255, 255);
                border-width: 0px;
                border-radius: 0px;
            }
        )");

    m_toolBox = new SwToolBox(nullptr);
    m_toolBox->setContentBasedLayout(true);
    m_toolBox->setExclusive(false);
    m_toolBox->setContentsMargin(0);
    m_toolBox->setSpacing(0);
    m_toolBox->setHeaderHeight(26);
    m_toolBox->setStyleSheet(R"(
            SwToolBox {
                background-color: rgb(255, 255, 255);
                border-width: 0px;
                border-radius: 0px;
            }
        )");
    m_toolBox->setHeaderStyleSheet(R"(
            SwToolButton {
                background-color: rgb(241, 244, 247);
                background-color-hover: rgb(236, 240, 244);
                background-color-pressed: rgb(229, 233, 238);
                background-color-checked: rgb(236, 240, 244);
                border-color: rgb(226, 230, 234);
                border-width: 1px;
                border-radius: 0px;
                color: rgb(15, 23, 42);
                color-disabled: rgb(148, 163, 184);
                indicator-color: rgb(71, 85, 105);
                indicator-color-disabled: rgb(148, 163, 184);
                padding: 3px 10px;
            }
        )");

    m_toolBoxScroll->setWidget(m_toolBox);

    SwObject::connect(m_toolBox, &SwToolBox::contentSizeChanged, this, [this](int) {
        if (m_toolBoxScroll) {
            m_toolBoxScroll->refreshLayout();
        }
    });
    SwObject::connect(m_search, &SwLineEdit::TextChanged, this, [this](const SwString& text) { applyFilter_(text); });
}

void SwCreatorWidgetPalette::buildEntries_() {
    m_entries.clear();
    m_entries.reserve(64);

    // Basic
    m_entries.push_back(makeWidget("Basic", "SwPushButton"));
    m_entries.push_back(makeWidget("Basic", "SwToolButton"));
    m_entries.push_back(makeWidget("Basic", "SwLabel"));
    m_entries.push_back(makeWidget("Basic", "SwLineEdit"));
    m_entries.push_back(makeWidget("Basic", "SwCheckBox"));
    m_entries.push_back(makeWidget("Basic", "SwRadioButton"));
    m_entries.push_back(makeWidget("Basic", "SwProgressBar"));

    // Input
    m_entries.push_back(makeWidget("Input", "SwComboBox"));
    m_entries.push_back(makeWidget("Input", "SwSpinBox"));
    m_entries.push_back(makeWidget("Input", "SwDoubleSpinBox"));
    m_entries.push_back(makeWidget("Input", "SwSlider"));
    m_entries.push_back(makeWidget("Input", "SwPlainTextEdit"));
    m_entries.push_back(makeWidget("Input", "SwTextEdit"));

    // Containers
    m_entries.push_back(makeWidget("Containers", "SwFrame"));
    m_entries.push_back(makeWidget("Containers", "SwGroupBox"));
    m_entries.push_back(makeWidget("Containers", "SwScrollArea"));
    m_entries.push_back(makeWidget("Containers", "SwTabWidget"));
    m_entries.push_back(makeWidget("Containers", "SwStackedWidget"));
    m_entries.push_back(makeWidget("Containers", "SwSplitter"));

    // Views
    m_entries.push_back(makeWidget("Views", "SwTableWidget"));
    m_entries.push_back(makeWidget("Views", "SwTreeWidget"));
    m_entries.push_back(makeWidget("Views", "SwTableView"));
    m_entries.push_back(makeWidget("Views", "SwTreeView"));

    // Layouts (not widgets): represented as palette entries.
    m_entries.push_back(makeWidget("Layouts", "SwSpacer"));
    m_entries.push_back(makeLayout("Layouts", "SwVerticalLayout"));
    m_entries.push_back(makeLayout("Layouts", "SwHorizontalLayout"));
    m_entries.push_back(makeLayout("Layouts", "SwGridLayout"));
    m_entries.push_back(makeLayout("Layouts", "SwFormLayout"));
}

void SwCreatorWidgetPalette::buildPages_() {
    m_pages.clear();

    if (!m_toolBox) {
        return;
    }

    auto ensureCategory = [&](const SwString& category) {
        if (m_pages.find(category) != m_pages.end()) {
            return;
        }

        auto* page = new SwCreatorPalettePage(nullptr);
        page->move(0, 0);
        page->resize(100, 100);

        SwObject::connect(page, &SwCreatorPalettePage::entryActivated, this, [this](const SwCreatorPaletteEntry& e) {
            entryActivated(e);
        });
        SwObject::connect(page, &SwCreatorPalettePage::entryDragStarted, this, [this](const SwCreatorPaletteEntry& e) {
            entryDragStarted(e);
        });
        SwObject::connect(page,
                          &SwCreatorPalettePage::entryDragMoved,
                          this,
                          [this](const SwCreatorPaletteEntry& e, int x, int y) { entryDragMoved(e, x, y); });
        SwObject::connect(page,
                          &SwCreatorPalettePage::entryDropped,
                          this,
                          [this](const SwCreatorPaletteEntry& e, int x, int y) { entryDropped(e, x, y); });

        m_toolBox->addItem(page, category);
        m_pages[category] = page;
        page->refreshLayout();
    };

    // Preserve a stable order.
    const SwStringList categories = SwStringList{SwString("Basic"), SwString("Input"), SwString("Containers"), SwString("Views"), SwString("Layouts")};
    for (const SwString& cat : categories) {
        ensureCategory(cat);
    }

    for (const auto& cat : categories) {
        auto it = m_pages.find(cat);
        if (it == m_pages.end()) {
            continue;
        }
        std::vector<SwCreatorPaletteEntry> list;
        for (const auto& e : m_entries) {
            if (e.category == cat) {
                list.push_back(e);
            }
        }
        it->second->setEntries(list);
    }

    // The toolbox queried sizeHint() when pages were added (still empty).
    // Now that entries are populated, force the toolbox to re-query page sizes.
    if (m_toolBox) {
        m_toolBox->refreshLayout();
    }

    updateLayout_();
}

void SwCreatorWidgetPalette::updateLayout_() {
    const SwRect r = rect();
    const int pad = 0;
    const int gap = 6;
    const int searchH = 34;

    if (m_search) {
        m_search->move(pad, pad);
        m_search->resize(std::max(0, r.width - 2 * pad), searchH);
    }
    if (m_toolBox) {
        const int y = pad + searchH + gap;
        const int w = std::max(0, r.width - 2 * pad);
        const int h = std::max(0, r.height - y - pad);

        if (m_toolBoxScroll) {
            m_toolBoxScroll->move(pad, y);
            m_toolBoxScroll->resize(w, h);
            m_toolBoxScroll->refreshLayout();
        } else {
            m_toolBox->move(pad, y);
            m_toolBox->resize(w, h);
        }
    }
}

void SwCreatorWidgetPalette::applyFilter_(const SwString& text) {
    for (auto& it : m_pages) {
        if (it.second) {
            it.second->setFilterText(text);
        }
    }
    if (m_toolBox) {
        m_toolBox->refreshLayout();
    }
}
