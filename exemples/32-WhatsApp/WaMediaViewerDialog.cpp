#include "WaMediaViewerDialog.h"

#include "WaMediaImageLoader.h"

#include "SwLabel.h"
#include "SwVideoWidget.h"

#include "media/SwMediaFoundationMovieSource.h"

class WaImageView final : public SwWidget {
    SW_OBJECT(WaImageView, SwWidget)

public:
    explicit WaImageView(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setStyleSheet("SwWidget { background-color: rgb(0, 0, 0); border-width: 0px; }");
        setFocusPolicy(FocusPolicyEnum::NoFocus);
    }

    void setImage(const SwImage& image) {
        m_image = image;
        update();
    }

protected:
    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect r = getRect();
        painter->fillRect(r, SwColor{0, 0, 0}, SwColor{0, 0, 0}, 0);

        if (m_image.isNull()) {
            return;
        }

        const int iw = m_image.width();
        const int ih = m_image.height();
        if (iw <= 0 || ih <= 0) {
            return;
        }

        const float sx = static_cast<float>(r.width) / static_cast<float>(iw);
        const float sy = static_cast<float>(r.height) / static_cast<float>(ih);
        const float s = std::min(sx, sy);
        const int tw = std::max(1, static_cast<int>(iw * s));
        const int th = std::max(1, static_cast<int>(ih * s));

        const int x = r.x + (r.width - tw) / 2;
        const int y = r.y + (r.height - th) / 2;
        painter->drawImage(SwRect{x, y, tw, th}, m_image, nullptr);
    }

private:
    SwImage m_image;
};

WaMediaViewerDialog::WaMediaViewerDialog(Kind kind, const SwString& absoluteFilePath, SwWidget* parent)
    : SwDialog(parent), m_kind(kind), m_absPath(absoluteFilePath) {
    setModal(false);
    setUseNativeWindow(true);
    resize(980, 620);
    setStyleSheet(R"(
        SwDialog {
            background-color: rgb(0, 0, 0);
            border-color: rgb(0, 0, 0);
            border-width: 0px;
            border-radius: 14px;
        }
    )");

    if (buttonBarWidget()) {
        buttonBarWidget()->hide();
    }

    buildUi_();
    updateLayout_();
}

WaMediaViewerDialog::~WaMediaViewerDialog() {
    if (m_videoWidget) {
        m_videoWidget->stop();
    }
    if (m_videoSource) {
        m_videoSource->stop();
    }
}

void WaMediaViewerDialog::resizeEvent(ResizeEvent* event) {
    SwDialog::resizeEvent(event);
    updateLayout_();
}

void WaMediaViewerDialog::buildUi_() {
    SwWidget* content = contentWidget();
    if (!content) {
        return;
    }

    if (m_kind == Kind::Image) {
        m_imageView = new WaImageView(content);
        m_imageView->show();

        SwImage img = WaMediaImageLoader::loadImageFit(m_absPath, 1920, 1080);
        m_imageView->setImage(img);

        setWindowTitle("Photo");
        return;
    }

    if (m_kind == Kind::Video) {
#if defined(_WIN32)
        m_videoWidget = new SwVideoWidget(content);
        m_videoWidget->setBackgroundColor({0, 0, 0});
        m_videoWidget->setScalingMode(SwVideoWidget::ScalingMode::Fit);
        m_videoWidget->show();

        auto movie = std::make_shared<SwMediaFoundationMovieSource>(m_absPath.toStdWString());
        if (movie->initialize()) {
            m_videoSource = movie;
            m_videoWidget->setVideoSource(movie);
            m_videoWidget->start();
        } else {
            auto* label = new SwLabel("Impossible de lire cette vidéo.", content);
            label->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(255,255,255); font-size: 14px; }");
            label->show();
        }
        setWindowTitle("Vidéo");
#else
        auto* label = new SwLabel("La lecture vidéo est supportée uniquement sur Windows.", content);
        label->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(255,255,255); font-size: 14px; }");
        label->show();
        setWindowTitle("Vidéo");
#endif
        return;
    }
}

void WaMediaViewerDialog::updateLayout_() {
    SwWidget* content = contentWidget();
    if (!content) {
        return;
    }

    const SwRect r = content->getRect();
    if (m_imageView) {
        m_imageView->move(r.x, r.y);
        m_imageView->resize(r.width, r.height);
    }
    if (m_videoWidget) {
        m_videoWidget->move(r.x, r.y);
        m_videoWidget->resize(r.width, r.height);
    }
}

