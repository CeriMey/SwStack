#pragma once

#include "SwMainWindow.h"

class WhatsAppWidget;

class WhatsAppWindow final : public SwMainWindow {
    SW_OBJECT(WhatsAppWindow, SwMainWindow)

public:
    WhatsAppWindow();

    bool saveSnapshot(const SwString& outDir);

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    void ensureRoot_(const SwString& storageRoot = SwString());
    void stripChrome_();

    WhatsAppWidget* m_root{nullptr};
    SwString m_rootStorageRoot;
};
