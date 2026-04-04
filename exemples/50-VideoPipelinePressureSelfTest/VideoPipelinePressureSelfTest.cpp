#include "media/SwVideoDecoder.h"
#include "media/SwMediaPlayer.h"
#include "media/SwVideoSource.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

bool waitForPredicate(std::condition_variable& cv,
                      std::mutex& mutex,
                      const std::function<bool()>& predicate,
                      int timeoutMs) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(mutex);
    while (!predicate()) {
        if (cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            return predicate();
        }
    }
    return true;
}

SwByteArray makePayload(int size, char fill) {
    SwByteArray payload;
    for (int i = 0; i < size; ++i) {
        payload.append(static_cast<char>(fill + (i % 7)));
    }
    return payload;
}

SwVideoPacket makePacket(std::int64_t pts, bool keyFrame, int payloadSize, char fill) {
    return SwVideoPacket(SwVideoPacket::Codec::H265,
                         makePayload(payloadSize, fill),
                         pts,
                         pts,
                         keyFrame);
}

bool equalsSequence(const std::vector<std::int64_t>& values,
                    std::initializer_list<std::int64_t> expected) {
    if (values.size() != expected.size()) {
        return false;
    }
    std::size_t index = 0;
    for (std::initializer_list<std::int64_t>::const_iterator it = expected.begin();
         it != expected.end();
         ++it, ++index) {
        if (values[index] != *it) {
            return false;
        }
    }
    return true;
}

class FakeVideoSource : public SwVideoSource {
public:
    SwString name() const override { return "FakeVideoSource"; }

    void start() override { setRunning(true); }

    void stop() override { setRunning(false); }

    void pushPacket(const SwVideoPacket& packet) {
        if (!isRunning()) {
            return;
        }
        emitPacket(packet);
    }

    void emitRecoveryEvent(SwMediaSource::RecoveryEvent::Kind kind,
                           const SwString& reason) {
        emitRecovery(kind, reason);
    }

    bool waitForPressure(const std::function<bool(const SwVideoSource::ConsumerPressure&)>& predicate,
                         int timeoutMs) {
        return waitForPredicate(m_cv,
                                m_mutex,
                                [this, &predicate]() {
                                    if (predicate(m_lastPressure)) {
                                        return true;
                                    }
                                    for (std::vector<SwVideoSource::ConsumerPressure>::const_iterator it =
                                             m_history.begin();
                                         it != m_history.end();
                                         ++it) {
                                        if (predicate(*it)) {
                                            return true;
                                        }
                                    }
                                    return false;
                                },
                                timeoutMs);
    }

    bool sawHardPressure() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (std::vector<SwVideoSource::ConsumerPressure>::const_iterator it = m_history.begin();
             it != m_history.end();
             ++it) {
            if (it->hardPressure) {
                return true;
            }
        }
        return false;
    }

protected:
    void handleConsumerPressureChanged(const SwVideoSource::ConsumerPressure& pressure) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastPressure = pressure;
            m_history.push_back(pressure);
        }
        m_cv.notify_all();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    SwVideoSource::ConsumerPressure m_lastPressure{};
    std::vector<SwVideoSource::ConsumerPressure> m_history{};
};

class RecordingDecoder : public SwVideoDecoder {
public:
    explicit RecordingDecoder(int delayMs)
        : m_delayMs(delayMs) {}

    const char* name() const override { return "RecordingDecoder"; }

    bool feed(const SwVideoPacket& packet) override {
        if (m_delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_delayMs));
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_feedPts.push_back(packet.pts());
        }
        m_cv.notify_all();
        return true;
    }

    bool waitForFeedCount(std::size_t count, int timeoutMs) {
        return waitForPredicate(m_cv,
                                m_mutex,
                                [this, count]() {
                                    return m_feedPts.size() >= count;
                                },
                                timeoutMs);
    }

    std::vector<std::int64_t> feedPts() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_feedPts;
    }

    int flushCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_flushCount;
    }

    void flush() override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_flushCount;
        }
        m_cv.notify_all();
    }

private:
    int m_delayMs{0};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::int64_t> m_feedPts{};
    int m_flushCount{0};
};

class BlockingDecoder : public SwVideoDecoder {
public:
    const char* name() const override { return "BlockingDecoder"; }

    bool feed(const SwVideoPacket& packet) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_startedPts.push_back(packet.pts());
        }
        m_cv.notify_all();
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return !m_blockFeeds; });
        m_completedPts.push_back(packet.pts());
        return true;
    }

    void flush() override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_flushCount;
        }
        m_cv.notify_all();
    }

    bool waitForStartedCount(std::size_t count, int timeoutMs) {
        return waitForPredicate(m_cv,
                                m_mutex,
                                [this, count]() {
                                    return m_startedPts.size() >= count;
                                },
                                timeoutMs);
    }

    bool waitForCompletedCount(std::size_t count, int timeoutMs) {
        return waitForPredicate(m_cv,
                                m_mutex,
                                [this, count]() {
                                    return m_completedPts.size() >= count;
                                },
                                timeoutMs);
    }

    void unblockFeeds() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_blockFeeds = false;
        }
        m_cv.notify_all();
    }

    std::vector<std::int64_t> completedPts() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_completedPts;
    }

    int flushCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_flushCount;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::int64_t> m_startedPts{};
    std::vector<std::int64_t> m_completedPts{};
    bool m_blockFeeds{true};
    int m_flushCount{0};
};

