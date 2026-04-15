#include "media/SwRtspTrackGraph.h"
#include "media/SwVideoDecoder.h"
#include "media/SwPlatformVideoDecoderIds.h"
#include "media/rtp/SwRtpDepacketizerH264.h"
#include "media/rtp/SwRtpDepacketizerH265.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct VideoObservation {
    std::int64_t pts{0};
    bool keyFrame{false};
    bool discontinuity{false};
};

struct MediaObservation {
    SwMediaPacket::Type type{SwMediaPacket::Type::Unknown};
    std::int64_t pts{0};
    bool discontinuity{false};
};

struct DepacketizedObservation {
    std::int64_t pts{0};
    bool keyFrame{false};
    bool discontinuity{false};
    SwByteArray payload{};
};

SwByteArray makePayload(const char* data, int size) {
    SwByteArray payload;
    payload.append(data, size);
    return payload;
}

SwByteArray makeSizedPayload(const SwByteArray& prefix, int targetSize) {
    SwByteArray payload = prefix;
    while (payload.size() < targetSize) {
        const int currentSize = static_cast<int>(payload.size());
        const int remaining = targetSize - currentSize;
        const int chunk = std::min(remaining, 256);
        for (int i = 0; i < chunk; ++i) {
            payload.append(static_cast<char>('a' + (i % 23)));
        }
    }
    return payload;
}

void appendByte(SwByteArray& payload, uint8_t value) {
    payload.append(static_cast<char>(value));
}

void appendUint16Be(SwByteArray& payload, uint16_t value) {
    const char bytes[2] = {
        static_cast<char>((value >> 8) & 0xFFU),
        static_cast<char>(value & 0xFFU)
    };
    payload.append(bytes, 2);
}

void appendBytes(SwByteArray& payload, std::initializer_list<uint8_t> bytes) {
    for (std::initializer_list<uint8_t>::const_iterator it = bytes.begin();
         it != bytes.end();
         ++it) {
        appendByte(payload, *it);
    }
}

void appendNalToAggregation(SwByteArray& payload, std::initializer_list<uint8_t> nal) {
    appendUint16Be(payload, static_cast<uint16_t>(nal.size()));
    appendBytes(payload, nal);
}

void appendNalToAggregation(SwByteArray& payload, const SwByteArray& nal) {
    appendUint16Be(payload, static_cast<uint16_t>(nal.size()));
    payload.append(nal);
}

