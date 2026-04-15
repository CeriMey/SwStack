#pragma once

/**
 * @file src/media/SwAudioDecoder.h
 * @ingroup media
 * @brief Declares generic audio decoders and the audio decoder factory.
 */

#include "core/fs/SwMutex.h"
#include "core/types/SwList.h"
#include "core/types/SwString.h"
#include "media/SwAudioFrame.h"
#include "media/SwAudioPacket.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class SwAudioDecoder {
public:
    virtual ~SwAudioDecoder() = default;

    virtual const char* name() const = 0;
    virtual bool decode(const SwAudioPacket& packet, SwAudioFrame& frame) = 0;
    virtual void flush() {}
};

struct SwAudioDecoderDescriptor {
    SwAudioPacket::Codec codec{SwAudioPacket::Codec::Unknown};
    SwString id{};
    SwString displayName{};
    int priority{0};
    bool available{true};

    bool isValid() const {
        return codec != SwAudioPacket::Codec::Unknown && !id.isEmpty();
    }
};

class SwAudioDecoderFactory {
public:
    using Creator = std::function<std::shared_ptr<SwAudioDecoder>()>;

    static SwAudioDecoderFactory& instance() {
        static SwAudioDecoderFactory g_factory;
        return g_factory;
    }

    void registerDecoder(SwAudioPacket::Codec codec,
                         const SwString& id,
                         const SwString& displayName,
                         Creator creator,
                         int priority = 0,
                         bool available = true) {
        if (!creator) {
            return;
        }
        SwMutexLocker lock(m_mutex);
        auto& entries = m_entries[codec];
        entries.emplace_back();
        auto& entry = entries.back();
        entry.descriptor.codec = codec;
        entry.descriptor.id = id;
        entry.descriptor.displayName = displayName.isEmpty() ? id : displayName;
        entry.descriptor.priority = priority;
        entry.descriptor.available = available;
        entry.creator = std::move(creator);
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.descriptor.priority > b.descriptor.priority;
        });
    }

    std::shared_ptr<SwAudioDecoder> acquire(SwAudioPacket::Codec codec) {
        SwMutexLocker lock(m_mutex);
        auto it = m_entries.find(codec);
        if (it == m_entries.end()) {
            return nullptr;
        }
        for (const auto& entry : it->second) {
            if (!entry.descriptor.available) {
                continue;
            }
            auto decoder = entry.creator();
            if (decoder) {
                return decoder;
            }
        }
        return nullptr;
    }

    std::shared_ptr<SwAudioDecoder> acquire(SwAudioPacket::Codec codec, const SwString& id) {
        SwMutexLocker lock(m_mutex);
        auto it = m_entries.find(codec);
        if (it == m_entries.end()) {
            return nullptr;
        }
        for (const auto& entry : it->second) {
            if (entry.descriptor.id != id || !entry.descriptor.available) {
                continue;
            }
            return entry.creator();
        }
        return nullptr;
    }

    SwList<SwAudioDecoderDescriptor> list(SwAudioPacket::Codec codec,
                                          bool availableOnly = true) const {
        SwList<SwAudioDecoderDescriptor> descriptors;
        SwMutexLocker lock(m_mutex);
        auto it = m_entries.find(codec);
        if (it == m_entries.end()) {
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

    bool contains(SwAudioPacket::Codec codec,
                  const SwString& id,
                  bool availableOnly = true) const {
        SwMutexLocker lock(m_mutex);
        auto it = m_entries.find(codec);
        if (it == m_entries.end()) {
            return false;
        }
        for (const auto& entry : it->second) {
            if (entry.descriptor.id != id) {
                continue;
            }
            return !availableOnly || entry.descriptor.available;
        }
        return false;
    }

private:
    struct Entry {
        SwAudioDecoderDescriptor descriptor{};
        Creator creator{};
    };

    mutable SwMutex m_mutex;
    std::map<SwAudioPacket::Codec, std::vector<Entry>> m_entries{};
};

class SwBuiltinPcmuAudioDecoder : public SwAudioDecoder {
public:
    const char* name() const override { return "SwBuiltinPcmuAudioDecoder"; }

    bool decode(const SwAudioPacket& packet, SwAudioFrame& frame) override {
        if (packet.codec() != SwAudioPacket::Codec::PCMU || packet.payload().isEmpty()) {
            return false;
        }
        return decodeImpl_(packet, frame, true);
    }

private:
    static float muLawToFloat_(uint8_t value) {
        value = static_cast<uint8_t>(~value);
        int sign = value & 0x80;
        int exponent = (value >> 4) & 0x07;
        int mantissa = value & 0x0F;
        int sample = ((mantissa << 4) + 0x08) << exponent;
        sample -= 0x84;
        if (sign != 0) {
            sample = -sample;
        }
        return std::max(-1.0f, std::min(1.0f, static_cast<float>(sample) / 32768.0f));
    }

    static bool decodeImpl_(const SwAudioPacket& packet, SwAudioFrame& frame, bool muLaw) {
        std::vector<float> samples(packet.payload().size());
        const uint8_t* src = reinterpret_cast<const uint8_t*>(packet.payload().constData());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = muLaw ? muLawToFloat_(src[i]) : 0.0f;
        }
        frame.setSampleFormat(SwAudioFrame::SampleFormat::Float32);
        frame.setSampleRate(packet.sampleRate() > 0 ? packet.sampleRate() : 8000);
        frame.setChannelCount(packet.channelCount() > 0 ? packet.channelCount() : 1);
        frame.setTimestamp(packet.pts());
        frame.setPayload(SwByteArray(reinterpret_cast<const char*>(samples.data()),
                                     samples.size() * sizeof(float)));
        return true;
    }

    friend class SwBuiltinPcmaAudioDecoder;
};