bool runSoftPressureNoTrimTest() {
    std::shared_ptr<FakeVideoSource> source = std::make_shared<FakeVideoSource>();
    std::shared_ptr<RecordingDecoder> decoder = std::make_shared<RecordingDecoder>(220);
    std::shared_ptr<SwVideoPipeline> pipeline = std::make_shared<SwVideoPipeline>();
    pipeline->setAsyncDecode(true);
    pipeline->setDecoder(decoder);
    pipeline->setSource(source);
    pipeline->setQueueLimits(4, 512U * 1024U);
    pipeline->start();

    source->pushPacket(makePacket(100, true, 64 * 1024, 'a'));
    source->pushPacket(makePacket(200, false, 64 * 1024, 'b'));
    source->pushPacket(makePacket(300, false, 64 * 1024, 'c'));
    source->pushPacket(makePacket(400, true, 64 * 1024, 'd'));

    const bool sawSoftPressure = source->waitForPressure(
        [](const SwVideoSource::ConsumerPressure& pressure) {
            return pressure.softPressure;
        },
        1500);
    const bool decodedAllPackets = decoder->waitForFeedCount(4U, 2500);

    pipeline->stop();

    const std::vector<std::int64_t> feedPts = decoder->feedPts();
    if (!sawSoftPressure || !decodedAllPackets ||
        !equalsSequence(feedPts, {100, 200, 300, 400}) || source->sawHardPressure()) {
        std::cerr << "[soft-pressure-no-trim] FAIL"
                  << " sawSoftPressure=" << (sawSoftPressure ? 1 : 0)
                  << " decodedAllPackets=" << (decodedAllPackets ? 1 : 0)
                  << " sawHardPressure=" << (source->sawHardPressure() ? 1 : 0)
                  << " feedCount=" << feedPts.size()
                  << "\n";
        return false;
    }

    std::cout << "[soft-pressure-no-trim] PASS\n";
    return true;
}

bool runHardGuardTest() {
    std::shared_ptr<FakeVideoSource> source = std::make_shared<FakeVideoSource>();
    std::shared_ptr<RecordingDecoder> decoder = std::make_shared<RecordingDecoder>(260);
    std::shared_ptr<SwVideoPipeline> pipeline = std::make_shared<SwVideoPipeline>();
    pipeline->setAsyncDecode(true);
    pipeline->setDecoder(decoder);
    pipeline->setSource(source);
    pipeline->setQueueLimits(2, 512U * 1024U);
    pipeline->start();

    source->pushPacket(makePacket(100, true, 48 * 1024, 'a'));
    source->pushPacket(makePacket(200, false, 48 * 1024, 'b'));
    source->pushPacket(makePacket(300, false, 48 * 1024, 'c'));
    source->pushPacket(makePacket(400, false, 48 * 1024, 'd'));
    source->pushPacket(makePacket(500, true, 48 * 1024, 'e'));

    const bool sawHardPressure = source->waitForPressure(
        [](const SwVideoSource::ConsumerPressure& pressure) {
            return pressure.hardPressure;
        },
        1500);
    const bool decodedAtLeastTwo = decoder->waitForFeedCount(2U, 2500);

    pipeline->stop();

    const std::vector<std::int64_t> feedPts = decoder->feedPts();
    const bool preservedFifoPrefix =
        feedPts.size() >= 2U && feedPts[0] == 100 && feedPts[1] == 200;
    const bool rejectedSomePackets = feedPts.size() < 5U;
    if (!sawHardPressure || !decodedAtLeastTwo || !preservedFifoPrefix || !rejectedSomePackets) {
        std::cerr << "[hard-guard-fifo] FAIL"
                  << " sawHardPressure=" << (sawHardPressure ? 1 : 0)
                  << " decodedAtLeastTwo=" << (decodedAtLeastTwo ? 1 : 0)
                  << " preservedFifoPrefix=" << (preservedFifoPrefix ? 1 : 0)
                  << " rejectedSomePackets=" << (rejectedSomePackets ? 1 : 0)
                  << " feedCount=" << feedPts.size()
                  << "\n";
        return false;
    }

    std::cout << "[hard-guard-fifo] PASS\n";
    return true;
}

