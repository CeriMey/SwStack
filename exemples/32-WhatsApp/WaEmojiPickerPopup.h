#pragma once

#include "SwFrame.h"

class SwEmojiPiker;

class WaEmojiPickerPopup final : public SwFrame {
    SW_OBJECT(WaEmojiPickerPopup, SwFrame)

public:
    explicit WaEmojiPickerPopup(SwWidget* parent = nullptr);

    SwEmojiPiker* picker() const { return m_picker; }

signals:
    DECLARE_SIGNAL(emojiPicked, SwString);

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    void buildUi_();
    void updateLayout_();

    SwEmojiPiker* m_picker{nullptr};
    int m_pad{10};
};
