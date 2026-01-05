#pragma once

#include "SwDialog.h"
#include "SwString.h"

#include <memory>

class SwVideoWidget;
class WaImageView;

class SwVideoSource;

class WaMediaViewerDialog final : public SwDialog {
    SW_OBJECT(WaMediaViewerDialog, SwDialog)

public:
    enum class Kind {
        Image,
        Video,
    };

    WaMediaViewerDialog(Kind kind, const SwString& absoluteFilePath, SwWidget* parent = nullptr);
    ~WaMediaViewerDialog() override;

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    void buildUi_();
    void updateLayout_();

    Kind m_kind{Kind::Image};
    SwString m_absPath;

    WaImageView* m_imageView{nullptr};
    SwVideoWidget* m_videoWidget{nullptr};

    std::shared_ptr<SwVideoSource> m_videoSource;
};

