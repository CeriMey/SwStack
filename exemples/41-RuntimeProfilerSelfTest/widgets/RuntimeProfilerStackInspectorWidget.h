#pragma once

#include "SwCodeEditor.h"
#include "SwLayout.h"
#include "SwWidget.h"
#include "RuntimeProfilerViewTypes.h"

class RuntimeProfilerStackInspectorWidget : public SwWidget {
    SW_OBJECT(RuntimeProfilerStackInspectorWidget, SwWidget)

public:
    explicit RuntimeProfilerStackInspectorWidget(SwWidget* parent = nullptr);

    void showEntry(const RuntimeProfilerStackInspectorData& data);
    void clearEntry();
    bool isPinnedToTop() const;

    virtual SwSize minimumSizeHint() const override;

private:
    void buildUi_();
    void applyEditorTheme_();
    SwString reportTextFor_(const RuntimeProfilerStackInspectorData& data) const;

    SwCodeEditor* editor_{nullptr};
    unsigned long long lastSequence_{0};
    SwString lastReportText_;
};
