#include "IpcRingBufferPlayer.h"

#include "IpcRingBufferVideoWidget.h"

#include "SwLabel.h"
#include "SwLayout.h"
#include "SwMainWindow.h"
#include "SwTimer.h"
#include "SwGuiApplication.h"

#include "media/SwVideoFrame.h"
#include "media/SwVideoTypes.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {

static bool parseKeyValueArg(const char* arg, const char* key, SwString& valueOut) {
    if (!arg || !key) return false;
    std::string a(arg);
    std::string k(key);
    if (a.rfind(k, 0) != 0) return false;
    if (a.size() <= k.size()) return false;
    if (a[k.size()] != '=') return false;
    valueOut = SwString(a.substr(k.size() + 1));
    return true;
}

static bool splitTarget(const SwString& fqn, SwString& domainOut, SwString& objectOut) {
    SwString x = fqn.trimmed();
    x.replace('\\', '/');
    while (x.contains("//")) x.replace("//", "/");

    const int slash = x.indexOf('/');
    if (slash <= 0 || slash >= x.size() - 1) return false;
    domainOut = x.left(slash);
    objectOut = x.mid(slash + 1);
    return !domainOut.isEmpty() && !objectOut.isEmpty();
}

} // namespace

IpcRingBufferPlayer::IpcRingBufferPlayer(int argc, char** argv) {
    parseArgs_(argc, argv);
    setupUi_();
    tryConnect_();
}

IpcRingBufferPlayer::~IpcRingBufferPlayer() = default;

void IpcRingBufferPlayer::show() {
    if (window_) window_->show();
}

void IpcRingBufferPlayer::parseArgs_(int argc, char** argv) {
    SwString target("demo/camera");
    for (int i = 1; i < argc; ++i) {
        SwString v;
        if (parseKeyValueArg(argv[i], "--target", v)) {
            target = v;
        } else if (parseKeyValueArg(argv[i], "--stream", v)) {
            stream_ = v;
        }
    }

    SwString d, o;
    if (!splitTarget(target, d, o)) {
        domain_ = "demo";
        object_ = "camera";
    } else {
        domain_ = d;
        object_ = o;
    }
    if (stream_.isEmpty()) stream_ = "video";
}

void IpcRingBufferPlayer::setupUi_() {
    window_.reset(new SwMainWindow(L"IpcRingBufferPlayer", 1280, 760));

    infoLabel_ = new SwLabel(window_.get());
    infoLabel_->setText("Connecting...");
    infoLabel_->setMinimumSize(480, 28);

    videoWidget_ = new IpcRingBufferVideoWidget(window_.get());
    videoWidget_->show();

    auto* layout = new SwVerticalLayout(window_.get());
    layout->setMargin(20);
    layout->setSpacing(12);
    window_->setLayout(layout);
    layout->addWidget(infoLabel_, 0, infoLabel_->minimumSizeHint().height);
    layout->addWidget(videoWidget_, 1);

    statsTimer_.reset(new SwTimer(1000, window_.get()));
    statsTimer_->setSingleShot(false);
    SwObject::connect(statsTimer_.get(), &SwTimer::timeout, [this]() {
        updateStats_();
    });
    statsTimer_->start();
}

void IpcRingBufferPlayer::tryConnect_() {
    try {
        reg_ = std::make_unique<sw::ipc::Registry>(domain_, object_);
        rb_ = RB::open(*reg_, stream_);
        consumer_ = rb_.consumer();
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Open failed: " << e.what();
        infoLabel_->setText(SwString(oss.str()));
        return;
    }

    sub_ = rb_.connect(consumer_, [this](uint64_t seq, RB::ReadLease lease) {
        onFrame_(seq, std::move(lease));
    }, /*fireInitial=*/true);

    infoLabel_->setText("Waiting for frames...");
}

void IpcRingBufferPlayer::onFrame_(uint64_t seq, RB::ReadLease lease) {
    if (!lease.isValid()) return;

    // Video use-case: "latest wins". Advance our consumer cursor immediately so the publisher
    // can keep publishing even if we skip intermediate seq values (the leased slot itself is
    // still protected by refCount until the frame is replaced).
    consumer_.keepUp(seq + 1);

    const IpcRingBufferFrameMeta& meta = lease.meta();
    if (meta.width == 0 || meta.height == 0) return;

    const SwVideoPixelFormat fmt = static_cast<SwVideoPixelFormat>(meta.pixelFormat);
    if (fmt != SwVideoPixelFormat::BGRA32) return;

    SwVideoFormatInfo info = SwDescribeVideoFormat(fmt, static_cast<int>(meta.width), static_cast<int>(meta.height));
    if (!info.isValid()) return;

    if (meta.stride != 0) {
        info.stride[0] = static_cast<int>(meta.stride);
    }

    auto leasePtr = std::make_shared<RB::ReadLease>(std::move(lease));
    uint8_t* data = leasePtr->data();
    if (!data) return;

    std::shared_ptr<void> external(leasePtr, leasePtr.get());
    SwVideoFrame frame = SwVideoFrame::wrapExternal(info, std::move(external), static_cast<std::size_t>(rb_.maxBytesPerItem()), data);
    frame.setTimestamp(meta.pts);

    videoWidget_->setFrame(std::move(frame));

    frames_.fetch_add(1, std::memory_order_relaxed);
    width_.store(static_cast<int>(meta.width), std::memory_order_relaxed);
    height_.store(static_cast<int>(meta.height), std::memory_order_relaxed);
    lastSeq_.store(seq, std::memory_order_relaxed);
}

void IpcRingBufferPlayer::updateStats_() {
    const int fps = frames_.exchange(0, std::memory_order_relaxed);
    const int w = width_.load(std::memory_order_relaxed);
    const int h = height_.load(std::memory_order_relaxed);
    const uint64_t seq = lastSeq_.load(std::memory_order_relaxed);

    std::ostringstream oss;
    oss << domain_.toStdString() << "/" << object_.toStdString()
        << " stream=" << stream_.toStdString()
        << " " << w << "x" << h
        << " @" << fps << " fps"
        << " seq=" << seq
        << " dropped=" << rb_.droppedCount();
    infoLabel_->setText(SwString(oss.str()));
}
