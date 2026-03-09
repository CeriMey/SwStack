#pragma once

#include "SwString.h"
#include "SwWidget.h"

class SwCodeEditor;

class SwCreatorEditorPage : public SwWidget {
    SW_OBJECT(SwCreatorEditorPage, SwWidget)

public:
    explicit SwCreatorEditorPage(SwWidget* parent = nullptr);

    bool openFile(const SwString& filePath);
    bool save();

    SwString filePath() const;
    SwString displayName() const;
    bool isDirty() const;
    SwCodeEditor* editor() const;

signals:
    DECLARE_SIGNAL(dirtyStateChanged, bool);

private:
    void onEditorTextChanged_();
    void setDirtyState_(bool dirty);

    SwCodeEditor* m_editor{nullptr};
    SwString m_filePath;
    SwString m_savedText;
    bool m_dirty{false};
    bool m_loading{false};
};
