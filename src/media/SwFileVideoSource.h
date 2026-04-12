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
 * @file src/media/SwFileVideoSource.h
 * @ingroup media
 * @brief Declares the public interface exposed by SwFileVideoSource in the CoreSw media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the file video source interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwFileVideoSource.
 *
 * Source-oriented declarations here describe how data or media is produced over time, how
 * consumers observe availability, and which lifetime guarantees apply to delivered payloads.
 *
 * Media-facing declarations here focus on packet and frame ownership, format description,
 * decoding boundaries, and real-time source control.
 *
 */


/***************************************************************************************************
 * SwFileVideoSource
 * Simple SwVideoSource implementation that streams raw bytes from a file to the decoder.
 *
 * The goal is to provide a lightweight source abstraction for future work where the decoder will
 * interpret the payload (e.g. by delegating to FFmpeg, GStreamer, etc.). For now, this component
 * simply opens a file, reads it sequentially, and emits SwVideoPacket chunks.
 ***************************************************************************************************/

#include "media/SwVideoSource.h"
#include "media/SwVideoPacket.h"
#include "SwByteArray.h"
#include "SwDebug.h"

#include <atomic>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
static constexpr const char* kSwLogCategory_SwFileVideoSource = "sw.media.swfilevideosource";


class SwFileVideoSource : public SwVideoSource {
public:
    /**
     * @brief Constructs a `SwFileVideoSource` instance.
     * @param filePath Path of the target file.
     * @param codec Value passed to the method.
     * @param packetSize Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwFileVideoSource(const std::string& filePath,
                      SwVideoPacket::Codec codec = SwVideoPacket::Codec::Unknown,
                      std::size_t packetSize = 64 * 1024)
        : m_path(filePath)
        , m_codec(codec)
        , m_packetSize(packetSize > 0 ? packetSize : 64 * 1024) {
        publishTracks_();
    }

    /**
     * @brief Destroys the `SwFileVideoSource` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwFileVideoSource() override {
        stop();
    }

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString name() const override {
        return "SwFileVideoSource";
    }

    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() override {
        if (isRunning()) {
            return;
        }
        if (m_path.empty()) {
            swCError(kSwLogCategory_SwFileVideoSource) << "[SwFileVideoSource] empty file path.";
            emitStatus(StreamState::Recovering, "Empty file path");
            return;
        }
        publishTracks_();
        emitStatus(StreamState::Connecting, "Opening file source...");
        setRunning(true);
        m_worker = std::thread([this]() { streamLoop(); });
    }

    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() override {
        if (!isRunning()) {
            return;
        }
        setRunning(false);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    /**
     * @brief Sets the loop.
     * @param loop Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLoop(bool loop) {
        m_loop.store(loop);
    }

private:
    static SwString codecName_(SwVideoPacket::Codec codec) {
        switch (codec) {
        case SwVideoPacket::Codec::H264:
            return "h264";
        case SwVideoPacket::Codec::H265:
            return "h265";
        case SwVideoPacket::Codec::VP8:
            return "vp8";
        case SwVideoPacket::Codec::VP9:
            return "vp9";
        case SwVideoPacket::Codec::AV1:
            return "av1";
        case SwVideoPacket::Codec::MotionJPEG:
            return "mjpeg";
        case SwVideoPacket::Codec::RawBGRA:
            return "raw-bgra";
        case SwVideoPacket::Codec::RawRGBA:
            return "raw-rgba";
        case SwVideoPacket::Codec::RawBGR:
            return "raw-bgr";
        case SwVideoPacket::Codec::RawRGB:
            return "raw-rgb";
        case SwVideoPacket::Codec::Unknown:
        default:
            return "unknown";
        }
    }

    void publishTracks_() {
        SwMediaTrack track;
        track.id = "file-video-0";
        track.type = SwMediaTrack::Type::Video;
        track.codec = codecName_(m_codec);
        track.selected = true;
        track.availability = SwMediaTrack::Availability::Available;
        SwList<SwMediaTrack> tracks;
        tracks.append(track);
        setTracks(tracks);
    }

    void streamLoop() {
        bool reachedEndOfFile = false;
        while (isRunning()) {
            std::ifstream input(m_path, std::ios::binary);
            if (!input) {
                swCError(kSwLogCategory_SwFileVideoSource) << "[SwFileVideoSource] failed to open file: " << m_path;
                emitStatus(StreamState::Recovering, "Failed to open file");
                break;
            }

            std::vector<char> buffer(m_packetSize);
            std::size_t pts = 0;
            while (isRunning() && input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                std::streamsize bytesRead = input.gcount();
                if (bytesRead <= 0) {
                    break;
                }

                SwByteArray payload(buffer.data(), static_cast<std::size_t>(bytesRead));
                SwVideoPacket packet(m_codec, payload, static_cast<std::int64_t>(pts));
                emitStatus(StreamState::Streaming, "Streaming");
                emitPacket(packet);
                pts += static_cast<std::size_t>(bytesRead);
            }

            if (!m_loop.load()) {
                reachedEndOfFile = true;
                break;
            }
        }
        setRunning(false);
        emitStatus(StreamState::Stopped,
                   reachedEndOfFile ? SwString("End of file") : SwString("Stream stopped"));
    }

    std::string m_path;
    SwVideoPacket::Codec m_codec{SwVideoPacket::Codec::Unknown};
    std::size_t m_packetSize{64 * 1024};
    std::atomic<bool> m_loop{false};
    std::thread m_worker;
};
