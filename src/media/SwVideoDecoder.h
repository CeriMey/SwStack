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

#include "media/SwVideoFrame.h"
#include "media/SwVideoPacket.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>

class SwVideoDecoder {
public:
    using FrameCallback = std::function<void(const SwVideoFrame&)>;

    virtual ~SwVideoDecoder() = default;

    virtual const char* name() const = 0;
    virtual bool open(const SwVideoFormatInfo& expectedFormat) {
        (void)expectedFormat;
        return true;
    }
    virtual bool feed(const SwVideoPacket& packet) = 0;
    virtual void flush() {}

    void setFrameCallback(FrameCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_frameCallback = std::move(callback);
    }

protected:
    void emitFrame(const SwVideoFrame& frame) {
        FrameCallback cb;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            cb = m_frameCallback;
        }
        if (cb) {
            cb(frame);
        }
    }

private:
    std::mutex m_callbackMutex;
    FrameCallback m_frameCallback;
};

class SwPassthroughVideoDecoder : public SwVideoDecoder {
public:
    const char* name() const override { return "SwPassthroughVideoDecoder"; }

    bool feed(const SwVideoPacket& packet) override {
        if (!packet.carriesRawFrame() || packet.payload().isEmpty()) {
            return false;
        }
        SwVideoFrame frame =
            SwVideoFrame::fromCopy(packet.rawFormat().width,
                                   packet.rawFormat().height,
                                   packet.rawFormat().format,
                                   packet.payload().constData(),
                                   static_cast<std::size_t>(packet.payload().size()));
        if (!frame.isValid()) {
            return false;
        }
        frame.setTimestamp(packet.pts());
        emitFrame(frame);
        return true;
    }
};

class SwVideoDecoderFactory {
public:
    using Creator = std::function<std::shared_ptr<SwVideoDecoder>()>;

    static SwVideoDecoderFactory& instance() {
        static SwVideoDecoderFactory g_factory;
        return g_factory;
    }

    void registerDecoder(SwVideoPacket::Codec codec,
                         Creator creator,
                         int priority = 0,
                         bool shareable = false) {
        if (!creator) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& entries = m_decoders[codec];
        entries.emplace_back();
        auto& entry = entries.back();
        entry.factory = std::move(creator);
        entry.priority = priority;
        entry.shareable = shareable;
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.priority > b.priority;
        });
    }

    std::shared_ptr<SwVideoDecoder> acquire(SwVideoPacket::Codec codec) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_decoders.find(codec);
        if (it == m_decoders.end()) {
            return nullptr;
        }
        for (auto& entry : it->second) {
            if (entry.shareable) {
                for (auto poolIt = entry.pool.begin(); poolIt != entry.pool.end();) {
                    if (auto decoder = poolIt->lock()) {
                        return decoder;
                    }
                    poolIt = entry.pool.erase(poolIt);
                }
            }
            if (entry.factory) {
                auto decoder = entry.factory();
                if (decoder && entry.shareable) {
                    entry.pool.emplace_back(decoder);
                }
                if (decoder) {
                    return decoder;
                }
            }
        }
        return nullptr;
    }

    std::shared_ptr<SwVideoDecoder> create(SwVideoPacket::Codec codec) {
        return acquire(codec);
    }

private:
    struct Entry {
        Creator factory;
        int priority{0};
        bool shareable{false};
        std::vector<std::weak_ptr<SwVideoDecoder>> pool;
    };

    SwVideoDecoderFactory() {
        registerDecoder(SwVideoPacket::Codec::RawRGB, []() { return std::make_shared<SwPassthroughVideoDecoder>(); }, -1, true);
        registerDecoder(SwVideoPacket::Codec::RawBGR, []() { return std::make_shared<SwPassthroughVideoDecoder>(); }, -1, true);
        registerDecoder(SwVideoPacket::Codec::RawRGBA, []() { return std::make_shared<SwPassthroughVideoDecoder>(); }, -1, true);
        registerDecoder(SwVideoPacket::Codec::RawBGRA, []() { return std::make_shared<SwPassthroughVideoDecoder>(); }, -1, true);
    }

    mutable std::mutex m_mutex;
    std::map<SwVideoPacket::Codec, std::vector<Entry>> m_decoders;
};
