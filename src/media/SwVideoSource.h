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


#include "media/SwVideoDecoder.h"
#include "media/SwVideoPacket.h"

#include <atomic>
#include <functional>
#include <memory>
#include "core/types/SwString.h"
#include "core/fs/SwMutex.h"

class SwVideoSource {
public:
    using PacketCallback = std::function<void(const SwVideoPacket&)>;

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
    }

    /**
     * @brief Returns whether the object reports running.
     * @return `true` when the object reports running; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isRunning() const { return m_running.load(); }
    /**
     * @brief Sets the running.
     * @param running Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRunning(bool running) { m_running.store(running); }

private:
    SwMutex m_callbackMutex;
    PacketCallback m_packetCallback;
    std::atomic<bool> m_running{false};
};

class SwVideoPipeline : public std::enable_shared_from_this<SwVideoPipeline> {
public:
    /**
     * @brief Sets the source.
     * @param source Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSource(const std::shared_ptr<SwVideoSource>& source) {
        SwMutexLocker lock(m_mutex);
        m_source = source;
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
        if (m_decoder && m_frameCallback) {
            m_decoder->setFrameCallback(m_frameCallback);
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
        m_autoDecoderEnabled = true;
        if (m_decoder && m_frameCallback) {
            m_decoder->setFrameCallback(m_frameCallback);
        }
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
            m_decoder->setFrameCallback(m_frameCallback);
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
        std::shared_ptr<SwVideoDecoder> decoder;
        bool autoDecoder = true;
        {
            SwMutexLocker lock(m_mutex);
            source = m_source;
            decoder = m_decoder;
            autoDecoder = m_autoDecoderEnabled;
        }
        if (!source || (!decoder && !autoDecoder)) {
            return;
        }

        std::weak_ptr<SwVideoPipeline> weakSelf = shared_from_this();
        source->setPacketCallback([weakSelf](const SwVideoPacket& packet) {
            if (auto self = weakSelf.lock()) {
                self->handlePacket(packet);
            }
        });
        source->start();
    }

    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() {
        std::shared_ptr<SwVideoSource> source;
        {
            SwMutexLocker lock(m_mutex);
            source = m_source;
        }
        if (source) {
            source->stop();
        }
    }

private:
    void handlePacket(const SwVideoPacket& packet) {
        std::shared_ptr<SwVideoDecoder> decoder;
        {
            SwMutexLocker lock(m_mutex);
            decoder = m_decoder;
            if (decoder && m_autoDecoderEnabled &&
                m_decoderCodec != SwVideoPacket::Codec::Unknown &&
                m_decoderCodec != packet.codec()) {
                decoder.reset();
                m_decoder.reset();
                m_decoderCodec = SwVideoPacket::Codec::Unknown;
            }
            if (!decoder && m_autoDecoderEnabled) {
                decoder = SwVideoDecoderFactory::instance().acquire(packet.codec());
                if (decoder) {
                    if (m_frameCallback) {
                        decoder->setFrameCallback(m_frameCallback);
                    }
                    m_decoder = decoder;
                    m_decoderCodec = packet.codec();
                }
            }
        }
        if (decoder) {
            decoder->feed(packet);
        }
    }

    SwMutex m_mutex;
    std::shared_ptr<SwVideoSource> m_source;
    std::shared_ptr<SwVideoDecoder> m_decoder;
    SwVideoPacket::Codec m_decoderCodec{SwVideoPacket::Codec::Unknown};
    SwVideoDecoder::FrameCallback m_frameCallback;
    bool m_autoDecoderEnabled{true};
};