bool byteArrayContains(const SwByteArray& data, std::initializer_list<uint8_t> pattern) {
    if (pattern.size() == 0U) {
        return true;
    }
    if (static_cast<std::size_t>(data.size()) < pattern.size()) {
        return false;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    if (!bytes) {
        return false;
    }
    for (std::size_t offset = 0; offset + pattern.size() <= static_cast<std::size_t>(data.size());
         ++offset) {
        bool match = true;
        std::size_t index = 0;
        for (std::initializer_list<uint8_t>::const_iterator it = pattern.begin();
             it != pattern.end();
             ++it, ++index) {
            if (bytes[offset + index] != *it) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool byteArrayEquals(const SwByteArray& data, std::initializer_list<uint8_t> expected) {
    if (static_cast<std::size_t>(data.size()) != expected.size()) {
        return false;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    if (!bytes) {
        return expected.size() == 0U;
    }
    std::size_t index = 0;
    for (std::initializer_list<uint8_t>::const_iterator it = expected.begin();
         it != expected.end();
         ++it, ++index) {
        if (bytes[index] != *it) {
            return false;
        }
    }
    return true;
}

SwRtpSession::Packet makeH264Packet(uint16_t sequenceNumber,
                                    uint32_t timestamp,
                                    bool keyFrame,
                                    int payloadSize = 1) {
    SwRtpSession::Packet packet;
    packet.payloadType = 96;
    packet.sequenceNumber = sequenceNumber;
    packet.timestamp = timestamp;
    packet.marker = true;
    const char nal = static_cast<char>(keyFrame ? 0x65 : 0x41);
    packet.payload = makeSizedPayload(makePayload(&nal, 1), payloadSize);
    return packet;
}

SwRtpSession::Packet makeH264StapBPacket(uint16_t sequenceNumber,
                                         uint32_t timestamp) {
    SwRtpSession::Packet packet;
    packet.payloadType = 96;
    packet.sequenceNumber = sequenceNumber;
    packet.timestamp = timestamp;
    packet.marker = true;

    SwByteArray payload;
    appendByte(payload, 0x79U);
    appendUint16Be(payload, 1U);
    appendNalToAggregation(payload, {0x67U, 0x42U, 0x00U, 0x1FU});
    appendNalToAggregation(payload, {0x68U, 0xCEU, 0x06U, 0xE2U});
    appendNalToAggregation(payload, {0x65U, 0x88U, 0x84U, 0x21U});
    packet.payload = payload;
    return packet;
}

SwRtpSession::Packet makeH264FuBPacket(uint16_t sequenceNumber,
                                       uint32_t timestamp,
                                       bool start,
                                       bool end,
                                       std::initializer_list<uint8_t> fragment) {
    SwRtpSession::Packet packet;
    packet.payloadType = 96;
    packet.sequenceNumber = sequenceNumber;
    packet.timestamp = timestamp;
    packet.marker = end;

    SwByteArray payload;
    appendByte(payload, 0x7DU);
    appendByte(payload,
               static_cast<uint8_t>((start ? 0x80U : 0x00U) |
                                    (end ? 0x40U : 0x00U) |
                                    0x05U));
    appendUint16Be(payload, 1U);
    appendBytes(payload, fragment);
    packet.payload = payload;
    return packet;
}

SwRtpSession::Packet makeH265Packet(uint16_t sequenceNumber,
                                    uint32_t timestamp,
                                    bool keyFrame,
                                    int payloadSize = 3) {
    auto makeNal = [](uint8_t nalType, int targetSize, uint8_t seed) {
        SwByteArray nal;
        const int nalSize = std::max(2, targetSize);
        nal.reserve(static_cast<size_t>(nalSize));
        appendByte(nal, static_cast<uint8_t>(nalType << 1U));
        appendByte(nal, 0x01U);
        while (static_cast<int>(nal.size()) < nalSize) {
            appendByte(nal, static_cast<uint8_t>(seed + (nal.size() & 0x3FU)));
        }
        return nal;
    };

    SwRtpSession::Packet packet;
    packet.payloadType = 96;
    packet.sequenceNumber = sequenceNumber;
    packet.timestamp = timestamp;
    packet.marker = true;
    if (!keyFrame) {
        packet.payload = makeSizedPayload(makeNal(1U, 2, 0x55U), payloadSize);
        return packet;
    }

    const SwByteArray vps = makeNal(32U, 4, 0x20U);
    const SwByteArray sps = makeNal(33U, 4, 0x30U);
    const SwByteArray pps = makeNal(34U, 4, 0x40U);
    const int fixedBytes =
        2 + (2 + static_cast<int>(vps.size())) + (2 + static_cast<int>(sps.size())) +
        (2 + static_cast<int>(pps.size())) + 2;
    const SwByteArray idr = makeNal(19U, std::max(2, payloadSize - fixedBytes), 0x50U);

    SwByteArray payload;
    payload.reserve(static_cast<size_t>(fixedBytes + idr.size()));
    appendByte(payload, static_cast<uint8_t>(48U << 1U));
    appendByte(payload, 0x01U);
    appendNalToAggregation(payload, vps);
    appendNalToAggregation(payload, sps);
    appendNalToAggregation(payload, pps);
    appendNalToAggregation(payload, idr);
    packet.payload = payload;
    return packet;
}

SwMediaPacket makeAudioPacket(std::int64_t pts) {
    SwMediaPacket packet;
    packet.setType(SwMediaPacket::Type::Audio);
    packet.setCodec("opus");
    packet.setPts(pts);
    packet.setDts(pts);
    packet.setClockRate(48000);
    packet.setSampleRate(48000);
    const char bytes[4] = {'a', 'u', 'd', '0'};
    packet.setPayload(makePayload(bytes, 4));
    return packet;
}

bool waitForObservations(std::condition_variable& cv,
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

template <typename PacketFactory>
bool runScenario(const char* name,
                 SwVideoPacket::Codec codec,
                 PacketFactory makePacket,
                 bool useConsumerPressure = false) {
    SwRtspTrackGraph graph;
    SwRtspTrackGraph::VideoConfig config;
    config.codec = codec;
    config.payloadType = 96;
    config.clockRate = 90000;
    config.liveTrimEnabled = true;
    graph.setVideoConfig(config);

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<VideoObservation> videoFrames;
    std::vector<MediaObservation> mediaPackets;
    std::vector<SwMediaSource::RecoveryEvent::Kind> recoveryKinds;
    std::vector<std::string> recoveryReasons;
    std::atomic<int> videoCallbackCount(0);

    graph.setVideoPacketCallback([&](const SwVideoPacket& packet) {
        const int count = videoCallbackCount.fetch_add(1) + 1;
        {
            std::lock_guard<std::mutex> lock(mutex);
            VideoObservation observation;
            observation.pts = packet.pts();
            observation.keyFrame = packet.isKeyFrame();
            observation.discontinuity = packet.isDiscontinuity();
            videoFrames.push_back(observation);
        }
        cv.notify_all();
        if (count == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    graph.setMediaPacketCallback([&](const SwMediaPacket& packet) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            MediaObservation observation;
            observation.type = packet.type();
            observation.pts = packet.pts();
            observation.discontinuity = packet.isDiscontinuity();
            mediaPackets.push_back(observation);
        }
        cv.notify_all();
    });
    graph.setRecoveryCallback([&](SwMediaSource::RecoveryEvent::Kind kind, const SwString& reason) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            recoveryKinds.push_back(kind);
            recoveryReasons.push_back(reason.toStdString());
        }
        cv.notify_all();
    });

    graph.start();

    constexpr int kLargePacketBytes = 80 * 1024;
    constexpr int kSmallPacketBytes = 8 * 1024;
    const int backlogPacketBytes = useConsumerPressure ? kSmallPacketBytes : kLargePacketBytes;
    const int backlogCount = useConsumerPressure ? 6 : 60;
    std::vector<SwRtpSession::Packet> gopPackets;
    gopPackets.reserve(static_cast<std::size_t>(backlogCount) + 2U);
    gopPackets.push_back(makePacket(1, 1000, true, backlogPacketBytes));
    uint16_t sequence = 2;
    uint32_t timestamp = 2000;
    for (int i = 0; i < backlogCount; ++i, ++sequence, timestamp += 1000) {
        gopPackets.push_back(makePacket(sequence, timestamp, false, backlogPacketBytes));
    }
    const uint32_t retainedKeyPts = timestamp;
    gopPackets.push_back(makePacket(sequence, retainedKeyPts, true, backlogPacketBytes));

    graph.submitVideoPacket(gopPackets.front(), false);
    const bool firstFrameObserved = waitForObservations(
        cv,
        mutex,
        [&]() {
            return !videoFrames.empty();
        },
        1000);
    if (!firstFrameObserved) {
        graph.stop();
        std::cerr << "[" << name << "] timeout waiting for first frame\n";
        return false;
    }
    for (std::size_t i = 1; i < gopPackets.size(); ++i) {
        if (useConsumerPressure && i == gopPackets.size() - 1U) {
            SwVideoSource::ConsumerPressure pressure;
            pressure.softPressure = true;
            pressure.queuedPackets = 3;
            pressure.queuedBytes = 3U * static_cast<std::size_t>(backlogPacketBytes);
            graph.setConsumerPressure(pressure);
        }
        graph.submitVideoPacket(gopPackets[i], false);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    graph.submitAudioPacket(makeAudioPacket(48000));

    const bool haveOutputs = waitForObservations(
        cv,
        mutex,
        [&]() {
            return videoFrames.size() >= 2U && !mediaPackets.empty() && !recoveryKinds.empty();
        },
        3000);

    graph.stop();

    if (!haveOutputs) {
        std::cerr << "[" << name << "] timeout waiting for outputs\n";
        return false;
    }

    bool sawFirstKey = false;
    bool sawRetainedKey = false;
    bool sawDroppedMiddle = false;
    bool retainedKeyHasDiscontinuity = false;
    for (std::size_t i = 0; i < videoFrames.size(); ++i) {
        if (videoFrames[i].pts == 1000) {
            sawFirstKey = true;
        }
        if (videoFrames[i].pts == retainedKeyPts) {
            sawRetainedKey = true;
            retainedKeyHasDiscontinuity = videoFrames[i].discontinuity;
        }
        if (videoFrames[i].pts > 1000 && videoFrames[i].pts < retainedKeyPts) {
            sawDroppedMiddle = true;
        }
    }

    const bool audioDiscontinuity = !mediaPackets.empty() && mediaPackets.front().discontinuity;
    const bool sawSingleLiveCutRecovery =
        recoveryKinds.size() == 1U &&
        recoveryKinds.front() == SwMediaSource::RecoveryEvent::Kind::LiveCut;
    if (!sawFirstKey || !sawRetainedKey || sawDroppedMiddle || !retainedKeyHasDiscontinuity ||
        !audioDiscontinuity || !sawSingleLiveCutRecovery) {
        std::cerr << "[" << name << "] unexpected output set"
                  << " sawFirstKey=" << (sawFirstKey ? 1 : 0)
                  << " sawRetainedKey=" << (sawRetainedKey ? 1 : 0)
                  << " sawDroppedMiddle=" << (sawDroppedMiddle ? 1 : 0)
                  << " retainedKeyHasDiscontinuity=" << (retainedKeyHasDiscontinuity ? 1 : 0)
                  << " audioDiscontinuity=" << (audioDiscontinuity ? 1 : 0)
                  << " recoveryCount=" << recoveryKinds.size()
                  << " sawSingleLiveCutRecovery=" << (sawSingleLiveCutRecovery ? 1 : 0);
        if (!recoveryReasons.empty()) {
            std::cerr << " recoveryReasons=";
            for (std::size_t i = 0; i < recoveryReasons.size(); ++i) {
                if (i > 0U) {
                    std::cerr << ",";
                }
                std::cerr << recoveryReasons[i];
            }
        }
        std::cerr
                  << "\n";
        return false;
    }

    std::cout << "[" << name << "] PASS\n";
    return true;
}

bool runH264InterleavedDepacketizerScenario() {
    SwRtpDepacketizerH264 depacketizer;
    std::vector<DepacketizedObservation> frames;

    depacketizer.setPacketCallback([&](const SwVideoPacket& packet) {
        DepacketizedObservation observation;
        observation.pts = packet.pts();
        observation.keyFrame = packet.isKeyFrame();
        observation.discontinuity = packet.isDiscontinuity();
        observation.payload = packet.payload();
        frames.push_back(observation);
    });

    depacketizer.push(makeH264StapBPacket(1, 1000));
    depacketizer.push(makeH264FuBPacket(2, 2000, true, false, {0xAAU, 0xBBU}));
    depacketizer.push(makeH264FuBPacket(3, 2000, false, true, {0xCCU, 0xDDU}));

    const bool stapOk =
        frames.size() >= 1U &&
        frames[0].pts == 1000 &&
        frames[0].keyFrame &&
        byteArrayContains(frames[0].payload, {0x00U, 0x00U, 0x00U, 0x01U, 0x67U}) &&
        byteArrayContains(frames[0].payload, {0x00U, 0x00U, 0x00U, 0x01U, 0x68U}) &&
        byteArrayContains(frames[0].payload, {0x00U, 0x00U, 0x00U, 0x01U, 0x65U});
    const bool fuBOk =
        frames.size() >= 2U &&
        frames[1].pts == 2000 &&
        frames[1].keyFrame &&
        byteArrayEquals(frames[1].payload,
                        {0x00U, 0x00U, 0x00U, 0x01U, 0x65U, 0xAAU, 0xBBU, 0xCCU, 0xDDU});

    if (frames.size() != 2U || !stapOk || !fuBOk) {
        std::cerr << "[H264 interleaved depacketizer] FAIL"
                  << " frameCount=" << frames.size()
                  << " stapOk=" << (stapOk ? 1 : 0)
                  << " fuBOk=" << (fuBOk ? 1 : 0)
                  << "\n";
        return false;
    }

    std::cout << "[H264 interleaved depacketizer] PASS\n";
    return true;
}

bool runH264GapRecoveryScenario() {
    SwRtspTrackGraph graph;
    SwRtspTrackGraph::VideoConfig config;
    config.codec = SwVideoPacket::Codec::H264;
    config.payloadType = 96;
    config.clockRate = 90000;
    graph.setVideoConfig(config);

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<VideoObservation> videoFrames;
    std::vector<SwMediaSource::RecoveryEvent::Kind> recoveryKinds;

    graph.setVideoPacketCallback([&](const SwVideoPacket& packet) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            VideoObservation observation;
            observation.pts = packet.pts();
            observation.keyFrame = packet.isKeyFrame();
            observation.discontinuity = packet.isDiscontinuity();
            videoFrames.push_back(observation);
        }
        cv.notify_all();
    });
    graph.setRecoveryCallback([&](SwMediaSource::RecoveryEvent::Kind kind, const SwString&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            recoveryKinds.push_back(kind);
        }
        cv.notify_all();
    });

    graph.start();
    graph.submitVideoPacket(makeH264Packet(1, 1000, true, 1200), true);
    const bool firstFrameObserved = waitForObservations(
        cv,
        mutex,
        [&]() {
            return !videoFrames.empty();
        },
        1000);
    if (!firstFrameObserved) {
        graph.stop();
        std::cerr << "[H264 RTP gap recovery] timeout waiting for first frame\n";
        return false;
    }

    graph.submitVideoPacket(makeH264Packet(2, 2000, false, 1200), true);
    graph.submitVideoPacket(makeH264Packet(6, 6000, false, 1200), true);
    graph.submitVideoPacket(makeH264Packet(7, 7000, false, 1200), true);
    graph.submitVideoPacket(makeH264Packet(8, 8000, true, 1200), true);

    const bool recovered = waitForObservations(
        cv,
        mutex,
        [&]() {
            bool sawRecoveredKey = false;
            for (std::size_t i = 0; i < videoFrames.size(); ++i) {
                if (videoFrames[i].pts == 8000) {
                    sawRecoveredKey = true;
                    break;
                }
            }
            return !recoveryKinds.empty() && sawRecoveredKey;
        },
        2000);

    graph.stop();

    bool sawCorruptAfterGap = false;
    bool sawRecoveredKey = false;
    bool recoveredKeyHasDiscontinuity = false;
    for (std::size_t i = 0; i < videoFrames.size(); ++i) {
        if (videoFrames[i].pts == 6000 || videoFrames[i].pts == 7000) {
            sawCorruptAfterGap = true;
        }
        if (videoFrames[i].pts == 8000) {
            sawRecoveredKey = videoFrames[i].keyFrame;
            recoveredKeyHasDiscontinuity = videoFrames[i].discontinuity;
        }
    }
    const bool sawLiveCutRecovery =
        !recoveryKinds.empty() &&
        recoveryKinds.front() == SwMediaSource::RecoveryEvent::Kind::LiveCut;
    if (!recovered || sawCorruptAfterGap || !sawRecoveredKey ||
        !recoveredKeyHasDiscontinuity || !sawLiveCutRecovery) {
        std::cerr << "[H264 RTP gap recovery] FAIL"
                  << " recovered=" << (recovered ? 1 : 0)
                  << " sawCorruptAfterGap=" << (sawCorruptAfterGap ? 1 : 0)
                  << " sawRecoveredKey=" << (sawRecoveredKey ? 1 : 0)
                  << " recoveredKeyHasDiscontinuity=" << (recoveredKeyHasDiscontinuity ? 1 : 0)
                  << " recoveryCount=" << recoveryKinds.size()
                  << " sawLiveCutRecovery=" << (sawLiveCutRecovery ? 1 : 0)
                  << "\n";
        return false;
    }

    std::cout << "[H264 RTP gap recovery] PASS\n";
    return true;
}

bool runH265AggregationDepacketizerScenario() {
    SwRtpDepacketizerH265 depacketizer;
    std::vector<DepacketizedObservation> frames;

    depacketizer.setPacketCallback([&](const SwVideoPacket& packet) {
        DepacketizedObservation observation;
        observation.pts = packet.pts();
        observation.keyFrame = packet.isKeyFrame();
        observation.discontinuity = packet.isDiscontinuity();
        observation.payload = packet.payload();
        frames.push_back(observation);
    });

    depacketizer.push(makeH265Packet(1, 1000, true, 64));
    depacketizer.push(makeH265Packet(2, 2000, false, 16));

    const bool firstFrameOk =
        frames.size() >= 1U &&
        frames[0].pts == 1000 &&
        frames[0].keyFrame &&
        frames[0].discontinuity &&
        byteArrayContains(frames[0].payload, {0x00U, 0x00U, 0x00U, 0x01U, 0x40U}) &&
        byteArrayContains(frames[0].payload, {0x00U, 0x00U, 0x00U, 0x01U, 0x42U}) &&
        byteArrayContains(frames[0].payload, {0x00U, 0x00U, 0x00U, 0x01U, 0x44U}) &&
        byteArrayContains(frames[0].payload, {0x00U, 0x00U, 0x00U, 0x01U, 0x26U});
    const bool secondFrameOk =
        frames.size() >= 2U &&
        frames[1].pts == 2000 &&
        !frames[1].keyFrame &&
        !frames[1].discontinuity &&
        byteArrayContains(frames[1].payload, {0x00U, 0x00U, 0x00U, 0x01U, 0x02U});

    if (frames.size() != 2U || !firstFrameOk || !secondFrameOk) {
        std::cerr << "[H265 depacketizer aggregation] FAIL"
                  << " frameCount=" << frames.size()
                  << " firstFrameOk=" << (firstFrameOk ? 1 : 0)
                  << " secondFrameOk=" << (secondFrameOk ? 1 : 0)
                  << "\n";
        return false;
    }

    std::cout << "[H265 depacketizer aggregation] PASS\n";
    return true;
}

bool runPressureCooldownScenario() {
    SwRtspTrackGraph graph;
    SwRtspTrackGraph::VideoConfig config;
    config.codec = SwVideoPacket::Codec::H265;
    config.payloadType = 96;
    config.clockRate = 90000;
    config.liveTrimEnabled = true;
    config.latencyTargetMs = 120;
    graph.setVideoConfig(config);

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<VideoObservation> videoFrames;
    std::vector<SwMediaSource::RecoveryEvent::Kind> recoveryKinds;
    std::atomic<int> callbackCount(0);

    graph.setVideoPacketCallback([&](const SwVideoPacket& packet) {
        const int count = callbackCount.fetch_add(1) + 1;
        {
            std::lock_guard<std::mutex> lock(mutex);
            VideoObservation observation;
            observation.pts = packet.pts();
            observation.keyFrame = packet.isKeyFrame();
            observation.discontinuity = packet.isDiscontinuity();
            videoFrames.push_back(observation);
        }
        cv.notify_all();
        if (count <= 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(220));
        }
    });
    graph.setRecoveryCallback([&](SwMediaSource::RecoveryEvent::Kind kind, const SwString&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            recoveryKinds.push_back(kind);
        }
        cv.notify_all();
    });

    graph.start();

    graph.submitVideoPacket(makeH265Packet(1, 1000, true, 12 * 1024), false);
    const bool firstFrameObserved = waitForObservations(
        cv,
        mutex,
        [&]() {
            return !videoFrames.empty();
        },
        1000);
    if (!firstFrameObserved) {
        graph.stop();
        std::cerr << "[H265 pressure cooldown] timeout waiting for first frame\n";
        return false;
    }

    SwVideoSource::ConsumerPressure pressure;
    pressure.softPressure = true;
    pressure.queuedPackets = 4;
    pressure.queuedBytes = 4U * 12U * 1024U;
    graph.setConsumerPressure(pressure);
    for (uint16_t seq = 2; seq <= 7; ++seq) {
        const bool keyFrame = (seq == 7);
        graph.submitVideoPacket(makeH265Packet(seq,
                                               static_cast<uint32_t>(seq) * 1000U,
                                               keyFrame,
                                               12 * 1024),
                                false);
    }

    const bool firstRecoveryObserved = waitForObservations(
        cv,
        mutex,
        [&]() {
            return recoveryKinds.size() >= 1U;
        },
        1500);
    if (!firstRecoveryObserved) {
        graph.stop();
        std::cerr << "[H265 pressure cooldown] timeout waiting for first recovery\n";
        return false;
    }

    graph.setConsumerPressure(pressure);
    for (uint16_t seq = 8; seq <= 13; ++seq) {
        const bool keyFrame = (seq == 13);
        graph.submitVideoPacket(makeH265Packet(seq,
                                               static_cast<uint32_t>(seq) * 1000U,
                                               keyFrame,
                                               12 * 1024),
                                false);
    }

    const bool secondKeyFrameObserved = waitForObservations(
        cv,
        mutex,
        [&]() {
            if (recoveryKinds.size() > 1U) {
                return true;
            }
            for (std::size_t i = 0; i < videoFrames.size(); ++i) {
                if (videoFrames[i].pts == 13000) {
                    return true;
                }
            }
            return false;
        },
        2500);

    graph.stop();

    bool sawSecondKeyFrame = false;
    for (std::size_t i = 0; i < videoFrames.size(); ++i) {
        if (videoFrames[i].pts == 13000) {
            sawSecondKeyFrame = true;
            break;
        }
    }
    const bool keptSingleRecovery =
        recoveryKinds.size() == 1U &&
        recoveryKinds.front() == SwMediaSource::RecoveryEvent::Kind::LiveCut;
    if (!secondKeyFrameObserved || !keptSingleRecovery || !sawSecondKeyFrame) {
        std::cerr << "[H265 pressure cooldown] FAIL"
                  << " secondKeyFrameObserved=" << (secondKeyFrameObserved ? 1 : 0)
                  << " keptSingleRecovery=" << (keptSingleRecovery ? 1 : 0)
                  << " recoveryCount=" << recoveryKinds.size()
                  << " sawSecondKeyFrame=" << (sawSecondKeyFrame ? 1 : 0)
                  << "\n";
        return false;
    }

    std::cout << "[H265 pressure cooldown] PASS\n";
    return true;
}

bool runFragmentBurstNoFalsePressureScenario() {
    SwRtspTrackGraph graph;
    SwRtspTrackGraph::VideoConfig config;
    config.codec = SwVideoPacket::Codec::H265;
    config.payloadType = 96;
    config.clockRate = 90000;
    config.liveTrimEnabled = true;
    config.latencyTargetMs = 120;
    graph.setVideoConfig(config);

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<SwMediaSource::RecoveryEvent::Kind> recoveryKinds;
    std::atomic<int> frameCount(0);

    graph.setVideoPacketCallback([&](const SwVideoPacket&) {
        const int count = frameCount.fetch_add(1) + 1;
        cv.notify_all();
        if (count == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
        }
    });
    graph.setRecoveryCallback([&](SwMediaSource::RecoveryEvent::Kind kind, const SwString&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            recoveryKinds.push_back(kind);
        }
        cv.notify_all();
    });

    graph.start();
    graph.submitVideoPacket(makeH265Packet(1, 1000, true, 1200), false);
    const bool firstFrameObserved = waitForObservations(
        cv,
        mutex,
        [&]() {
            return frameCount.load() >= 1;
        },
        1000);
    if (!firstFrameObserved) {
        graph.stop();
        std::cerr << "[H265 fragment burst no false pressure] timeout waiting for first frame\n";
        return false;
    }

    for (uint16_t seq = 2; seq < 302; ++seq) {
        graph.submitVideoPacket(makeH265Packet(seq, 2000, false, 1200), false);
    }
    graph.submitVideoPacket(makeH265Packet(302, 3000, true, 1200), false);

    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    graph.stop();

    if (!recoveryKinds.empty()) {
        std::cerr << "[H265 fragment burst no false pressure] FAIL"
                  << " recoveryCount=" << recoveryKinds.size()
                  << "\n";
        return false;
    }

    std::cout << "[H265 fragment burst no false pressure] PASS\n";
    return true;
}

