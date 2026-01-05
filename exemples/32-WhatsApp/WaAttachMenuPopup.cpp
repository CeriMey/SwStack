#include "WaAttachMenuPopup.h"

#include "SwButton.h"

#include <algorithm>

WaAttachMenuPopup::WaAttachMenuPopup(SwWidget* parent)
    : SwFrame(parent) {
    setFrameShape(SwFrame::Shape::Box);
    setStyleSheet(R"(
        WaAttachMenuPopup {
            background-color: rgb(255, 255, 255);
            border-color: rgb(220, 224, 232);
            border-width: 1px;
            border-radius: 14px;
        }
    )");
    setFocusPolicy(FocusPolicyEnum::NoFocus);
    buildUi_();
}

void WaAttachMenuPopup::resizeEvent(ResizeEvent* event) {
    SwFrame::resizeEvent(event);
    updateLayout_();
}

SwButton* WaAttachMenuPopup::makeItem_(const SwString& text, SwWidget* parent) {
    auto* b = new SwButton(text, parent);
    b->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
    b->setStyleSheet(R"(
        SwButton {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            border-radius: 10px;
            color: rgb(17, 27, 33);
            font-size: 14px;
            padding: 0px 12px;
        }
        SwButton:hover { background-color: rgb(245, 246, 246); }
        SwButton:pressed { background-color: rgb(233, 237, 239); }
    )");
    return b;
}

void WaAttachMenuPopup::buildUi_() {
    if (m_file) {
        return;
    }

    m_file = makeItem_("📄  Fichier", this);
    m_media = makeItem_("🖼️  Photos / vidéos", this);
    m_audio = makeItem_("🎵  Audio", this);
    m_contact = makeItem_("👤  Contact", this);
    m_location = makeItem_("📍  Localisation", this);

    SwObject::connect(m_file, &SwButton::clicked, [this]() { actionTriggered("file"); });
    SwObject::connect(m_media, &SwButton::clicked, [this]() { actionTriggered("media"); });
    SwObject::connect(m_audio, &SwButton::clicked, [this]() { actionTriggered("audio"); });
    SwObject::connect(m_contact, &SwButton::clicked, [this]() { actionTriggered("contact"); });
    SwObject::connect(m_location, &SwButton::clicked, [this]() { actionTriggered("location"); });

    updateLayout_();
}

void WaAttachMenuPopup::updateLayout_() {
    const SwRect r = getRect();

    const int innerW = std::max(0, r.width - 2 * m_pad);
    int y = r.y + m_pad;
    auto place = [&](SwButton* b) {
        if (!b) {
            return;
        }
        b->move(r.x + m_pad, y);
        b->resize(innerW, m_itemH);
        y += m_itemH + m_gap;
    };

    place(m_file);
    place(m_media);
    place(m_audio);
    place(m_contact);
    place(m_location);
}

