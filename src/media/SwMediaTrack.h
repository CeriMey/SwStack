#pragma once

/**
 * @file src/media/SwMediaTrack.h
 * @ingroup media
 * @brief Declares the generic media-track descriptor used by SwMediaSource and SwMediaPlayer.
 */

#include "core/types/SwString.h"

class SwMediaTrack {
public:
    enum class Type {
        Unknown,
        Audio,
        Video,
        Metadata,
        Subtitle
    };

    enum class Availability {
        Unknown,
        Available,
        Unsupported
    };

    SwString id{};
    Type type{Type::Unknown};
    SwString codec{};
    int payloadType{-1};
    int clockRate{0};
    int sampleRate{0};
    int channelCount{0};
    SwString language{};
    SwString control{};
    SwString fmtp{};
    bool selected{false};
    Availability availability{Availability::Unknown};

    bool isValid() const {
        return !id.isEmpty() && type != Type::Unknown;
    }

    bool isAudio() const { return type == Type::Audio; }
    bool isVideo() const { return type == Type::Video; }
    bool isMetadata() const { return type == Type::Metadata; }
    bool isSubtitle() const { return type == Type::Subtitle; }
};
