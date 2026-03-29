#pragma once

/**
 * @file src/media/SwAudioPipeline.h
 * @ingroup media
 * @brief Declares the generic audio pipeline used by SwMediaPlayer.
 */

#include "SwDebug.h"
#include "media/SwAudioDecoder.h"
#include "media/SwAudioOutput.h"
#include "media/SwMediaPacket.h"

#include <map>
#include <memory>

static constexpr const char* kSwLogCategory_SwAudioPipeline = "sw.media.swaudiopipeline";

class SwAudioPipeline {
public:
    void setAudioOutput(const std::shared_ptr<SwAudioOutput>& output) {
        m_audioOutput = output;
    }

    bool setDecoderSelection(SwAudioPacket::Codec codec, const SwString& decoderId) {
        if (decoderId.isEmpty()) {
            m_decoderSelectionIds.erase(codec);
            return true;
        }
        if (!SwAudioDecoderFactory::instance().contains(codec, decoderId)) {
            return false;
        }
        m_decoderSelectionIds[codec] = decoderId;
        if (m_decoder && m_decoderCodec == codec) {
            m_decoder.reset();
            m_decoderCodec = SwAudioPacket::Codec::Unknown;
        }
        return true;
    }

    void clearDecoderSelection(SwAudioPacket::Codec codec) {
        m_decoderSelectionIds.erase(codec);
        if (m_decoder && m_decoderCodec == codec) {
            m_decoder.reset();
            m_decoderCodec = SwAudioPacket::Codec::Unknown;
        }
    }

    SwString decoderSelection(SwAudioPacket::Codec codec) const {
        auto it = m_decoderSelectionIds.find(codec);
        if (it == m_decoderSelectionIds.end()) {
            return SwString();
        }
        return it->second;
    }

    bool handleMediaPacket(const SwMediaPacket& mediaPacket) {
        if (mediaPacket.type() != SwMediaPacket::Type::Audio) {
            return false;
        }
        SwAudioPacket packet;
        packet.setTrackId(mediaPacket.trackId());
        packet.setPayload(mediaPacket.payload());
        packet.setPts(mediaPacket.pts());
        packet.setDts(mediaPacket.dts());
        packet.setDiscontinuity(mediaPacket.isDiscontinuity());
        packet.setPayloadType(mediaPacket.payloadType());
        packet.setClockRate(mediaPacket.clockRate());
        packet.setSampleRate(mediaPacket.sampleRate());
        packet.setChannelCount(mediaPacket.channelCount());
        packet.setCodec(codecFromName_(mediaPacket.codec()));
        return handleAudioPacket(packet);
    }

    bool handleAudioPacket(const SwAudioPacket& packet) {
        if (!m_audioOutput) {
            return false;
        }
        SwAudioFrame frame;
        if (packet.codec() == SwAudioPacket::Codec::RawF32) {
            frame.setSampleFormat(SwAudioFrame::SampleFormat::Float32);
            frame.setSampleRate(packet.sampleRate());
            frame.setChannelCount(packet.channelCount());
            frame.setTimestamp(packet.pts());
            frame.setPayload(packet.payload());
        } else {
            auto decoder = acquireDecoder_(packet.codec());
            if (!decoder) {
                if (!m_loggedUnsupported[packet.codec()]) {
                    m_loggedUnsupported[packet.codec()] = true;
                    swCWarning(kSwLogCategory_SwAudioPipeline)
                        << "[SwAudioPipeline] No audio decoder available codec="
                        << static_cast<int>(packet.codec());
                }
                return false;
            }
            if (!decoder->decode(packet, frame)) {
                swCWarning(kSwLogCategory_SwAudioPipeline)
                    << "[SwAudioPipeline] Audio decode failed decoder=" << decoder->name()
                    << " codec=" << static_cast<int>(packet.codec())
                    << " bytes=" << packet.payload().size();
                return false;
            }
        }

        if (!frame.isValid()) {
            return false;
        }
        if (!m_outputOpened || frame.sampleRate() != m_openSampleRate || frame.channelCount() != m_openChannelCount) {
            m_audioOutput->stop();
            if (!m_audioOutput->start(frame.sampleRate(), frame.channelCount())) {
                swCWarning(kSwLogCategory_SwAudioPipeline)
                    << "[SwAudioPipeline] Failed to open audio output rate="
                    << frame.sampleRate() << " channels=" << frame.channelCount();
                return false;
            }
            m_outputOpened = true;
            m_openSampleRate = frame.sampleRate();
            m_openChannelCount = frame.channelCount();
        }
        return m_audioOutput->pushFrame(frame);
    }

private:
    std::shared_ptr<SwAudioDecoder> acquireDecoder_(SwAudioPacket::Codec codec) {
        if (m_decoder && m_decoderCodec == codec) {
            return m_decoder;
        }
        const auto selectionIt = m_decoderSelectionIds.find(codec);
        if (selectionIt != m_decoderSelectionIds.end() && !selectionIt->second.isEmpty()) {
            m_decoder = SwAudioDecoderFactory::instance().acquire(codec, selectionIt->second);
        } else {
            m_decoder = SwAudioDecoderFactory::instance().acquire(codec);
        }
        m_decoderCodec = m_decoder ? codec : SwAudioPacket::Codec::Unknown;
        return m_decoder;
    }

    static SwAudioPacket::Codec codecFromName_(const SwString& codecName) {
        const SwString normalized = codecName.trimmed().toLower();
        if (normalized == "pcmu" || normalized == "g711u" || normalized == "mulaw") {
            return SwAudioPacket::Codec::PCMU;
        }
        if (normalized == "pcma" || normalized == "g711a" || normalized == "alaw") {
            return SwAudioPacket::Codec::PCMA;
        }
        if (normalized == "opus") {
            return SwAudioPacket::Codec::Opus;
        }
        if (normalized == "aac" || normalized == "mpeg4-generic" || normalized == "mp4a-latm") {
            return SwAudioPacket::Codec::AAC;
        }
        if (normalized == "raw-f32") {
            return SwAudioPacket::Codec::RawF32;
        }
        return SwAudioPacket::Codec::Unknown;
    }

    std::shared_ptr<SwAudioOutput> m_audioOutput;
    std::shared_ptr<SwAudioDecoder> m_decoder;
    SwAudioPacket::Codec m_decoderCodec{SwAudioPacket::Codec::Unknown};
    std::map<SwAudioPacket::Codec, SwString> m_decoderSelectionIds{};
    std::map<SwAudioPacket::Codec, bool> m_loggedUnsupported{};
    bool m_outputOpened{false};
    int m_openSampleRate{0};
    int m_openChannelCount{0};
};
