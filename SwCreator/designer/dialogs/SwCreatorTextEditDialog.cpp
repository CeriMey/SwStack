#include "SwCreatorTextEditDialog.h"

#include "SwLayout.h"
#include "SwPlainTextEdit.h"
#include "SwPushButton.h"
#include "theme/SwCreatorTheme.h"

#include <algorithm>

SwCreatorTextEditDialog::SwCreatorTextEditDialog(SwWidget* parent)
    : SwCreatorDockDialog(parent) {
    setWindowTitle("Edit text");
    setMinimumSize(560, 360);
    resize(760, 520);
    buildUi_();
}

void SwCreatorTextEditDialog::setText(const SwString& text) {
    if (m_edit) {
        m_edit->setPlainText(text);
    }
}

SwString SwCreatorTextEditDialog::text() const {
    return m_edit ? m_edit->toPlainText() : SwString();
}

void SwCreatorTextEditDialog::setPlaceholderText(const SwString& text) {
    if (m_edit) {
        m_edit->setPlaceholderText(text);
    }
}

void SwCreatorTextEditDialog::setOnApply(const std::function<void(const SwString&)>& handler) {
    m_onApply = handler;
}

void SwCreatorTextEditDialog::buildUi_() {
    if (m_edit) {
        return;
    }

    const auto& th = SwCreatorTheme::current();

    if (auto* content = contentWidget()) {
        auto* layout = new SwVerticalLayout(content);
        layout->setMargin(0);
        layout->setSpacing(10);
        content->setLayout(layout);

        m_edit = new SwPlainTextEdit(content);
        m_edit->setPlaceholderText("...");
        m_edit->setStyleSheet(
            "SwPlainTextEdit {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " border-width: 1px;"
            " border-radius: 12px;"
            " padding: 10px 12px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " font-size: 13px;"
            " }"
        );
        layout->addWidget(m_edit, 1, 0);

        auto* spacer = new SwWidget(content);
        spacer->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        layout->addWidget(spacer, 0, 0);
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
        m_apply->setStyleSheet(
            "SwPushButton {"
            " background-color: " + SwCreatorTheme::rgb(th.accentSecondary) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.accentSecondary) + ";"
            " color: " + SwCreatorTheme::rgb(th.textInverse) + ";"
            " border-radius: 10px;"
            " padding: 8px 14px;"
            " border-width: 1px;"
            " font-size: 14px;"
            " }"
            " SwPushButton:hover {"
            " background-color: " + SwCreatorTheme::rgb(th.accentSecondaryHover) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.accentSecondaryHover) + ";"
            " }"
            " SwPushButton:pressed {"
            " background-color: " + SwCreatorTheme::rgb(th.accentSecondaryPressed) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.accentSecondaryPressed) + ";"
            " }"
        );
        SwObject::connect(m_apply, &SwPushButton::clicked, this, [this]() {
            const SwString t = text();
            if (m_onApply) {
                m_onApply(t);
            }
            applied(t);
        });
        barLayout->addWidget(m_apply, 0, m_apply->width());

        m_close = new SwPushButton("Close", bar);
        m_close->resize(120, 36);
        SwObject::connect(m_close, &SwPushButton::clicked, this, [this]() {
            if (isDockedOpen()) {
                closeDocked();
                return;
            }
            reject();
        });
        barLayout->addWidget(m_close, 0, m_close->width());
    }
}
