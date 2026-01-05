#pragma once

#include "SwPlainTextEdit.h"

class SwScrollBar;

class WaMessageEdit final : public SwPlainTextEdit {
    SW_OBJECT(WaMessageEdit, SwPlainTextEdit)

public:
    explicit WaMessageEdit(const SwString& placeholderText = SwString(), SwWidget* parent = nullptr);

    SwString getText() const { return toPlainText(); }
    void setText(const SwString& text) { setPlainText(text); }

    void setMaxLines(int lines);
    int maxLines() const { return m_maxLines; }
    int preferredHeight();

signals:
    DECLARE_SIGNAL_VOID(submitted);

protected:
    void paintEvent(PaintEvent* event) override;
    void resizeEvent(ResizeEvent* event) override;
    void wheelEvent(WheelEvent* event) override;
    void keyPressEvent(KeyEvent* event) override;

private:
    void updateScrollBar_();
    void updateScrollBarGeometry_();
    int visibleLines_();

    SwScrollBar* m_scrollBar{nullptr};
    bool m_syncingScrollBar{false};
    int m_maxLines{5};
    int m_minHeightPx{36};
};
