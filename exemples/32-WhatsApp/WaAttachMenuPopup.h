#pragma once

#include "SwFrame.h"
#include "SwString.h"

class SwButton;

class WaAttachMenuPopup final : public SwFrame {
    SW_OBJECT(WaAttachMenuPopup, SwFrame)

public:
    explicit WaAttachMenuPopup(SwWidget* parent = nullptr);

signals:
    DECLARE_SIGNAL(actionTriggered, SwString);

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    void buildUi_();
    void updateLayout_();
    static SwButton* makeItem_(const SwString& text, SwWidget* parent);

    SwButton* m_file{nullptr};
    SwButton* m_media{nullptr};
    SwButton* m_audio{nullptr};
    SwButton* m_contact{nullptr};
    SwButton* m_location{nullptr};

    int m_pad{10};
    int m_gap{6};
    int m_itemH{36};
};

