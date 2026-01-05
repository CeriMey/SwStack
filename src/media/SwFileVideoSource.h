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
    SwFileVideoSource(const std::string& filePath,
                      SwVideoPacket::Codec codec = SwVideoPacket::Codec::Unknown,
                      std::size_t packetSize = 64 * 1024)
        : m_path(filePath)
        , m_codec(codec)
        , m_packetSize(packetSize > 0 ? packetSize : 64 * 1024) {}

    ~SwFileVideoSource() override {
        stop();
    }

    std::string name() const override {
        return "SwFileVideoSource";
    }

    void start() override {
        if (isRunning()) {
            return;
        }
        if (m_path.empty()) {
            swCError(kSwLogCategory_SwFileVideoSource) << "[SwFileVideoSource] empty file path.";
            return;
        }
        setRunning(true);
        m_worker = std::thread([this]() { streamLoop(); });
    }

    void stop() override {
        if (!isRunning()) {
            return;
        }
        setRunning(false);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    void setLoop(bool loop) {
        m_loop.store(loop);
    }

private:
    void streamLoop() {
        while (isRunning()) {
            std::ifstream input(m_path, std::ios::binary);
            if (!input) {
                swCError(kSwLogCategory_SwFileVideoSource) << "[SwFileVideoSource] failed to open file: " << m_path;
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
                emitPacket(packet);
                pts += static_cast<std::size_t>(bytesRead);
            }

            if (!m_loop.load()) {
                break;
            }
        }
        setRunning(false);
    }

    std::string m_path;
    SwVideoPacket::Codec m_codec{SwVideoPacket::Codec::Unknown};
    std::size_t m_packetSize{64 * 1024};
    std::atomic<bool> m_loop{false};
    std::thread m_worker;
};
