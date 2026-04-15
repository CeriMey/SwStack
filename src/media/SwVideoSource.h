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
#include "media/SwPlatformVideoDecoderIds.h"
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

    struct ConsumerPressure {
        std::size_t queuedPackets{0};
        std::size_t queuedBytes{0};
        bool softPressure{false};
        bool hardPressure{false};
        bool decoderStalled{false};
        std::int64_t stalledForMs{0};
        uint64_t packetsWithoutFrame{0};
    };

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

    void setConsumerPressure(const ConsumerPressure& pressure) {
        bool changed = false;
        {
            SwMutexLocker lock(m_consumerPressureMutex);
            changed =
                (m_consumerPressure.queuedPackets != pressure.queuedPackets) ||
                (m_consumerPressure.queuedBytes != pressure.queuedBytes) ||
                (m_consumerPressure.softPressure != pressure.softPressure) ||
                (m_consumerPressure.hardPressure != pressure.hardPressure) ||
                (m_consumerPressure.decoderStalled != pressure.decoderStalled) ||
                (m_consumerPressure.stalledForMs != pressure.stalledForMs) ||
                (m_consumerPressure.packetsWithoutFrame != pressure.packetsWithoutFrame);
            m_consumerPressure = pressure;
        }
        if (changed) {
            handleConsumerPressureChanged(pressure);
        }
    }

    ConsumerPressure consumerPressure() const {
        SwMutexLocker lock(m_consumerPressureMutex);
        return m_consumerPressure;
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
        if (m_mirrorVideoPacketsToMediaCallback && hasMediaPacketCallback()) {
            emitMediaPacket(SwMediaPacket::fromVideoPacket(packet));
        }
    }

    virtual void handleConsumerPressureChanged(const ConsumerPressure& pressure) {
        SW_UNUSED(pressure);
    }

public:
    void setMirrorVideoPacketsToMediaCallback(bool enabled) {
        m_mirrorVideoPacketsToMediaCallback = enabled;
    }

    bool mirrorVideoPacketsToMediaCallback() const {
        return m_mirrorVideoPacketsToMediaCallback;
    }

private:
    SwMutex m_callbackMutex;
    PacketCallback m_packetCallback;
    mutable SwMutex m_consumerPressureMutex;
    ConsumerPressure m_consumerPressure{};
    bool m_mirrorVideoPacketsToMediaCallback{false};
};

class SwVideoPipeline : public std::enable_shared_from_this<SwVideoPipeline> {
public:
    struct QueuedVideoPacket {
        SwVideoPacket packet{};
        uint64_t recoveryEpoch{0};
    };

