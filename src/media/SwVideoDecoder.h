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
 * @file src/media/SwVideoDecoder.h
 * @ingroup media
 * @brief Declares the public interface exposed by SwVideoDecoder in the CoreSw media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the video decoder interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwVideoDecoder, SwPassthroughVideoDecoder, and
 * SwVideoDecoderFactory.
 *
 * Decoder-oriented declarations here establish the boundary between encoded input and decoded
 * output, including the format assumptions and ownership expectations that surround that
 * conversion.
 *
 * Media-facing declarations here focus on packet and frame ownership, format description,
 * decoding boundaries, and real-time source control.
 *
 */


#include "media/SwVideoFrame.h"
#include "media/SwVideoPacket.h"
#include "core/types/SwList.h"
#include "core/types/SwString.h"

#include <functional>
#include <map>
#include <memory>
#include "core/fs/SwMutex.h"
#include <vector>
#include <algorithm>

class SwVideoDecoder {
public:
    using FrameCallback = std::function<void(const SwVideoFrame&)>;

    /**
     * @brief Destroys the `SwVideoDecoder` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwVideoDecoder() = default;

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual const char* name() const = 0;
    /**
     * @brief Opens the underlying resource managed by the object.
     * @param expectedFormat Value passed to the method.
     * @return The requested open.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    virtual bool open(const SwVideoFormatInfo& expectedFormat) {
        (void)expectedFormat;
        return true;
    }
    /**
     * @brief Performs the `feed` operation.
     * @param packet Value passed to the method.
     * @return The requested feed.
     */
    virtual bool feed(const SwVideoPacket& packet) = 0;
    /**
     * @brief Returns the current flush.
     * @return The current flush.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void flush() {}

    /**
     * @brief Sets the frame Callback.
     * @param callback Callback invoked by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFrameCallback(FrameCallback callback) {
        SwMutexLocker lock(m_callbackMutex);
        m_frameCallback = std::move(callback);
    }

protected:
    /**
     * @brief Performs the `emitFrame` operation.
     * @param frame Value passed to the method.
     */
    void emitFrame(const SwVideoFrame& frame) {
        FrameCallback cb;
        {
            SwMutexLocker lock(m_callbackMutex);
            cb = m_frameCallback;
        }
        if (cb) {
            cb(frame);
        }
    }

private:
    SwMutex m_callbackMutex;
    FrameCallback m_frameCallback;
};

class SwPassthroughVideoDecoder : public SwVideoDecoder {
public:
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* name() const override { return "SwPassthroughVideoDecoder"; }

    /**
     * @brief Performs the `feed` operation.
     * @param packet Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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

struct SwVideoDecoderDescriptor {
    SwVideoPacket::Codec codec{SwVideoPacket::Codec::Unknown};
    SwString id;
    SwString displayName;
    int priority{0};
    bool shareable{false};
    bool available{true};

    bool isValid() const {
        return codec != SwVideoPacket::Codec::Unknown && !id.isEmpty();
    }
};

class SwVideoDecoderFactory {
public:
    using Creator = std::function<std::shared_ptr<SwVideoDecoder>()>;

    /**
     * @brief Returns the current instance.
     * @return The current instance.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwVideoDecoderFactory& instance() {
        static SwVideoDecoderFactory g_factory;
        return g_factory;
    }

    /**
     * @brief Performs the `registerDecoder` operation.
     * @param codec Value passed to the method.
     * @param creator Value passed to the method.
     * @param priority Value passed to the method.
     * @param shareable Value passed to the method.
     */
    void registerDecoder(SwVideoPacket::Codec codec,
                         Creator creator,
                         int priority = 0,
                         bool shareable = false) {
        registerDecoder(codec,
                        SwString(),
                        SwString(),
                        std::move(creator),
                        priority,
                        shareable,
                        true);
    }

