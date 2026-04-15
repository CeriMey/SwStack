#pragma once

/**
 * @file src/media/SwPlatformVideoDecoderIds.h
 * @ingroup media
 * @brief Declares backend-neutral video decoder identifiers used by media pipelines.
 */

#include "core/types/SwString.h"

inline SwString swPlatformVideoDecoderId() {
    return "platform";
}

inline SwString swPlatformHardwareVideoDecoderId() {
    return "platform-hardware";
}

inline SwString swPlatformSoftwareVideoDecoderId() {
    return "platform-software";
}

inline bool swIsPlatformHardwareVideoDecoderId(const SwString& decoderId) {
    return decoderId == swPlatformHardwareVideoDecoderId() ||
           decoderId == "media-foundation-hardware";
}

inline bool swIsPlatformSoftwareVideoDecoderId(const SwString& decoderId) {
    return decoderId == swPlatformSoftwareVideoDecoderId() ||
           decoderId == "media-foundation-software";
}

inline bool swIsPlatformAutoVideoDecoderId(const SwString& decoderId) {
    return decoderId.isEmpty() ||
           decoderId == swPlatformVideoDecoderId() ||
           decoderId == "media-foundation";
}
