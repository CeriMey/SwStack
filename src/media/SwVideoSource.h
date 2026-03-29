/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

/**
 * @file src/media/SwVideoSource.h
 * @ingroup media
 * @brief Declares the public interface exposed by SwVideoSource in the CoreSw media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the video source interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwVideoSource and SwVideoPipeline.
 *
 * Source-oriented declarations here describe how data or media is produced over time, how
 * consumers observe availability, and which lifetime guarantees apply to delivered payloads.
 *
 * Media-facing declarations here focus on packet and frame ownership, format description,
 * decoding boundaries, and real-time source control.
 *
 */


#include "media/SwMediaSource.h"
#include "media/SwVideoDecoder.h"
#include "media/SwVideoPacket.h"
#include "SwDebug.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include "core/types/SwString.h"
#include "core/fs/SwMutex.h"
static constexpr const char* kSwLogCategory_SwVideoPipeline = "sw.media.swvideopipeline";

class SwVideoSource : public SwMediaSource {
public:
    using StreamState = SwMediaSource::StreamState;
    using StreamStatus = SwMediaSource::StreamStatus;

    using PacketCallback = std::function<void(const SwVideoPacket&)>;
    using StatusCallback = SwMediaSource::StatusCallback;

    /**
     * @brief Destroys the `SwVideoSource` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwVideoSource() = default;

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwString name() const = 0;
    /**
     * @brief Returns the current start.
     * @return The current start.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void start() = 0;
    /**
     * @brief Returns the current stop.
     * @return The current stop.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void stop() = 0;

    /**
     * @brief Sets the packet Callback.
     * @param callback Callback invoked by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPacketCallback(PacketCallback callback) {
        SwMutexLocker lock(m_callbackMutex);
        m_packetCallback = std::move(callback);
    }

protected:
    /**
     * @brief Performs the `emitPacket` operation.
     * @param packet Value passed to the method.
     */
    void emitPacket(const SwVideoPacket& packet) {
        PacketCallback cb;
        {
            SwMutexLocker lock(m_callbackMutex);
            cb = m_packetCallback;
        }
        if (cb) {
            cb(packet);
        }
        emitMediaPacket(SwMediaPacket::fromVideoPacket(packet));
    }

private:
    SwMutex m_callbackMutex;
    PacketCallback m_packetCallback;
};

class SwVideoPipeline : public std::enable_shared_from_this<SwVideoPipeline> {
public:
    ~SwVideoPipeline() {
        stopWorker();
    }

