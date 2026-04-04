#pragma once

#include "SwCodeEditor.h"
#include "SwLayout.h"
#include "SwScrollBar.h"
#include "SwWidget.h"
#include "SocketTrafficViewTypes.h"

class SocketTrafficInspectorWidget : public SwWidget {
    SW_OBJECT(SocketTrafficInspectorWidget, SwWidget)

public:
    explicit SocketTrafficInspectorWidget(SwWidget* parent = nullptr);

    void showConsumer(const SocketTrafficInspectorData& data);
    void clearEntry();

    virtual SwSize minimumSizeHint() const override;

private:
    void buildUi_();
    void applyEditorTheme_();
    void restoreViewport_(int firstVisibleLine);
    SwString reportTextFor_(const SocketTrafficInspectorData& data) const;

    SwCodeEditor* editor_{nullptr};
    SwString lastReportText_;
    unsigned long long lastConsumerId_{0};
};