bool runPlatformDecoderRegistrationScenario() {
    const bool h264PlatformListed =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                   swPlatformVideoDecoderId(),
                                                   false);
    const bool h264PlatformHardwareListed =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                   swPlatformHardwareVideoDecoderId(),
                                                   false);
    const bool h264PlatformSoftwareListed =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                   swPlatformSoftwareVideoDecoderId(),
                                                   false);
    const bool h265PlatformListed =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                   swPlatformVideoDecoderId(),
                                                   false);
    const bool h265PlatformHardwareListed =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                   swPlatformHardwareVideoDecoderId(),
                                                   false);

    if (!h264PlatformListed || !h264PlatformHardwareListed || !h264PlatformSoftwareListed ||
        !h265PlatformListed || !h265PlatformHardwareListed) {
        std::cerr << "[Platform decoder registration] FAIL"
                  << " h264PlatformListed=" << (h264PlatformListed ? 1 : 0)
                  << " h264PlatformHardwareListed=" << (h264PlatformHardwareListed ? 1 : 0)
                  << " h264PlatformSoftwareListed=" << (h264PlatformSoftwareListed ? 1 : 0)
                  << " h265PlatformListed=" << (h265PlatformListed ? 1 : 0)
                  << " h265PlatformHardwareListed=" << (h265PlatformHardwareListed ? 1 : 0)
                  << "\n";
        return false;
    }