bool runRecoverEpochPurgeTest() {
    std::shared_ptr<FakeVideoSource> source = std::make_shared<FakeVideoSource>();
    std::shared_ptr<BlockingDecoder> decoder = std::make_shared<BlockingDecoder>();
    std::shared_ptr<SwVideoPipeline> pipeline = std::make_shared<SwVideoPipeline>();
    pipeline->setAsyncDecode(true);
    pipeline->setDecoder(decoder);
    pipeline->setSource(source);
    pipeline->setQueueLimits(8, 512U * 1024U);
    pipeline->start();

    source->pushPacket(makePacket(100, true, 32 * 1024, 'a'));
    const bool firstFeedStarted = decoder->waitForStartedCount(1U, 1000);
    source->pushPacket(makePacket(200, false, 32 * 1024, 'b'));
    source->pushPacket(makePacket(300, false, 32 * 1024, 'c'));
    source->pushPacket(makePacket(400, true, 32 * 1024, 'd'));

    SwMediaSource::RecoveryEvent event;
    event.epoch = 7;
    event.kind = SwMediaSource::RecoveryEvent::Kind::LiveCut;
    event.reason = "self-test recover";
    pipeline->recoverLiveEdge(event);

    source->pushPacket(makePacket(500, false, 32 * 1024, 'e'));
    source->pushPacket(makePacket(600, true, 32 * 1024, 'f'));
    source->pushPacket(makePacket(700, false, 32 * 1024, 'g'));

    decoder->unblockFeeds();
    const bool completedThreeFeeds = decoder->waitForCompletedCount(3U, 2500);

    pipeline->stop();

    const std::vector<std::int64_t> feedPts = decoder->completedPts();
    const bool flushedOnRecover = decoder->flushCount() >= 1;
    const bool purgedQueuedOldPackets =
        equalsSequence(feedPts, {100, 600, 700});
    if (!firstFeedStarted || !completedThreeFeeds || !flushedOnRecover || !purgedQueuedOldPackets) {
        std::cerr << "[recover-epoch-purge] FAIL"
                  << " firstFeedStarted=" << (firstFeedStarted ? 1 : 0)
                  << " completedThreeFeeds=" << (completedThreeFeeds ? 1 : 0)
                  << " flushedOnRecover=" << (flushedOnRecover ? 1 : 0)
                  << " feedCount=" << feedPts.size()
                  << "\n";
        return false;
    }

    std::cout << "[recover-epoch-purge] PASS\n";
    return true;
}

bool runPlayerStopRestartRecoverTest() {
    std::mutex mutex;
    std::condition_variable cv;
    int frameCount = 0;
    auto installFrameCallback = [&](SwMediaPlayer& player) {
        player.videoSink()->setFrameCallback([&](const SwVideoFrame&) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++frameCount;
            }
            cv.notify_all();
        });
    };
    auto makeRawFramePacket = [](std::int64_t pts, char fill) {
        SwVideoPacket packet = makePacket(pts, true, 2 * 2 * 4, fill);
        packet.setCodec(SwVideoPacket::Codec::RawBGRA);
        packet.setRawFormat(SwDescribeVideoFormat(SwVideoPixelFormat::BGRA32, 2, 2));
        return packet;
    };

    std::shared_ptr<FakeVideoSource> source1 = std::make_shared<FakeVideoSource>();
    std::unique_ptr<SwMediaPlayer> player1(new SwMediaPlayer());
    installFrameCallback(*player1);
    player1->setSource(source1);
    player1->play();

    source1->pushPacket(makeRawFramePacket(100, 'a'));
    const bool firstFramePresented = waitForPredicate(cv, mutex, [&]() { return frameCount >= 1; }, 500);

    source1->emitRecoveryEvent(SwMediaSource::RecoveryEvent::Kind::LiveCut, "player self-test");
    player1->stop();
    player1->setSource(std::shared_ptr<SwMediaSource>());
    player1.reset();

    source1->emitRecoveryEvent(SwMediaSource::RecoveryEvent::Kind::LiveCut,
                               "ignored after player destruction");

    std::shared_ptr<FakeVideoSource> source2 = std::make_shared<FakeVideoSource>();
    std::unique_ptr<SwMediaPlayer> player2(new SwMediaPlayer());
    installFrameCallback(*player2);
    player2->setSource(source2);
    player2->play();
    source2->pushPacket(makeRawFramePacket(200, 'b'));
    const bool secondFramePresented = waitForPredicate(cv, mutex, [&]() { return frameCount >= 2; }, 500);

    player2->stop();
    player2->setSource(std::shared_ptr<SwMediaSource>());

    if (!firstFramePresented || !secondFramePresented) {
        std::cerr << "[player-stop-restart-recover] FAIL"
                  << " firstFramePresented=" << (firstFramePresented ? 1 : 0)
                  << " secondFramePresented=" << (secondFramePresented ? 1 : 0)
                  << "\n";
        return false;
    }

    std::cout << "[player-stop-restart-recover] PASS\n";
    return true;
}

} // namespace

int main() {
    bool ok = true;
    ok = runSoftPressureNoTrimTest() && ok;
    ok = runHardGuardTest() && ok;
    ok = runRecoverEpochPurgeTest() && ok;
    ok = runPlayerStopRestartRecoverTest() && ok;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
