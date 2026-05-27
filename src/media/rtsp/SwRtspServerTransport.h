#pragma once

/**
 * @file src/media/rtsp/SwRtspServerTransport.h
 * @brief RTSP media-plane transport implementation of the common media server interface.
 */

#include "media/rtp/SwRtpServerTransport.h"

class SwRtspServerTransport : public SwRtpServerTransport {
public:
    SwRtspServerTransport()
        : SwRtpServerTransport("rtsp") {}
};