    /**
     * @brief Performs the `registerDecoder` operation.
     * @param codec Value passed to the method.
     * @param id Value passed to the method.
     * @param displayName Value passed to the method.
     * @param creator Value passed to the method.
     * @param priority Value passed to the method.
     * @param shareable Value passed to the method.
     * @param available Value passed to the method.
     */
    void registerDecoder(SwVideoPacket::Codec codec,
                         const SwString& id,
                         const SwString& displayName,
                         Creator creator,
                         int priority = 0,
                         bool shareable = false,
                         bool available = true) {
        if (!creator) {
            return;
        }
        SwMutexLocker lock(m_mutex);
        auto& entries = m_decoders[codec];
        entries.emplace_back();
        auto& entry = entries.back();
        entry.descriptor.codec = codec;
        entry.descriptor.id = id.isEmpty() ? generateAnonymousIdLocked(codec) : id;
        entry.descriptor.displayName = displayName.isEmpty() ? entry.descriptor.id : displayName;
        entry.descriptor.priority = priority;
        entry.descriptor.shareable = shareable;
        entry.descriptor.available = available;
        entry.factory = std::move(creator);
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.descriptor.priority > b.descriptor.priority;
        });
    }

    /**
     * @brief Performs the `acquire` operation.
     * @param codec Value passed to the method.
     * @return The requested acquire.
     */
    std::shared_ptr<SwVideoDecoder> acquire(SwVideoPacket::Codec codec) {
        SwMutexLocker lock(m_mutex);
        auto it = m_decoders.find(codec);
        if (it == m_decoders.end()) {
            return nullptr;
        }
        for (auto& entry : it->second) {
            auto decoder = acquireEntryLocked(entry);
            if (decoder) {
                return decoder;
            }
        }
        return nullptr;
    }

    /**
     * @brief Performs the `acquire` operation.
     * @param codec Value passed to the method.
     * @param id Value passed to the method.
     * @return The requested acquire.
     */
    std::shared_ptr<SwVideoDecoder> acquire(SwVideoPacket::Codec codec, const SwString& id) {
        SwMutexLocker lock(m_mutex);
        auto it = m_decoders.find(codec);
        if (it == m_decoders.end()) {
            return nullptr;
        }
        for (auto& entry : it->second) {
            if (entry.descriptor.id != id) {
                continue;
            }
            return acquireEntryLocked(entry);
        }
        return nullptr;
    }

    /**
     * @brief Returns the registered decoder descriptors for the given codec.
     * @param codec Value passed to the method.
     * @param availableOnly Value passed to the method.
     * @return The registered descriptors ordered by priority.
     */
    SwList<SwVideoDecoderDescriptor> list(SwVideoPacket::Codec codec,
                                          bool availableOnly = true) const {
        SwList<SwVideoDecoderDescriptor> descriptors;
        SwMutexLocker lock(m_mutex);
        auto it = m_decoders.find(codec);
        if (it == m_decoders.end()) {
            return descriptors;
        }
        for (const auto& entry : it->second) {
            if (availableOnly && !entry.descriptor.available) {
                continue;
            }
            descriptors.append(entry.descriptor);
        }
        return descriptors;
    }

    /**
     * @brief Returns whether a decoder id is registered for the given codec.
     * @param codec Value passed to the method.
     * @param id Value passed to the method.
     * @param availableOnly Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool contains(SwVideoPacket::Codec codec,
                  const SwString& id,
                  bool availableOnly = true) const {
        SwMutexLocker lock(m_mutex);
        auto it = m_decoders.find(codec);
        if (it == m_decoders.end()) {
            return false;
        }
        for (const auto& entry : it->second) {
            if (entry.descriptor.id != id) {
                continue;
            }
            if (availableOnly && !entry.descriptor.available) {
                return false;
            }
            return true;
        }
        return false;
    }

    /**
     * @brief Creates the requested create.
     * @param codec Value passed to the method.
     * @return The resulting create.
     */
    std::shared_ptr<SwVideoDecoder> create(SwVideoPacket::Codec codec) {
        return acquire(codec);
    }

    /**
     * @brief Creates the requested create.
     * @param codec Value passed to the method.
     * @param id Value passed to the method.
     * @return The resulting create.
     */
    std::shared_ptr<SwVideoDecoder> create(SwVideoPacket::Codec codec, const SwString& id) {
        return acquire(codec, id);
    }

private:
    struct Entry {
        SwVideoDecoderDescriptor descriptor;
        Creator factory;
        std::vector<std::weak_ptr<SwVideoDecoder>> pool;
    };

    std::shared_ptr<SwVideoDecoder> acquireEntryLocked(Entry& entry) {
        if (!entry.descriptor.available) {
            return nullptr;
        }
        if (entry.descriptor.shareable) {
            for (auto poolIt = entry.pool.begin(); poolIt != entry.pool.end();) {
                if (auto decoder = poolIt->lock()) {
                    return decoder;
                }
                poolIt = entry.pool.erase(poolIt);
            }
        }
        if (!entry.factory) {
            return nullptr;
        }
        auto decoder = entry.factory();
        if (decoder && entry.descriptor.shareable) {
            entry.pool.emplace_back(decoder);
        }
        return decoder;
    }

    SwString generateAnonymousIdLocked(SwVideoPacket::Codec codec) {
        int& counter = m_anonymousIds[codec];
        ++counter;
        return SwString("decoder-") + SwString(std::to_string(static_cast<int>(codec))) +
               SwString("-") + SwString(std::to_string(counter));
    }

    SwVideoDecoderFactory() {
        registerDecoder(SwVideoPacket::Codec::RawRGB,
                        "passthrough",
                        "Passthrough",
                        []() { return std::make_shared<SwPassthroughVideoDecoder>(); },
                        -1,
                        true);
        registerDecoder(SwVideoPacket::Codec::RawBGR,
                        "passthrough",
                        "Passthrough",
                        []() { return std::make_shared<SwPassthroughVideoDecoder>(); },
                        -1,
                        true);
        registerDecoder(SwVideoPacket::Codec::RawRGBA,
                        "passthrough",
                        "Passthrough",
                        []() { return std::make_shared<SwPassthroughVideoDecoder>(); },
                        -1,
                        true);
        registerDecoder(SwVideoPacket::Codec::RawBGRA,
                        "passthrough",
                        "Passthrough",
                        []() { return std::make_shared<SwPassthroughVideoDecoder>(); },
                        -1,
                        true);
    }

    mutable SwMutex m_mutex;
    std::map<SwVideoPacket::Codec, std::vector<Entry>> m_decoders;
    std::map<SwVideoPacket::Codec, int> m_anonymousIds;
};
