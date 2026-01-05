#include "WaEmojiPickerPopup.h"

#include "SwEmojiPicker.h"

#include <algorithm>

WaEmojiPickerPopup::WaEmojiPickerPopup(SwWidget* parent)
    : SwFrame(parent) {
    setFrameShape(SwFrame::Shape::Box);
    setStyleSheet(R"(
        WaEmojiPickerPopup {
            background-color: rgb(255, 255, 255);
            border-color: rgb(220, 224, 232);
            border-width: 1px;
            border-radius: 16px;
        }
    )");
    setFocusPolicy(FocusPolicyEnum::NoFocus);
    buildUi_();
}

void WaEmojiPickerPopup::resizeEvent(ResizeEvent* event) {
    SwFrame::resizeEvent(event);
    updateLayout_();
}

void WaEmojiPickerPopup::buildUi_() {
    if (m_picker) {
        return;
    }

    m_picker = new SwEmojiPicker(this);
    m_picker->setStyleSheet(R"(
        SwEmojiPiker { background-color: rgba(0,0,0,0); border-width: 0px; }
    )");

    SwObject::connect(m_picker, &SwEmojiPiker::emojiClicked, [this](const SwString& emoji) {
        emojiPicked(emoji);
    });

    updateLayout_();
}

void WaEmojiPickerPopup::updateLayout_() {
    if (!m_picker) {
        return;
    }
    const SwRect r = getRect();
    m_picker->move(r.x + m_pad, r.y + m_pad);
    m_picker->resize(std::max(0, r.width - 2 * m_pad), std::max(0, r.height - 2 * m_pad));
}