    struct DecoderRoutingState {
        SwString temporaryOverrideDecoderId{};
        bool retryPrimaryOnNextRecoveryKeyFrame{false};
        bool retryAttemptedThisCycle{false};
        bool temporaryOverrideBridged{false};
        bool primaryRetryEligible{false};
        uint64_t bridgeRecoveryEpoch{0};
        bool pendingPrimaryRetry{false};
        uint64_t softwareStableDecodedFrames{0};
        int64_t softwareStableSinceTickMs{0};
    };

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
            previousSource->setConsumerPressure(SwVideoSource::ConsumerPressure());
            if (restartSource) {
                previousSource->stop();
            }
        }
        if (source && restartSource) {
            source->setPacketCallback(makePacketCallback());
            source->start();
        }
        publishConsumerPressure_();
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
        m_activeDecoderId = inferredDecoderIdForInstanceLocked_(decoder);
        m_autoDecoderEnabled = false;
        m_decoderRoutingStates.clear();
        clearResolvedDecoderSelectionsLocked_();
        clearDecoderRecoveryRequestLocked_();
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
        m_activeDecoderId = inferredDecoderIdForInstanceLocked_(decoder);
        m_decoderSelectionIds.erase(codec);
        m_autoDecoderEnabled = true;
        m_decoderRoutingStates.erase(codec);
        clearResolvedDecoderSelectionLocked_(codec);
        clearDecoderRecoveryRequestLocked_();
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
            m_decoderRoutingStates.erase(codec);
            clearResolvedDecoderSelectionLocked_(codec);
            clearDecoderRecoveryRequestLocked_();
            m_autoDecoderEnabled = true;
            decoderToFlush = m_decoder;
            m_decoder.reset();
            m_decoderCodec = SwVideoPacket::Codec::Unknown;
            m_activeDecoderId.clear();
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
            m_decoderRoutingStates.erase(codec);
            clearResolvedDecoderSelectionLocked_(codec);
            clearDecoderRecoveryRequestLocked_();
            if (m_decoderCodec == codec) {
                decoderToFlush = m_decoder;
                m_decoder.reset();
                m_decoderCodec = SwVideoPacket::Codec::Unknown;
                m_activeDecoderId.clear();
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
        publishConsumerPressure_();
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
            m_decoderRoutingStates.clear();
            clearResolvedDecoderSelectionsLocked_();
            m_activeDecoderId.clear();
            clearDecoderRecoveryRequestLocked_();
        }
        if (source) {
            source->setPacketCallback(SwVideoSource::PacketCallback());
            source->setConsumerPressure(SwVideoSource::ConsumerPressure());
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
        publishConsumerPressure_();
    }

    void setQueueLimits(std::size_t maxPackets, std::size_t maxBytes) {
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            m_maxQueuedPackets = maxPackets;
            m_maxQueuedBytes = maxBytes;
            updateQueuePressureLocked_();
        }
        publishConsumerPressure_();
        m_queueCv.notify_one();
    }

    void setAsyncDecode(bool enabled) {
        m_asyncDecodeEnabled.store(enabled);
    }

    void setRuntimeDecoderRerouteEnabled(bool enabled) {
        m_runtimeDecoderRerouteEnabled.store(enabled);
        if (!enabled) {
            SwMutexLocker lock(m_mutex);
            m_decoderRoutingStates.clear();
        }
    }

    void setDecoderStallRecoveryEnabled(bool enabled) {
        m_decoderStallRecoveryEnabled.store(enabled);
        publishConsumerPressure_();
    }

    void recoverLiveEdge(const SwMediaSource::RecoveryEvent& event) {
        std::shared_ptr<SwVideoDecoder> decoderToFlush;
        {
            SwMutexLocker lock(m_mutex);
            m_recoveryEpoch.store(event.epoch);
            m_decoderFrameEpoch.store(event.epoch);
            decoderToFlush = m_decoder;
            clearDecoderRecoveryRequestLocked_();
            for (auto& entry : m_decoderRoutingStates) {
                entry.second.retryAttemptedThisCycle = false;
                entry.second.pendingPrimaryRetry = false;
            }
            m_waitingForDecoderSync.store(true);
            m_loggedWaitingForDecoderSync.store(false);
            m_loggedFirstPacketToDecoder.store(false);
            m_packetsSinceLastDecodedFrame.store(0);
            m_lastPacketTickMs.store(0);
            m_lastDecodedFrameTickMs.store(0);
        }
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            m_packetQueue.clear();
            m_queuedBytes = 0;
            updateQueuePressureLocked_();
        }
        if (decoderToFlush) {
            decoderToFlush->flush();
        }
        swCWarning(kSwLogCategory_SwVideoPipeline)
            << "[SwVideoPipeline] Coordinated live-edge recover"
            << " epoch=" << event.epoch
            << " kind=" << static_cast<int>(event.kind)
            << " reason=" << event.reason;
        publishConsumerPressure_();
        m_queueCv.notify_all();
    }

