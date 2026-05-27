#pragma once

/**
 * @file src/media/encoder/SwAv1LiveEncoder.h
 * @brief Live AV1 encoder front-end with bitrate/key-frame control.
 */

#include "media/encoder/SwVideoEncoder.h"

#include <algorithm>
#include <memory>

class SwAv1LiveEncoderBackend {
public:
    virtual ~SwAv1LiveEncoderBackend() = default;

    virtual SwString name() const = 0;
    virtual bool configure(const SwVideoEncoderConfig& config) = 0;
    virtual bool encodeFrame(const SwVideoFrame& frame, SwVideoPacket& outPacket) = 0;
    virtual void flush() {}
    virtual void requestKeyFrame() {}
    virtual bool setTargetBitrateKbps(uint32_t bitrateKbps) {
        (void)bitrateKbps;
        return true;
    }
};

class SwAv1LiveEncoder : public SwVideoEncoder {
public:
    explicit SwAv1LiveEncoder(std::shared_ptr<SwAv1LiveEncoderBackend> backend = nullptr)
        : m_backend(std::move(backend)) {}

    SwString name() const override {
        return m_backend ? SwString("SwAv1LiveEncoder/") + m_backend->name()
                         : SwString("SwAv1LiveEncoder");
    }

    void setBackend(std::shared_ptr<SwAv1LiveEncoderBackend> backend) {
        m_backend = std::move(backend);
        if (m_configured && m_backend) {
            m_backend->configure(m_config);
            if (m_targetBitrateKbps != 0U) {
                m_backend->setTargetBitrateKbps(m_targetBitrateKbps);
            }
        }
    }

    bool configure(const SwVideoEncoderConfig& config) override {
        if (!config.isValid() || config.codec != SwVideoPacket::Codec::AV1) {
            return false;
        }
        m_config = config;
        m_targetBitrateKbps = clampBitrate_(config.startBitrateKbps);
        m_configured = true;
        if (m_backend && !m_backend->configure(m_config)) {
            m_configured = false;
            return false;
        }
        if (m_backend) {
            m_backend->setTargetBitrateKbps(m_targetBitrateKbps);
        }
        return true;
    }

    bool encodeFrame(const SwVideoFrame& frame, SwVideoPacket& outPacket) override {
        if (!m_configured || !m_backend || !frame.isValid()) {
            return false;
        }
        if (!m_backend->encodeFrame(frame, outPacket)) {
            return false;
        }
        if (outPacket.codec() != SwVideoPacket::Codec::AV1 || outPacket.payload().isEmpty()) {
            outPacket = SwVideoPacket();
            return false;
        }
        if (m_forceNextKeyFrame) {
            outPacket.setKeyFrame(true);
            m_forceNextKeyFrame = false;
        }
        return true;
    }

    void flush() override {
        if (m_backend) {
            m_backend->flush();
        }
    }

    void requestKeyFrame() override {
        m_forceNextKeyFrame = true;
        if (m_backend) {
            m_backend->requestKeyFrame();
        }
    }

    bool setTargetBitrateKbps(uint32_t bitrateKbps) override {
        if (!m_configured) {
            m_targetBitrateKbps = bitrateKbps;
            return true;
        }
        m_targetBitrateKbps = clampBitrate_(bitrateKbps);
        return !m_backend || m_backend->setTargetBitrateKbps(m_targetBitrateKbps);
    }

    const SwVideoEncoderConfig& config() const {
        return m_config;
    }

    bool hasBackend() const {
        return static_cast<bool>(m_backend);
    }

private:
    uint32_t clampBitrate_(uint32_t bitrateKbps) const {
        if (!m_configured && m_config.minBitrateKbps == 0U && m_config.maxBitrateKbps == 0U) {
            return bitrateKbps;
        }
        const uint32_t minBitrate = m_config.minBitrateKbps == 0U ? bitrateKbps
                                                                   : m_config.minBitrateKbps;
        const uint32_t maxBitrate = m_config.maxBitrateKbps == 0U ? bitrateKbps
                                                                   : m_config.maxBitrateKbps;
        return std::min(std::max(bitrateKbps, minBitrate), maxBitrate);
    }

    SwVideoEncoderConfig m_config{};
    std::shared_ptr<SwAv1LiveEncoderBackend> m_backend{};
    bool m_configured{false};
    bool m_forceNextKeyFrame{false};
};
