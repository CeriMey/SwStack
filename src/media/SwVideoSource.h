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

#include "media/SwVideoDecoder.h"
#include "media/SwVideoPacket.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class SwVideoSource {
public:
    using PacketCallback = std::function<void(const SwVideoPacket&)>;

    virtual ~SwVideoSource() = default;

    virtual std::string name() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    void setPacketCallback(PacketCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_packetCallback = std::move(callback);
    }

protected:
    void emitPacket(const SwVideoPacket& packet) {
        PacketCallback cb;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            cb = m_packetCallback;
        }
        if (cb) {
            cb(packet);
        }
    }

    bool isRunning() const { return m_running.load(); }
    void setRunning(bool running) { m_running.store(running); }

private:
    std::mutex m_callbackMutex;
    PacketCallback m_packetCallback;
    std::atomic<bool> m_running{false};
};

class SwVideoPipeline : public std::enable_shared_from_this<SwVideoPipeline> {
public:
    void setSource(const std::shared_ptr<SwVideoSource>& source) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_source = source;
    }

    void setDecoder(const std::shared_ptr<SwVideoDecoder>& decoder) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_decoder = decoder;
        m_decoderCodec = SwVideoPacket::Codec::Unknown;
        m_autoDecoderEnabled = false;
        if (m_decoder && m_frameCallback) {
            m_decoder->setFrameCallback(m_frameCallback);
        }
    }

    void setDecoderHint(SwVideoPacket::Codec codec, const std::shared_ptr<SwVideoDecoder>& decoder) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_decoder = decoder;
        m_decoderCodec = codec;
        m_autoDecoderEnabled = true;
        if (m_decoder && m_frameCallback) {
            m_decoder->setFrameCallback(m_frameCallback);
        }
    }

    void setFrameCallback(SwVideoDecoder::FrameCallback cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameCallback = std::move(cb);
        if (m_decoder) {
            m_decoder->setFrameCallback(m_frameCallback);
        }
    }

    void useDecoderFactory(bool enabled) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_autoDecoderEnabled = enabled;
    }

    void start() {
        std::shared_ptr<SwVideoSource> source;
        std::shared_ptr<SwVideoDecoder> decoder;
        bool autoDecoder = true;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
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

    void stop() {
        std::shared_ptr<SwVideoSource> source;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
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
            std::lock_guard<std::mutex> lock(m_mutex);
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

    std::mutex m_mutex;
    std::shared_ptr<SwVideoSource> m_source;
    std::shared_ptr<SwVideoDecoder> m_decoder;
    SwVideoPacket::Codec m_decoderCodec{SwVideoPacket::Codec::Unknown};
    SwVideoDecoder::FrameCallback m_frameCallback;
    bool m_autoDecoderEnabled{true};
};