class SwBuiltinPcmaAudioDecoder : public SwAudioDecoder {
public:
    const char* name() const override { return "SwBuiltinPcmaAudioDecoder"; }

    bool decode(const SwAudioPacket& packet, SwAudioFrame& frame) override {
        if (packet.codec() != SwAudioPacket::Codec::PCMA || packet.payload().isEmpty()) {
            return false;
        }
        std::vector<float> samples(packet.payload().size());
        const uint8_t* src = reinterpret_cast<const uint8_t*>(packet.payload().constData());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = aLawToFloat_(src[i]);
        }
        frame.setSampleFormat(SwAudioFrame::SampleFormat::Float32);
        frame.setSampleRate(packet.sampleRate() > 0 ? packet.sampleRate() : 8000);
        frame.setChannelCount(packet.channelCount() > 0 ? packet.channelCount() : 1);
        frame.setTimestamp(packet.pts());
        frame.setPayload(SwByteArray(reinterpret_cast<const char*>(samples.data()),
                                     samples.size() * sizeof(float)));
        return true;
    }

private:
    static float aLawToFloat_(uint8_t value) {
        value ^= 0x55;
        int sign = value & 0x80;
        int exponent = (value & 0x70) >> 4;
        int mantissa = value & 0x0F;
        int sample = 0;
        if (exponent == 0) {
            sample = (mantissa << 4) + 8;
        } else {
            sample = ((mantissa << 4) + 0x108) << (exponent - 1);
        }
        if (sign == 0) {
            sample = -sample;
        }
        return std::max(-1.0f, std::min(1.0f, static_cast<float>(sample) / 32768.0f));
    }
};

inline bool swRegisterBuiltinAudioDecoders() {
    static bool registered = false;
    if (registered) {
        return true;
    }
    registered = true;
    SwAudioDecoderFactory::instance().registerDecoder(
        SwAudioPacket::Codec::PCMU,
        "builtin-pcmu",
        "Builtin PCMU",
        []() { return std::make_shared<SwBuiltinPcmuAudioDecoder>(); },
        100);
    SwAudioDecoderFactory::instance().registerDecoder(
        SwAudioPacket::Codec::PCMA,
        "builtin-pcma",
        "Builtin PCMA",
        []() { return std::make_shared<SwBuiltinPcmaAudioDecoder>(); },
        100);
    return true;
}

static const bool g_swBuiltinAudioDecodersRegistered = swRegisterBuiltinAudioDecoders();

#include "media/SwPlatformAudioDecoder.h"