#if defined(_WIN32)
    const bool expectedH264PlatformAvailable = true;
    const bool expectedH264HardwareAvailable = true;
    const bool expectedH264SoftwareAvailable = true;
    const bool expectedH265PlatformAvailable = true;
    const bool expectedH265HardwareAvailable = true;
#elif defined(__linux__)
    const bool expectedH264PlatformAvailable = swLinuxH264DecoderRuntimeAvailable();
    const bool expectedH264HardwareAvailable = false;
    const bool expectedH264SoftwareAvailable = swLinuxOpenH264RuntimeAvailable();
    const bool expectedH265PlatformAvailable = swLinuxH265DecoderRuntimeAvailable();
    const bool expectedH265HardwareAvailable = false;
#else
    const bool expectedH264PlatformAvailable = false;
    const bool expectedH264HardwareAvailable = false;
    const bool expectedH264SoftwareAvailable = false;
    const bool expectedH265PlatformAvailable = false;
    const bool expectedH265HardwareAvailable = false;
#endif

    const bool h264PlatformAvailable =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                   swPlatformVideoDecoderId());
    const bool h264PlatformHardwareAvailable =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                   swPlatformHardwareVideoDecoderId());
    const bool h264PlatformSoftwareAvailable =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                   swPlatformSoftwareVideoDecoderId());
    const bool h265PlatformAvailable =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                   swPlatformVideoDecoderId());
    const bool h265PlatformHardwareAvailable =
        SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                   swPlatformHardwareVideoDecoderId());

    if (h264PlatformAvailable != expectedH264PlatformAvailable ||
        h264PlatformHardwareAvailable != expectedH264HardwareAvailable ||
        h264PlatformSoftwareAvailable != expectedH264SoftwareAvailable ||
        h265PlatformAvailable != expectedH265PlatformAvailable ||
        h265PlatformHardwareAvailable != expectedH265HardwareAvailable) {
        std::cerr << "[Platform decoder availability] FAIL"
                  << " h264PlatformAvailable=" << (h264PlatformAvailable ? 1 : 0)
                  << " expectedH264PlatformAvailable=" << (expectedH264PlatformAvailable ? 1 : 0)
                  << " h264PlatformHardwareAvailable=" << (h264PlatformHardwareAvailable ? 1 : 0)
                  << " expectedH264HardwareAvailable=" << (expectedH264HardwareAvailable ? 1 : 0)
                  << " h264PlatformSoftwareAvailable=" << (h264PlatformSoftwareAvailable ? 1 : 0)
                  << " expectedH264SoftwareAvailable=" << (expectedH264SoftwareAvailable ? 1 : 0)
                  << " h265PlatformAvailable=" << (h265PlatformAvailable ? 1 : 0)
                  << " expectedH265PlatformAvailable=" << (expectedH265PlatformAvailable ? 1 : 0)
                  << " h265PlatformHardwareAvailable=" << (h265PlatformHardwareAvailable ? 1 : 0)
                  << " expectedH265HardwareAvailable=" << (expectedH265HardwareAvailable ? 1 : 0)
                  << "\n";
        return false;
    }

    if (h264PlatformAvailable) {
        auto decoder =
            SwVideoDecoderFactory::instance().create(SwVideoPacket::Codec::H264,
                                                     swPlatformVideoDecoderId());
        if (!decoder || !decoder->open(SwVideoFormatInfo())) {
            std::cerr << "[Platform decoder creation] FAIL h264 auto open\n";
            return false;
        }
    }
    if (h264PlatformSoftwareAvailable) {
        auto decoder =
            SwVideoDecoderFactory::instance().create(SwVideoPacket::Codec::H264,
                                                     swPlatformSoftwareVideoDecoderId());
        if (!decoder || !decoder->open(SwVideoFormatInfo())) {
            std::cerr << "[Platform decoder creation] FAIL h264 software open\n";
            return false;
        }
    }
    if (h265PlatformAvailable) {
        auto decoder =
            SwVideoDecoderFactory::instance().create(SwVideoPacket::Codec::H265,
                                                     swPlatformVideoDecoderId());
        if (!decoder) {
            std::cerr << "[Platform decoder creation] FAIL h265 auto create\n";
            return false;
        }
    }

    std::cout << "[Platform decoder registration] PASS\n";
    return true;
}

} // namespace

int main() {
    bool ok = true;
    ok = runPlatformDecoderRegistrationScenario() && ok;
    ok = runH264InterleavedDepacketizerScenario() && ok;
    ok = runH264GapRecoveryScenario() && ok;
    ok = runH265AggregationDepacketizerScenario() && ok;
    ok = runScenario("H264 GOP jump", SwVideoPacket::Codec::H264, makeH264Packet) && ok;
    ok = runScenario("H265 GOP jump", SwVideoPacket::Codec::H265, makeH265Packet) && ok;
    ok = runScenario("H265 consumer pressure jump",
                     SwVideoPacket::Codec::H265,
                     makeH265Packet,
                     true) &&
         ok;
    ok = runPressureCooldownScenario() && ok;
    ok = runFragmentBurstNoFalsePressureScenario() && ok;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