    /**
     * @brief Sets the source.
     * @param source Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSource(const std::shared_ptr<SwVideoSource>& source) {
        std::shared_ptr<SwVideoSource> previousSource;
        bool restartSource = false;
        {
            SwMutexLocker lock(m_mutex);
            if (m_source == source) {
                return;
            }
            previousSource = m_source;
            m_source = source;
            restartSource = m_started;
        }
        if (previousSource) {
            previousSource->setPacketCallback(SwVideoSource::PacketCallback());
            if (restartSource) {
                previousSource->stop();
            }
        }
        if (source && restartSource) {
            source->setPacketCallback(makePacketCallback());
            source->start();
        }
    }

    /**
     * @brief Sets the decoder.
     * @param decoder Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDecoder(const std::shared_ptr<SwVideoDecoder>& decoder) {
        SwMutexLocker lock(m_mutex);
        m_decoder = decoder;
        m_decoderCodec = SwVideoPacket::Codec::Unknown;
        m_autoDecoderEnabled = false;
        m_waitingForDecoderSync.store(m_decoder != nullptr);
        m_loggedWaitingForDecoderSync.store(false);
        m_loggedFirstPacketToDecoder.store(false);
        m_packetsSinceLastDecodedFrame.store(0);
        m_lastDecodedFrameTickMs.store(0);
        if (m_decoder && m_frameCallback) {
            m_decoder->setFrameCallback(makeDecoderFrameCallbackLocked_());
        }
    }

    /**
     * @brief Sets the decoder Hint.
     * @param codec Value passed to the method.
     * @param decoder Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDecoderHint(SwVideoPacket::Codec codec, const std::shared_ptr<SwVideoDecoder>& decoder) {
        SwMutexLocker lock(m_mutex);
        m_decoder = decoder;
        m_decoderCodec = codec;
        m_decoderSelectionIds.erase(codec);
        m_autoDecoderEnabled = true;
        m_waitingForDecoderSync.store(m_decoder != nullptr);
        m_loggedWaitingForDecoderSync.store(false);
        m_loggedFirstPacketToDecoder.store(false);
        m_packetsSinceLastDecodedFrame.store(0);
        m_lastDecodedFrameTickMs.store(0);
        if (m_decoder && m_frameCallback) {
            m_decoder->setFrameCallback(makeDecoderFrameCallbackLocked_());
        }
    }

    /**
     * @brief Selects a registered decoder backend for the given codec.
     * @param codec Value passed to the method.
     * @param decoderId Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool setDecoderSelection(SwVideoPacket::Codec codec, const SwString& decoderId) {
        if (decoderId.isEmpty()) {
            clearDecoderSelection(codec);
            return true;
        }
        if (!SwVideoDecoderFactory::instance().contains(codec, decoderId)) {
            return false;
        }

        std::shared_ptr<SwVideoDecoder> decoderToFlush;
        {
            SwMutexLocker lock(m_mutex);
            m_decoderSelectionIds[codec] = decoderId;
            m_autoDecoderEnabled = true;
            decoderToFlush = m_decoder;
            m_decoder.reset();
            m_decoderCodec = SwVideoPacket::Codec::Unknown;
            m_loggedFirstPacketToDecoder.store(false);
            m_waitingForDecoderSync.store(true);
            m_loggedWaitingForDecoderSync.store(false);
        }
        if (decoderToFlush) {
            decoderToFlush->flush();
        }
        return true;
    }

    /**
     * @brief Clears a codec-specific decoder backend selection.
     * @param codec Value passed to the method.
     */
    void clearDecoderSelection(SwVideoPacket::Codec codec) {
        std::shared_ptr<SwVideoDecoder> decoderToFlush;
        {
            SwMutexLocker lock(m_mutex);
            m_decoderSelectionIds.erase(codec);
            if (m_decoderCodec == codec) {
                decoderToFlush = m_decoder;
                m_decoder.reset();
                m_decoderCodec = SwVideoPacket::Codec::Unknown;
                m_loggedFirstPacketToDecoder.store(false);
                m_waitingForDecoderSync.store(true);
                m_loggedWaitingForDecoderSync.store(false);
            }
        }
        if (decoderToFlush) {
            decoderToFlush->flush();
        }
    }

    /**
     * @brief Returns the currently preferred decoder backend id for the given codec.
     * @param codec Value passed to the method.
     * @return The preferred decoder id, if any.
     */
    SwString decoderSelection(SwVideoPacket::Codec codec) const {
        SwMutexLocker lock(m_mutex);
        auto it = m_decoderSelectionIds.find(codec);
        if (it == m_decoderSelectionIds.end()) {
            return SwString();
        }
        return it->second;
    }

    /**
     * @brief Sets the frame Callback.
     * @param cb Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFrameCallback(SwVideoDecoder::FrameCallback cb) {
        SwMutexLocker lock(m_mutex);
        m_frameCallback = std::move(cb);
        if (m_decoder) {
            m_decoder->setFrameCallback(makeDecoderFrameCallbackLocked_());
        }
    }

    /**
     * @brief Performs the `useDecoderFactory` operation.
     * @param enabled Value passed to the method.
     */
    void useDecoderFactory(bool enabled) {
        SwMutexLocker lock(m_mutex);
        m_autoDecoderEnabled = enabled;
    }

    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() {
        std::shared_ptr<SwVideoSource> source;
        {
            SwMutexLocker lock(m_mutex);
            source = m_source;
            m_started = true;
        }
        if (!source) {
            return;
        }

        if (m_asyncDecodeEnabled.load()) {
            startWorker();
        }
        source->setPacketCallback(makePacketCallback());
        source->start();
    }

    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() {
        std::shared_ptr<SwVideoSource> source;
        std::shared_ptr<SwVideoDecoder> decoder;
        {
            SwMutexLocker lock(m_mutex);
            source = m_source;
            decoder = m_decoder;
            m_started = false;
        }
        if (source) {
            source->setPacketCallback(SwVideoSource::PacketCallback());
            source->stop();
        }
        if (m_asyncDecodeEnabled.load()) {
            stopWorker();
        }
        if (decoder) {
            decoder->flush();
        }
        m_waitingForDecoderSync.store(false);
        m_loggedWaitingForDecoderSync.store(false);
        m_loggedFirstPacketToDecoder.store(false);
        m_packetsSinceLastDecodedFrame.store(0);
        m_lastDecodedFrameTickMs.store(0);
    }

