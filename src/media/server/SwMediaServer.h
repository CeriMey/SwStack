#pragma once

/**
 * @file src/media/server/SwMediaServer.h
 * @brief High-level media publishing session.
 */

#include "core/types/SwList.h"
#include "media/encoder/SwVideoEncoder.h"
#include "media/server/SwMediaServerConfig.h"
#include "media/server/SwVideoPublishSource.h"
#include "media/server/SwVideoPublishStream.h"
#include "media/server/SwVideoTransportServer.h"

#include <functional>
#include <memory>

class SwMediaServer {
public:
    explicit SwMediaServer(const SwMediaServerConfig& config = SwMediaServerConfig())
        : m_config(config) {}

    void setConfig(const SwMediaServerConfig& config) {
        m_config = config;
    }

    const SwMediaServerConfig& config() const {
        return m_config;
    }

    void setTransport(std::shared_ptr<SwVideoTransportServer> transport) {
        m_transport = std::move(transport);
        installTransportCallbacks_();
    }

    void setVideoSource(std::shared_ptr<SwVideoPublishSource> source) {
        m_source = std::move(source);
    }

    void setVideoEncoder(std::shared_ptr<SwVideoEncoder> encoder) {
        m_encoder = std::move(encoder);
    }

    void setClientFeedbackCallback(SwVideoTransportServer::ClientFeedbackCallback callback) {
        m_clientFeedbackCallback = std::move(callback);
        installTransportCallbacks_();
    }

    bool addVideoStream(const SwVideoPublishStream& stream) {
        if (!stream.isValid()) {
            return false;
        }
        m_streams.append(stream);
        if (m_transport) {
            return m_transport->addStream(stream);
        }
        return true;
    }

    bool addMetadataTrack(const SwMediaTrack& track) {
        if (!track.isValid() || !track.isMetadata()) {
            return false;
        }
        m_metadataTracks.append(track);
        if (m_transport) {
            return m_transport->addMetadataTrack(track);
        }
        return true;
    }

    bool start() {
        if (!m_transport) {
            return false;
        }
        if (!m_transport->configure(m_config)) {
            return false;
        }
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            if (!m_transport->addStream(*it)) {
                return false;
            }
        }
        for (auto it = m_metadataTracks.begin(); it != m_metadataTracks.end(); ++it) {
            if (!m_transport->addMetadataTrack(*it)) {
                return false;
            }
        }
        if (m_source) {
            m_source->setPacketCallback([this](const SwString& streamId,
                                               const SwVideoPacket& packet) {
                publishVideoPacket(streamId, packet);
            });
        }
        if (!m_transport->start()) {
            return false;
        }
        if (m_source && !m_source->start()) {
            m_transport->stop();
            return false;
        }
        return true;
    }

    void stop() {
        if (m_source) {
            m_source->stop();
        }
        if (m_transport) {
            m_transport->stop();
        }
    }

    bool publishVideoPacket(const SwString& streamId, const SwVideoPacket& packet) {
        return m_transport && m_transport->publishVideoPacket(streamId, packet);
    }

    bool publishMediaPacket(const SwString& trackId, const SwMediaPacket& packet) {
        return m_transport && m_transport->publishMediaPacket(trackId, packet);
    }

    SwVideoServerMetrics metrics() const {
        return m_transport ? m_transport->metrics() : SwVideoServerMetrics();
    }

private:
    void installTransportCallbacks_() {
        if (!m_transport) {
            return;
        }
        m_transport->setClientFeedbackCallback([this](const SwVideoServerClientFeedback& feedback) {
            if (m_encoder) {
                if (feedback.targetBitrateKbps != 0U) {
                    m_encoder->setTargetBitrateKbps(feedback.targetBitrateKbps);
                }
                if (feedback.requestKeyFrame) {
                    m_encoder->requestKeyFrame();
                }
            }
            if (m_clientFeedbackCallback) {
                m_clientFeedbackCallback(feedback);
            }
        });
    }

    SwMediaServerConfig m_config{};
    std::shared_ptr<SwVideoTransportServer> m_transport{};
    std::shared_ptr<SwVideoPublishSource> m_source{};
    std::shared_ptr<SwVideoEncoder> m_encoder{};
    SwVideoTransportServer::ClientFeedbackCallback m_clientFeedbackCallback{};
    SwList<SwVideoPublishStream> m_streams{};
    SwList<SwMediaTrack> m_metadataTracks{};
};