private:
    static bool isHardwareDecoderId_(const SwString& decoderId) {
        return swIsPlatformHardwareVideoDecoderId(decoderId);
    }

    static bool isSoftwareDecoderId_(const SwString& decoderId) {
        return swIsPlatformSoftwareVideoDecoderId(decoderId);
    }

    static bool isAutoPlatformDecoderId_(const SwString& decoderId) {
        return swIsPlatformAutoVideoDecoderId(decoderId);
    }

    static bool usesMediaFoundationFixedStartupResolution_(SwVideoPacket::Codec codec) {
        return codec == SwVideoPacket::Codec::H264 ||
               codec == SwVideoPacket::Codec::H265;
    }

    static bool isRecoveryKeyFrame_(const SwVideoPacket& packet) {
        return packet.isKeyFrame() && packet.isDiscontinuity();
    }

    static constexpr uint64_t kSoftwareBridgeRetryStableFrameThreshold_ = 30U;
    static constexpr int64_t kSoftwareBridgeRetryStableMs_ = 2000;

    SwVideoSource::PacketCallback makePacketCallback() {
        std::weak_ptr<SwVideoPipeline> weakSelf = shared_from_this();
        return [weakSelf](const SwVideoPacket& packet) {
            if (auto self = weakSelf.lock()) {
                const uint64_t recoveryEpoch = self->m_recoveryEpoch.load();
                if (self->m_asyncDecodeEnabled.load()) {
                    self->enqueuePacket(packet, recoveryEpoch);
                } else {
                    self->handlePacket(packet, recoveryEpoch);
                }
            }
        };
    }

    SwString inferredDecoderIdForInstanceLocked_(const std::shared_ptr<SwVideoDecoder>& decoder) const {
        if (!decoder) {
            return SwString();
        }
        const SwString decoderName(decoder->name());
        if (decoderName.contains("DecoderHW")) {
            return swPlatformHardwareVideoDecoderId();
        }
        if (decoderName.contains("DecoderSW")) {
            return swPlatformSoftwareVideoDecoderId();
        }
        return "auto";
    }

    SwString primaryDecoderSelectionLocked_(SwVideoPacket::Codec codec) const {
        auto it = m_decoderSelectionIds.find(codec);
        if (it == m_decoderSelectionIds.end()) {
            return SwString();
        }
        return it->second;
    }

    SwString probeConcreteDecoderSelectionLocked_(SwVideoPacket::Codec codec,
                                                  const SwString& requestedDecoderId) const {
        const SwString hardwareDecoderId = swPlatformHardwareVideoDecoderId();
        if (SwVideoDecoderFactory::instance().contains(codec, hardwareDecoderId)) {
            std::shared_ptr<SwVideoDecoder> decoder =
                SwVideoDecoderFactory::instance().create(codec, hardwareDecoderId);
            if (decoder && decoder->open(SwVideoFormatInfo())) {
                decoder->flush();
                swCWarning(kSwLogCategory_SwVideoPipeline)
                    << "[SwVideoPipeline] Resolved startup decoder"
                    << " codec=" << static_cast<int>(codec)
                    << " requested=" << (requestedDecoderId.isEmpty() ? SwString("auto")
                                                                       : requestedDecoderId)
                    << " id=" << hardwareDecoderId;
                return hardwareDecoderId;
            }
        }

        const SwString softwareDecoderId = swPlatformSoftwareVideoDecoderId();
        if (SwVideoDecoderFactory::instance().contains(codec, softwareDecoderId)) {
            std::shared_ptr<SwVideoDecoder> decoder =
                SwVideoDecoderFactory::instance().create(codec, softwareDecoderId);
            if (decoder && decoder->open(SwVideoFormatInfo())) {
                decoder->flush();
                swCWarning(kSwLogCategory_SwVideoPipeline)
                    << "[SwVideoPipeline] Resolved startup decoder"
                    << " codec=" << static_cast<int>(codec)
                    << " requested=" << (requestedDecoderId.isEmpty() ? SwString("auto")
                                                                       : requestedDecoderId)
                    << " id=" << softwareDecoderId;
                return softwareDecoderId;
            }
        }

        return SwString();
    }

    SwString resolvedDecoderSelectionLocked_(SwVideoPacket::Codec codec) {
        auto resolvedIt = m_resolvedDecoderSelectionIds.find(codec);
        if (resolvedIt != m_resolvedDecoderSelectionIds.end()) {
            return resolvedIt->second;
        }

        const SwString primaryDecoderId = primaryDecoderSelectionLocked_(codec);
        SwString resolvedDecoderId = primaryDecoderId;
        if (usesMediaFoundationFixedStartupResolution_(codec) &&
            isAutoPlatformDecoderId_(primaryDecoderId)) {
            resolvedDecoderId = probeConcreteDecoderSelectionLocked_(codec, primaryDecoderId);
        }
        if (!resolvedDecoderId.isEmpty()) {
            m_resolvedDecoderSelectionIds[codec] = resolvedDecoderId;
        }
        return resolvedDecoderId;
    }

    SwString effectiveDecoderSelectionLocked_(SwVideoPacket::Codec codec) {
        if (m_runtimeDecoderRerouteEnabled.load()) {
            auto stateIt = m_decoderRoutingStates.find(codec);
            if (stateIt != m_decoderRoutingStates.end() &&
                !stateIt->second.temporaryOverrideDecoderId.isEmpty()) {
                return stateIt->second.temporaryOverrideDecoderId;
            }
        }
        return resolvedDecoderSelectionLocked_(codec);
    }

    void clearDecoderRecoveryRequestLocked_() {
        m_decoderRecoveryRequested.store(false);
        m_decoderRecoveryReason.clear();
    }

    void requestDecoderRecoveryLocked_(const SwString& reason) {
        m_decoderRecoveryRequested.store(true);
        m_decoderRecoveryReason = reason;
    }

    void clearResolvedDecoderSelectionLocked_(SwVideoPacket::Codec codec) {
        m_resolvedDecoderSelectionIds.erase(codec);
    }

    void clearResolvedDecoderSelectionsLocked_() {
        m_resolvedDecoderSelectionIds.clear();
    }

    void resetDecoderSelectionStateLocked_() {
        m_decoder.reset();
        m_decoderCodec = SwVideoPacket::Codec::Unknown;
        m_activeDecoderId.clear();
        m_loggedFirstPacketToDecoder.store(false);
        m_waitingForDecoderSync.store(true);
        m_loggedWaitingForDecoderSync.store(false);
        m_packetsSinceLastDecodedFrame.store(0);
        m_lastDecodedFrameTickMs.store(0);
    }

    void activateTemporaryDecoderOverrideLocked_(SwVideoPacket::Codec codec,
                                                 const SwString& reason,
                                                 bool markRetryAttempted) {
        if (!m_runtimeDecoderRerouteEnabled.load()) {
            requestDecoderRecoveryLocked_(reason);
            return;
        }
        if (codec != SwVideoPacket::Codec::H265) {
            requestDecoderRecoveryLocked_(reason);
            return;
        }
        DecoderRoutingState& state = m_decoderRoutingStates[codec];
        state.temporaryOverrideDecoderId = swPlatformSoftwareVideoDecoderId();
        state.retryPrimaryOnNextRecoveryKeyFrame = true;
        state.pendingPrimaryRetry = false;
        state.temporaryOverrideBridged = false;
        state.primaryRetryEligible = false;
        state.bridgeRecoveryEpoch = 0;
        state.softwareStableDecodedFrames = 0;
        state.softwareStableSinceTickMs = 0;
        if (markRetryAttempted) {
            state.retryAttemptedThisCycle = true;
        }
        requestDecoderRecoveryLocked_(reason);
        swCWarning(kSwLogCategory_SwVideoPipeline)
            << "[SwVideoPipeline] Activating temporary software decoder bridge"
            << " codec=" << static_cast<int>(codec)
            << " primary=" << primaryDecoderSelectionLocked_(codec)
            << " reason=" << reason;
    }

    bool shouldRetryPrimaryOnRecoveryKeyFrameLocked_(SwVideoPacket::Codec codec,
                                                     const SwVideoPacket& packet,
                                                     uint64_t packetEpoch) const {
        if (!m_runtimeDecoderRerouteEnabled.load()) {
            return false;
        }
        auto stateIt = m_decoderRoutingStates.find(codec);
        if (stateIt == m_decoderRoutingStates.end()) {
            return false;
        }
        const DecoderRoutingState& state = stateIt->second;
        return state.retryPrimaryOnNextRecoveryKeyFrame &&
               !state.retryAttemptedThisCycle &&
               !state.temporaryOverrideDecoderId.isEmpty() &&
               state.temporaryOverrideBridged &&
               state.primaryRetryEligible &&
               state.bridgeRecoveryEpoch != 0 &&
               (packetEpoch != state.bridgeRecoveryEpoch) &&
               isRecoveryKeyFrame_(packet);
    }

    void beginPrimaryRetryLocked_(SwVideoPacket::Codec codec, uint64_t packetEpoch) {
        if (!m_runtimeDecoderRerouteEnabled.load()) {
            return;
        }
        DecoderRoutingState& state = m_decoderRoutingStates[codec];
        state.temporaryOverrideDecoderId.clear();
        state.retryAttemptedThisCycle = true;
        state.pendingPrimaryRetry = true;
        state.temporaryOverrideBridged = false;
        state.primaryRetryEligible = false;
        state.bridgeRecoveryEpoch = packetEpoch;
        state.softwareStableDecodedFrames = 0;
        state.softwareStableSinceTickMs = 0;
        swCWarning(kSwLogCategory_SwVideoPipeline)
            << "[SwVideoPipeline] Retrying primary decoder on recovery keyframe"
            << " codec=" << static_cast<int>(codec)
            << " primary=" << primaryDecoderSelectionLocked_(codec)
            << " epoch=" << packetEpoch;
    }

    void noteSoftwareBridgeFrameLocked_(SwVideoPacket::Codec codec, uint64_t frameEpoch) {
        if (!m_runtimeDecoderRerouteEnabled.load()) {
            return;
        }
        auto stateIt = m_decoderRoutingStates.find(codec);
        if (stateIt == m_decoderRoutingStates.end()) {
            return;
        }
        DecoderRoutingState& state = stateIt->second;
        if (state.temporaryOverrideDecoderId.isEmpty()) {
            return;
        }
        const int64_t nowTickMs = monotonicMs_();
        if (!state.temporaryOverrideBridged) {
            state.temporaryOverrideBridged = true;
            state.bridgeRecoveryEpoch = frameEpoch;
            state.softwareStableDecodedFrames = 0;
            state.softwareStableSinceTickMs = nowTickMs;
            swCWarning(kSwLogCategory_SwVideoPipeline)
                << "[SwVideoPipeline] Temporary software decoder bridge active"
                << " codec=" << static_cast<int>(codec)
                << " epoch=" << frameEpoch;
        }
        ++state.softwareStableDecodedFrames;
        if (!state.primaryRetryEligible &&
            state.softwareStableSinceTickMs > 0 &&
            state.softwareStableDecodedFrames >= kSoftwareBridgeRetryStableFrameThreshold_ &&
            (nowTickMs - state.softwareStableSinceTickMs) >= kSoftwareBridgeRetryStableMs_) {
            state.primaryRetryEligible = true;
            swCWarning(kSwLogCategory_SwVideoPipeline)
                << "[SwVideoPipeline] Temporary software decoder bridge is now stable enough to retry primary"
                << " codec=" << static_cast<int>(codec)
                << " frames=" << state.softwareStableDecodedFrames
                << " stableMs=" << (nowTickMs - state.softwareStableSinceTickMs);
        }
    }

    void notePrimaryRetrySuccessLocked_(SwVideoPacket::Codec codec) {
        if (!m_runtimeDecoderRerouteEnabled.load()) {
            return;
        }
        auto stateIt = m_decoderRoutingStates.find(codec);
        if (stateIt == m_decoderRoutingStates.end()) {
            return;
        }
        DecoderRoutingState& state = stateIt->second;
        if (!state.pendingPrimaryRetry) {
            return;
        }
        state.pendingPrimaryRetry = false;
        state.retryPrimaryOnNextRecoveryKeyFrame = false;
        state.temporaryOverrideBridged = false;
        state.primaryRetryEligible = false;
        state.bridgeRecoveryEpoch = 0;
        state.softwareStableDecodedFrames = 0;
        state.softwareStableSinceTickMs = 0;
        swCWarning(kSwLogCategory_SwVideoPipeline)
            << "[SwVideoPipeline] Primary decoder retry succeeded"
            << " codec=" << static_cast<int>(codec)
            << " primary=" << primaryDecoderSelectionLocked_(codec);
    }

    bool emitRawFrameDirect(const SwVideoPacket& packet, uint64_t packetEpoch) {
        if (packetEpoch != m_recoveryEpoch.load()) {
            return false;
        }
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
            m_packetQueue.clear();
            m_queuedBytes = 0;
            updateQueuePressureLocked_();
        }
        publishConsumerPressure_();
        m_queueCv.notify_all();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            m_workerRunning = false;
            m_stopWorker = false;
            updateQueuePressureLocked_();
        }
        publishConsumerPressure_();
    }

    void enqueuePacket(const SwVideoPacket& packet, uint64_t recoveryEpoch) {
        bool accepted = false;
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            if (!m_workerRunning) {
                return;
            }
            const std::size_t packetBytes = static_cast<std::size_t>(packet.payload().size());
            const bool packetsLimited = m_maxQueuedPackets > 0;
            const bool bytesLimited = m_maxQueuedBytes > 0;
            const bool rejectForPackets =
                packetsLimited && (m_packetQueue.size() >= m_maxQueuedPackets);
            const bool rejectForBytes =
                bytesLimited && ((m_queuedBytes + packetBytes) > m_maxQueuedBytes);
            if (!rejectForPackets && !rejectForBytes) {
                QueuedVideoPacket queuedPacket;
                queuedPacket.packet = packet;
                queuedPacket.recoveryEpoch = recoveryEpoch;
                m_packetQueue.push_back(std::move(queuedPacket));
                m_queuedBytes += packetBytes;
                accepted = true;
            }
            updateQueuePressureLocked_();
        }
        publishConsumerPressure_();
        if (!accepted) {
            swCWarning(kSwLogCategory_SwVideoPipeline)
                << "[SwVideoPipeline] Rejecting packet enqueue at hard queue limit"
                << " codec=" << static_cast<int>(packet.codec())
                << " bytes=" << packet.payload().size();
            return;
        }
        m_queueCv.notify_one();
    }

    static std::size_t pressureHighWatermark_(std::size_t limit) {
        if (limit == 0U) {
            return 0U;
        }
        return std::max<std::size_t>(1U, (limit * 3U + 3U) / 4U);
    }

    static std::size_t pressureLowWatermark_(std::size_t limit) {
        if (limit == 0U) {
            return 0U;
        }
        return std::max<std::size_t>(1U, limit / 2U);
    }

    void updateQueuePressureLocked_() {
        const bool packetsLimited = m_maxQueuedPackets > 0U;
        const bool bytesLimited = m_maxQueuedBytes > 0U;
        const std::size_t highPacketWatermark = pressureHighWatermark_(m_maxQueuedPackets);
        const std::size_t highByteWatermark = pressureHighWatermark_(m_maxQueuedBytes);
        const std::size_t lowPacketWatermark = pressureLowWatermark_(m_maxQueuedPackets);
        const std::size_t lowByteWatermark = pressureLowWatermark_(m_maxQueuedBytes);

        const bool overSoftPackets =
            packetsLimited && (m_packetQueue.size() >= highPacketWatermark);
        const bool overSoftBytes =
            bytesLimited && (m_queuedBytes >= highByteWatermark);
        const bool underLowPackets =
            !packetsLimited || (m_packetQueue.size() <= lowPacketWatermark);
        const bool underLowBytes =
            !bytesLimited || (m_queuedBytes <= lowByteWatermark);
        const bool overHardPackets =
            packetsLimited && (m_packetQueue.size() >= m_maxQueuedPackets);
        const bool overHardBytes =
            bytesLimited && (m_queuedBytes >= m_maxQueuedBytes);

        if (!m_softPressureActive) {
            m_softPressureActive = overSoftPackets || overSoftBytes;
        } else if (underLowPackets && underLowBytes) {
            m_softPressureActive = false;
        }

        if (!m_hardPressureActive) {
            m_hardPressureActive = overHardPackets || overHardBytes;
        } else if (underLowPackets && underLowBytes) {
            m_hardPressureActive = false;
        }
    }

    void publishConsumerPressure_() {
        std::shared_ptr<SwVideoSource> source;
        SwVideoSource::ConsumerPressure pressure;
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            pressure.queuedPackets = m_packetQueue.size();
            pressure.queuedBytes = m_queuedBytes;
            pressure.softPressure = m_softPressureActive;
            pressure.hardPressure = m_hardPressureActive;
        }
        {
            SwMutexLocker lock(m_mutex);
            source = m_source;
        }

        const int64_t lastDecodedTickMs = m_lastDecodedFrameTickMs.load();
        const int64_t nowTickMs = monotonicMs_();
        pressure.packetsWithoutFrame = m_packetsSinceLastDecodedFrame.load();
        pressure.stalledForMs =
            (lastDecodedTickMs > 0) ? std::max<int64_t>(0, nowTickMs - lastDecodedTickMs) : 0;
        pressure.decoderStalled =
            m_decoderRecoveryRequested.load() ||
            (!m_waitingForDecoderSync.load() &&
             (lastDecodedTickMs > 0) &&
             (pressure.stalledForMs >= m_decoderStallThresholdMs) &&
             (pressure.packetsWithoutFrame >= std::max<uint64_t>(1U, m_decoderStallPacketThreshold)));

        if (source) {
            source->setConsumerPressure(pressure);
        }
    }

    void workerLoop() {
        while (true) {
            QueuedVideoPacket queuedPacket;
            {
                std::unique_lock<std::mutex> queueLock(m_queueMutex);
                m_queueCv.wait(queueLock, [this]() {
                    return m_stopWorker || !m_packetQueue.empty();
                });
                if (m_stopWorker && m_packetQueue.empty()) {
                    break;
                }
                queuedPacket = std::move(m_packetQueue.front());
                m_queuedBytes -= static_cast<std::size_t>(queuedPacket.packet.payload().size());
                m_packetQueue.pop_front();
                updateQueuePressureLocked_();
            }
            publishConsumerPressure_();
            if (queuedPacket.recoveryEpoch != m_recoveryEpoch.load()) {
                continue;
            }
            handlePacket(queuedPacket.packet, queuedPacket.recoveryEpoch);
        }
    }

    void handlePacket(const SwVideoPacket& packet, uint64_t packetEpoch) {
        if (packetEpoch != m_recoveryEpoch.load()) {
            return;
        }
        if (packet.carriesRawFrame()) {
            emitRawFrameDirect(packet, packetEpoch);
            return;
        }

        std::shared_ptr<SwVideoDecoder> decoder;
        std::shared_ptr<SwVideoDecoder> decoderToFlush;
        SwString preferredDecoderId;
        SwString activeDecoderId;
        const int64_t nowTickMs = monotonicMs_();
        {
            SwMutexLocker lock(m_mutex);
            if (packetEpoch != m_recoveryEpoch.load()) {
                return;
            }
            m_lastPacketTickMs.store(nowTickMs);
            decoder = m_decoder;
            if (shouldRetryPrimaryOnRecoveryKeyFrameLocked_(packet.codec(), packet, packetEpoch)) {
                decoderToFlush = decoder;
                decoder.reset();
                resetDecoderSelectionStateLocked_();
                beginPrimaryRetryLocked_(packet.codec(), packetEpoch);
            }
            if (decoder && m_autoDecoderEnabled &&
                m_decoderCodec != SwVideoPacket::Codec::Unknown &&
                m_decoderCodec != packet.codec()) {
                decoder.reset();
                resetDecoderSelectionStateLocked_();
            }
            if (decoder && !m_waitingForDecoderSync.load()) {
                const uint64_t packetsWithoutFrame = m_packetsSinceLastDecodedFrame.fetch_add(1) + 1;
                const int64_t lastDecodedTickMs = m_lastDecodedFrameTickMs.load();
                const int64_t stalledForMs =
                    (lastDecodedTickMs > 0) ? (nowTickMs - lastDecodedTickMs) : 0;
                const uint64_t packetThreshold =
                    packet.isDiscontinuity() ? (m_decoderStallPacketThreshold / 2)
                                             : m_decoderStallPacketThreshold;
                if (m_decoderStallRecoveryEnabled.load() &&
                    lastDecodedTickMs > 0 &&
                    stalledForMs >= m_decoderStallThresholdMs &&
                    packetsWithoutFrame >= std::max<uint64_t>(1, packetThreshold)) {
                    const bool canBridgeToSoftware =
                        m_runtimeDecoderRerouteEnabled.load() &&
                        m_autoDecoderEnabled &&
                        packet.codec() == SwVideoPacket::Codec::H265 &&
                        isHardwareDecoderId_(m_activeDecoderId);
                    const bool retryFailure =
                        canBridgeToSoftware &&
                        m_decoderRoutingStates[packet.codec()].pendingPrimaryRetry;
                    decoderToFlush = decoder;
                    decoder.reset();
                    resetDecoderSelectionStateLocked_();
                    if (canBridgeToSoftware) {
                        activateTemporaryDecoderOverrideLocked_(packet.codec(),
                                                                "decoder-stalled",
                                                                retryFailure);
                    } else {
                        requestDecoderRecoveryLocked_("decoder-stalled");
                    }
                    swCWarning(kSwLogCategory_SwVideoPipeline)
                        << "[SwVideoPipeline] Decoder stalled for " << stalledForMs
                        << " ms after " << packetsWithoutFrame
                        << " packets, forcing decoder resync"
                        << " codec=" << static_cast<int>(packet.codec())
                        << " discontinuity=" << (packet.isDiscontinuity() ? 1 : 0);
                }
            }
            if (!decoder && m_autoDecoderEnabled) {
                bool acquiredPreferredDecoder = false;
                preferredDecoderId = effectiveDecoderSelectionLocked_(packet.codec());
                if (!preferredDecoderId.isEmpty()) {
                    decoder = SwVideoDecoderFactory::instance().acquire(packet.codec(),
                                                                        preferredDecoderId);
                    acquiredPreferredDecoder = (decoder != nullptr);
                    if (!decoder) {
                        swCWarning(kSwLogCategory_SwVideoPipeline)
                            << "[SwVideoPipeline] Preferred decoder unavailable codec="
                            << static_cast<int>(packet.codec()) << " id=" << preferredDecoderId
                            << ", keeping fixed backend selection";
                    }
                }
                if (!decoder && preferredDecoderId.isEmpty()) {
                    decoder = SwVideoDecoderFactory::instance().acquire(packet.codec());
                }
                if (decoder) {
                    if (m_frameCallback) {
                        decoder->setFrameCallback(makeDecoderFrameCallbackLocked_());
                    }
                    m_decoder = decoder;
                    m_decoderCodec = packet.codec();
                    m_activeDecoderId = acquiredPreferredDecoder ? preferredDecoderId
                                                                 : inferredDecoderIdForInstanceLocked_(decoder);
                    m_waitingForDecoderSync.store(true);
                    m_loggedWaitingForDecoderSync.store(false);
                    m_loggedFirstPacketToDecoder.store(false);
                    m_packetsSinceLastDecodedFrame.store(0);
                    m_lastDecodedFrameTickMs.store(0);
                    swCWarning(kSwLogCategory_SwVideoPipeline) << "[SwVideoPipeline] Decoder selected codec="
                                << static_cast<int>(packet.codec())
                                << " name=" << decoder->name()
                                << " id=" << m_activeDecoderId;
                }
            }
            activeDecoderId = m_activeDecoderId;
        }
        if (decoderToFlush) {
            decoderToFlush->flush();
            decoderToFlush.reset();
        }
        if (packetEpoch != m_recoveryEpoch.load()) {
            publishConsumerPressure_();
            return;
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
            m_decoderFrameEpoch.store(packetEpoch);
            const bool feedOk = decoder->feed(packetToFeed);
            const SwVideoDecoder::RuntimeHealthEvent healthEvent = decoder->takeRuntimeHealthEvent();
            if (!feedOk || healthEvent.isValid()) {
                const SwString failureReason =
                    healthEvent.isValid() ? healthEvent.reason : SwString("feed-failed");
                const bool canBridgeToSoftware =
                    m_runtimeDecoderRerouteEnabled.load() &&
                    m_autoDecoderEnabled &&
                    packetToFeed.codec() == SwVideoPacket::Codec::H265 &&
                    isHardwareDecoderId_(activeDecoderId);
                bool retryFailure = false;
                swCWarning(kSwLogCategory_SwVideoPipeline)
                            << (feedOk ? "[SwVideoPipeline] Decoder output health failure "
                                       : "[SwVideoPipeline] Decoder feed failed ")
                            << decoder->name()
                            << " codec=" << static_cast<int>(packetToFeed.codec())
                            << " bytes=" << packetToFeed.payload().size()
                            << " reason=" << failureReason;
                {
                    SwMutexLocker lock(m_mutex);
                    if (canBridgeToSoftware) {
                        auto stateIt = m_decoderRoutingStates.find(packetToFeed.codec());
                        retryFailure = (stateIt != m_decoderRoutingStates.end()) &&
                                       stateIt->second.pendingPrimaryRetry;
                    }
                    if (m_decoder == decoder) {
                        resetDecoderSelectionStateLocked_();
                    }
                    if (healthEvent.isValid() && canBridgeToSoftware) {
                        activateTemporaryDecoderOverrideLocked_(packetToFeed.codec(),
                                                                failureReason,
                                                                retryFailure);
                    } else {
                        requestDecoderRecoveryLocked_(failureReason);
                    }
                }
                decoder->flush();
            }
        }
        publishConsumerPressure_();
    }

    static int64_t monotonicMs_() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    SwVideoDecoder::FrameCallback makeDecoderFrameCallbackLocked_() {
        SwVideoDecoder::FrameCallback downstream = m_frameCallback;
        return [this, downstream](const SwVideoFrame& frame) {
            const uint64_t frameEpoch = m_decoderFrameEpoch.load();
            if (frameEpoch != m_recoveryEpoch.load()) {
                return;
            }
            {
                SwMutexLocker lock(m_mutex);
                clearDecoderRecoveryRequestLocked_();
                if (m_decoderCodec == SwVideoPacket::Codec::H265 &&
                    isSoftwareDecoderId_(m_activeDecoderId)) {
                    noteSoftwareBridgeFrameLocked_(m_decoderCodec, frameEpoch);
                } else if (m_decoderCodec == SwVideoPacket::Codec::H265 &&
                           isHardwareDecoderId_(m_activeDecoderId)) {
                    notePrimaryRetrySuccessLocked_(m_decoderCodec);
                }
            }
            m_lastDecodedFrameTickMs.store(monotonicMs_());
            m_packetsSinceLastDecodedFrame.store(0);
            publishConsumerPressure_();
            if (downstream) {
                downstream(frame);
            }
        };
    }

    mutable SwMutex m_mutex;
    std::shared_ptr<SwVideoSource> m_source;
    std::shared_ptr<SwVideoDecoder> m_decoder;
    SwVideoPacket::Codec m_decoderCodec{SwVideoPacket::Codec::Unknown};
    SwString m_activeDecoderId{};
    SwString m_decoderRecoveryReason{};
    SwVideoDecoder::FrameCallback m_frameCallback;
    bool m_autoDecoderEnabled{true};
    std::map<SwVideoPacket::Codec, SwString> m_decoderSelectionIds;
    std::map<SwVideoPacket::Codec, SwString> m_resolvedDecoderSelectionIds;
    std::map<SwVideoPacket::Codec, DecoderRoutingState> m_decoderRoutingStates;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<QueuedVideoPacket> m_packetQueue;
    std::thread m_workerThread;
    bool m_stopWorker{false};
    bool m_workerRunning{false};
    std::size_t m_queuedBytes{0};
    std::size_t m_maxQueuedPackets{24};
    std::size_t m_maxQueuedBytes{4 * 1024 * 1024};
    bool m_softPressureActive{false};
    bool m_hardPressureActive{false};
    std::atomic<bool> m_loggedFirstPacketToDecoder{false};
    std::atomic<bool> m_asyncDecodeEnabled{false};
    std::atomic<bool> m_runtimeDecoderRerouteEnabled{false};
    std::atomic<bool> m_decoderStallRecoveryEnabled{true};
    std::atomic<bool> m_decoderRecoveryRequested{false};
    std::atomic<bool> m_waitingForDecoderSync{false};
    std::atomic<bool> m_loggedWaitingForDecoderSync{false};
    std::atomic<int64_t> m_lastPacketTickMs{0};
    std::atomic<int64_t> m_lastDecodedFrameTickMs{0};
    std::atomic<uint64_t> m_packetsSinceLastDecodedFrame{0};
    std::atomic<uint64_t> m_recoveryEpoch{1};
    std::atomic<uint64_t> m_decoderFrameEpoch{0};
    int64_t m_decoderStallThresholdMs{1500};
    uint64_t m_decoderStallPacketThreshold{18};
    bool m_started{false};
};