    void setQueueLimits(std::size_t maxPackets, std::size_t maxBytes) {
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            m_maxQueuedPackets = (maxPackets == 0) ? 1 : maxPackets;
            m_maxQueuedBytes = (maxBytes == 0) ? 1 : maxBytes;
            trimQueueLocked();
        }
        m_queueCv.notify_one();
    }

    void setAsyncDecode(bool enabled) {
        m_asyncDecodeEnabled.store(enabled);
    }

private:
    SwVideoSource::PacketCallback makePacketCallback() {
        std::weak_ptr<SwVideoPipeline> weakSelf = shared_from_this();
        return [weakSelf](const SwVideoPacket& packet) {
            if (auto self = weakSelf.lock()) {
                if (self->m_asyncDecodeEnabled.load()) {
                    self->enqueuePacket(packet);
                } else {
                    self->handlePacket(packet);
                }
            }
        };
    }

    bool emitRawFrameDirect(const SwVideoPacket& packet) {
        if (!packet.carriesRawFrame() || packet.payload().isEmpty()) {
            return false;
        }

        SwVideoDecoder::FrameCallback frameCallback;
        {
            SwMutexLocker lock(m_mutex);
            frameCallback = m_frameCallback;
        }
        if (!frameCallback) {
            return false;
        }

        const auto& format = packet.rawFormat();
        SwVideoFrame frame =
            SwVideoFrame::fromCopy(format.width,
                                   format.height,
                                   format.format,
                                   packet.payload().constData(),
                                   static_cast<std::size_t>(packet.payload().size()));
        if (!frame.isValid()) {
            return false;
        }
        frame.setTimestamp(packet.pts());
        frameCallback(frame);
        return true;
    }

    void startWorker() {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        if (m_workerRunning) {
            return;
        }
        m_stopWorker = false;
        m_workerRunning = true;
        m_workerThread = std::thread([this]() { workerLoop(); });
    }

    void stopWorker() {
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            m_stopWorker = true;
        }
        m_queueCv.notify_all();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            m_packetQueue.clear();
            m_queuedBytes = 0;
            m_workerRunning = false;
            m_stopWorker = false;
        }
    }

    void enqueuePacket(const SwVideoPacket& packet) {
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            if (!m_workerRunning) {
                return;
            }
            m_packetQueue.push_back(packet);
            m_queuedBytes += static_cast<std::size_t>(packet.payload().size());
            trimQueueLocked();
        }
        m_queueCv.notify_one();
    }

    void trimQueueLocked() {
        if (m_packetQueue.empty()) {
            m_queuedBytes = 0;
            return;
        }
        if (m_packetQueue.size() <= m_maxQueuedPackets && m_queuedBytes <= m_maxQueuedBytes) {
            return;
        }

        std::size_t keepFrom = m_packetQueue.size() - 1;
        for (std::size_t i = m_packetQueue.size(); i > 0; --i) {
            if (m_packetQueue[i - 1].isKeyFrame()) {
                keepFrom = i - 1;
                break;
            }
        }

        std::deque<SwVideoPacket> trimmed;
        std::size_t trimmedBytes = 0;
        for (std::size_t i = keepFrom; i < m_packetQueue.size(); ++i) {
            trimmed.push_back(m_packetQueue[i]);
            trimmedBytes += static_cast<std::size_t>(m_packetQueue[i].payload().size());
        }
        if (trimmed.empty()) {
            trimmed.push_back(m_packetQueue.back());
            trimmedBytes = static_cast<std::size_t>(m_packetQueue.back().payload().size());
        }
        m_packetQueue.swap(trimmed);
        m_queuedBytes = trimmedBytes;

        while (m_packetQueue.size() > m_maxQueuedPackets || m_queuedBytes > m_maxQueuedBytes) {
            m_queuedBytes -= static_cast<std::size_t>(m_packetQueue.front().payload().size());
            m_packetQueue.pop_front();
        }
    }

    void workerLoop() {
        while (true) {
            SwVideoPacket packet;
            {
                std::unique_lock<std::mutex> queueLock(m_queueMutex);
                m_queueCv.wait(queueLock, [this]() {
                    return m_stopWorker || !m_packetQueue.empty();
                });
                if (m_stopWorker && m_packetQueue.empty()) {
                    break;
                }
                packet = std::move(m_packetQueue.front());
                m_queuedBytes -= static_cast<std::size_t>(packet.payload().size());
                m_packetQueue.pop_front();
            }
            handlePacket(packet);
        }
    }

    void handlePacket(const SwVideoPacket& packet) {
        if (packet.carriesRawFrame()) {
            emitRawFrameDirect(packet);
            return;
        }

        std::shared_ptr<SwVideoDecoder> decoder;
        std::shared_ptr<SwVideoDecoder> decoderToFlush;
        SwString preferredDecoderId;
        const int64_t nowTickMs = monotonicMs_();
        {
            SwMutexLocker lock(m_mutex);
            m_lastPacketTickMs.store(nowTickMs);
            decoder = m_decoder;
            if (decoder && m_autoDecoderEnabled &&
                m_decoderCodec != SwVideoPacket::Codec::Unknown &&
                m_decoderCodec != packet.codec()) {
                decoder.reset();
                m_decoder.reset();
                m_decoderCodec = SwVideoPacket::Codec::Unknown;
                m_loggedFirstPacketToDecoder.store(false);
                m_packetsSinceLastDecodedFrame.store(0);
                m_lastDecodedFrameTickMs.store(0);
            }
            if (decoder && !m_waitingForDecoderSync.load()) {
                const uint64_t packetsWithoutFrame = m_packetsSinceLastDecodedFrame.fetch_add(1) + 1;
                const int64_t lastDecodedTickMs = m_lastDecodedFrameTickMs.load();
                const int64_t stalledForMs =
                    (lastDecodedTickMs > 0) ? (nowTickMs - lastDecodedTickMs) : 0;
                const uint64_t packetThreshold =
                    packet.isDiscontinuity() ? (m_decoderStallPacketThreshold / 2)
                                             : m_decoderStallPacketThreshold;
                if (lastDecodedTickMs > 0 &&
                    stalledForMs >= m_decoderStallThresholdMs &&
                    packetsWithoutFrame >= std::max<uint64_t>(1, packetThreshold)) {
                    decoderToFlush = decoder;
                    decoder.reset();
                    m_decoder.reset();
                    m_decoderCodec = SwVideoPacket::Codec::Unknown;
                    m_loggedFirstPacketToDecoder.store(false);
                    m_waitingForDecoderSync.store(true);
                    m_loggedWaitingForDecoderSync.store(false);
                    m_packetsSinceLastDecodedFrame.store(0);
                    m_lastDecodedFrameTickMs.store(0);
                    swCWarning(kSwLogCategory_SwVideoPipeline)
                        << "[SwVideoPipeline] Decoder stalled for " << stalledForMs
                        << " ms after " << packetsWithoutFrame
                        << " packets, forcing decoder resync"
                        << " codec=" << static_cast<int>(packet.codec())
                        << " discontinuity=" << (packet.isDiscontinuity() ? 1 : 0);
                }
            }
            if (!decoder && m_autoDecoderEnabled) {
                auto selectionIt = m_decoderSelectionIds.find(packet.codec());
                if (selectionIt != m_decoderSelectionIds.end()) {
                    preferredDecoderId = selectionIt->second;
                }
                if (!preferredDecoderId.isEmpty()) {
                    decoder = SwVideoDecoderFactory::instance().acquire(packet.codec(),
                                                                        preferredDecoderId);
                    if (!decoder) {
                        swCWarning(kSwLogCategory_SwVideoPipeline)
                            << "[SwVideoPipeline] Preferred decoder unavailable codec="
                            << static_cast<int>(packet.codec()) << " id=" << preferredDecoderId
                            << ", falling back to automatic selection";
                    }
                }
                if (!decoder) {
                    decoder = SwVideoDecoderFactory::instance().acquire(packet.codec());
                }
                if (decoder) {
                    if (m_frameCallback) {
                        decoder->setFrameCallback(makeDecoderFrameCallbackLocked_());
                    }
                    m_decoder = decoder;
                    m_decoderCodec = packet.codec();
                    m_waitingForDecoderSync.store(true);
                    m_loggedWaitingForDecoderSync.store(false);
                    m_loggedFirstPacketToDecoder.store(false);
                    m_packetsSinceLastDecodedFrame.store(0);
                    m_lastDecodedFrameTickMs.store(0);
                    swCWarning(kSwLogCategory_SwVideoPipeline) << "[SwVideoPipeline] Decoder selected codec="
                                << static_cast<int>(packet.codec())
                                << " name=" << decoder->name()
                                << " id=" << (preferredDecoderId.isEmpty() ? SwString("auto")
                                                                            : preferredDecoderId);
                }
            }
        }
        if (decoderToFlush) {
            decoderToFlush->flush();
        }
        if (decoder) {
            if (m_waitingForDecoderSync.load() && !packet.isKeyFrame()) {
                if (!m_loggedWaitingForDecoderSync.exchange(true)) {
                    swCWarning(kSwLogCategory_SwVideoPipeline)
                        << "[SwVideoPipeline] Dropping packet while waiting for decoder sync"
                        << " codec=" << static_cast<int>(packet.codec())
                        << " bytes=" << packet.payload().size();
                }
                return;
            }
            SwVideoPacket packetToFeed = packet;
            if (m_waitingForDecoderSync.exchange(false)) {
                packetToFeed.setDiscontinuity(true);
                m_loggedWaitingForDecoderSync.store(false);
            }
            if (!m_loggedFirstPacketToDecoder.exchange(true)) {
                swCWarning(kSwLogCategory_SwVideoPipeline) << "[SwVideoPipeline] First packet to decoder "
                            << " codec=" << static_cast<int>(packet.codec())
                            << " bytes=" << packetToFeed.payload().size()
                            << " key=" << (packetToFeed.isKeyFrame() ? 1 : 0);
            }
            if (!decoder->feed(packetToFeed)) {
                swCWarning(kSwLogCategory_SwVideoPipeline) << "[SwVideoPipeline] Decoder feed failed "
                            << decoder->name()
                            << " codec=" << static_cast<int>(packetToFeed.codec())
                            << " bytes=" << packetToFeed.payload().size();
                m_waitingForDecoderSync.store(true);
                m_loggedWaitingForDecoderSync.store(false);
                m_loggedFirstPacketToDecoder.store(false);
                m_packetsSinceLastDecodedFrame.store(0);
                m_lastDecodedFrameTickMs.store(0);
            }
        }
    }

    static int64_t monotonicMs_() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    SwVideoDecoder::FrameCallback makeDecoderFrameCallbackLocked_() {
        SwVideoDecoder::FrameCallback downstream = m_frameCallback;
        return [this, downstream](const SwVideoFrame& frame) {
            m_lastDecodedFrameTickMs.store(monotonicMs_());
            m_packetsSinceLastDecodedFrame.store(0);
            if (downstream) {
                downstream(frame);
            }
        };
    }

    mutable SwMutex m_mutex;
    std::shared_ptr<SwVideoSource> m_source;
    std::shared_ptr<SwVideoDecoder> m_decoder;
    SwVideoPacket::Codec m_decoderCodec{SwVideoPacket::Codec::Unknown};
    SwVideoDecoder::FrameCallback m_frameCallback;
    bool m_autoDecoderEnabled{true};
    std::map<SwVideoPacket::Codec, SwString> m_decoderSelectionIds;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<SwVideoPacket> m_packetQueue;
    std::thread m_workerThread;
    bool m_stopWorker{false};
    bool m_workerRunning{false};
    std::size_t m_queuedBytes{0};
    std::size_t m_maxQueuedPackets{24};
    std::size_t m_maxQueuedBytes{4 * 1024 * 1024};
    std::atomic<bool> m_loggedFirstPacketToDecoder{false};
    std::atomic<bool> m_asyncDecodeEnabled{false};
    std::atomic<bool> m_waitingForDecoderSync{false};
    std::atomic<bool> m_loggedWaitingForDecoderSync{false};
    std::atomic<int64_t> m_lastPacketTickMs{0};
    std::atomic<int64_t> m_lastDecodedFrameTickMs{0};
    std::atomic<uint64_t> m_packetsSinceLastDecodedFrame{0};
    int64_t m_decoderStallThresholdMs{1500};
    uint64_t m_decoderStallPacketThreshold{18};
    bool m_started{false};
};
