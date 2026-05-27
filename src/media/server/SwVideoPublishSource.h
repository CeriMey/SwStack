#pragma once

/**
 * @file src/media/server/SwVideoPublishSource.h
 * @brief Source interface for media publishing pipelines.
 */

#include "core/types/SwString.h"
#include "media/SwVideoPacket.h"

#include <functional>

class SwVideoPublishSource {
public:
    using PacketCallback = std::function<void(const SwString&, const SwVideoPacket&)>;

    virtual ~SwVideoPublishSource() = default;
    virtual SwString name() const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

    void setPacketCallback(PacketCallback callback) {
        m_packetCallback = std::move(callback);
    }

protected:
    void emitVideoPacket(const SwString& streamId, const SwVideoPacket& packet) {
        if (m_packetCallback) {
            m_packetCallback(streamId, packet);
        }
    }

private:
    PacketCallback m_packetCallback{};
};
