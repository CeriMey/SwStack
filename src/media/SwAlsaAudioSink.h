#pragma once

/**
 * @file src/media/SwAlsaAudioSink.h
 * @ingroup media
 * @brief Declares a Linux ALSA-backed audio sink used by SwAudioOutput.
 *
 * The sink renders interleaved Float32 PCM through `libasound`, loaded with `dlopen` at
 * runtime: there is no build-time or link-time dependency on ALSA. When the library (or a
 * playback device) is unavailable, `open()` fails gracefully and
 * `SwAlsaAudioSink::runtimeAvailable()` reports `false`, letting callers fall back to
 * `SwNullAudioSink`.
 *
 * Only a minimal, ABI-stable subset of the ALSA PCM API is declared locally (the ALSA PCM
 * ABI has been frozen for over a decade), mirroring the dlopen pattern used by
 * `SwLinuxVideoDecoder`.
 */

#include "media/SwAudioSink.h"

#if defined(__linux__)

#include <dlfcn.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

static constexpr const char* kSwLogCategory_SwAlsaAudioSink = "sw.media.swalsaaudiosink";

/**
 * @brief Runtime loader for the subset of libasound used by SwAlsaAudioSink.
 */
class SwAlsaLibrary {
public:
    // ABI-frozen constants from <alsa/pcm.h>.
    static const int kStreamPlayback = 0;      // SND_PCM_STREAM_PLAYBACK
    static const int kAccessRwInterleaved = 3; // SND_PCM_ACCESS_RW_INTERLEAVED
    static const int kFormatFloatLe = 14;      // SND_PCM_FORMAT_FLOAT_LE
    static const int kFormatFloatBe = 15;      // SND_PCM_FORMAT_FLOAT_BE

    using PcmOpenFn = int (*)(void** pcm, const char* name, int stream, int mode);
    using PcmSetParamsFn = int (*)(void* pcm,
                                   int format,
                                   int access,
                                   unsigned int channels,
                                   unsigned int rate,
                                   int softResample,
                                   unsigned int latencyUs);
    using PcmWriteiFn = long (*)(void* pcm, const void* buffer, unsigned long frames);
    using PcmRecoverFn = int (*)(void* pcm, int err, int silent);
    using PcmSimpleFn = int (*)(void* pcm);
    using PcmDelayFn = int (*)(void* pcm, long* delayFrames);

    PcmOpenFn pcmOpen{nullptr};
    PcmSetParamsFn pcmSetParams{nullptr};
    PcmWriteiFn pcmWritei{nullptr};
    PcmRecoverFn pcmRecover{nullptr};
    PcmSimpleFn pcmPrepare{nullptr};
    PcmSimpleFn pcmDrop{nullptr};
    PcmSimpleFn pcmDrain{nullptr};
    PcmSimpleFn pcmClose{nullptr};
    PcmDelayFn pcmDelay{nullptr};

    static SwAlsaLibrary& instance() {
        static SwAlsaLibrary g_library;
        return g_library;
    }

    bool available() const { return m_available; }

    static int nativeFloatFormat() {
        const unsigned int probe = 1;
        const bool littleEndian = (*reinterpret_cast<const unsigned char*>(&probe) == 1);
        return littleEndian ? kFormatFloatLe : kFormatFloatBe;
    }

private:
    SwAlsaLibrary() {
        static const char* kCandidates[] = {"libasound.so.2", "libasound.so"};
        for (std::size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i) {
            m_handle = ::dlopen(kCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_handle) {
                break;
            }
        }
        if (!m_handle) {
            return;
        }
        pcmOpen = reinterpret_cast<PcmOpenFn>(::dlsym(m_handle, "snd_pcm_open"));
        pcmSetParams = reinterpret_cast<PcmSetParamsFn>(::dlsym(m_handle, "snd_pcm_set_params"));
        pcmWritei = reinterpret_cast<PcmWriteiFn>(::dlsym(m_handle, "snd_pcm_writei"));
        pcmRecover = reinterpret_cast<PcmRecoverFn>(::dlsym(m_handle, "snd_pcm_recover"));
        pcmPrepare = reinterpret_cast<PcmSimpleFn>(::dlsym(m_handle, "snd_pcm_prepare"));
        pcmDrop = reinterpret_cast<PcmSimpleFn>(::dlsym(m_handle, "snd_pcm_drop"));
        pcmDrain = reinterpret_cast<PcmSimpleFn>(::dlsym(m_handle, "snd_pcm_drain"));
        pcmClose = reinterpret_cast<PcmSimpleFn>(::dlsym(m_handle, "snd_pcm_close"));
        pcmDelay = reinterpret_cast<PcmDelayFn>(::dlsym(m_handle, "snd_pcm_delay"));
        m_available = pcmOpen && pcmSetParams && pcmWritei && pcmRecover && pcmPrepare &&
                      pcmDrop && pcmClose;
    }

    void* m_handle{nullptr};
    bool m_available{false};
};

class SwAlsaAudioSink : public SwAudioSink {
public:
    explicit SwAlsaAudioSink(const char* deviceName = "default")
        : m_deviceName(deviceName ? deviceName : "default") {}

    ~SwAlsaAudioSink() override {
        close();
    }

    const char* name() const override { return "SwAlsaAudioSink"; }

    /**
     * @brief Returns whether libasound is loadable on this machine.
     */
    static bool runtimeAvailable() {
        return SwAlsaLibrary::instance().available();
    }

    /**
     * @brief Returns whether the default playback device can actually be opened.
     *
     * Containers and headless machines often ship libasound without any sound card;
     * probing once here lets SwAudioOutput fall back to SwNullAudioSink instead of
     * retrying (and logging) a doomed snd_pcm_open on every audio packet.
     */
    static bool defaultDeviceUsable() {
        static const bool g_usable = probeDefaultDevice_();
        return g_usable;
    }

    bool open(int sampleRate, int channelCount) override {
        close();
        if (sampleRate <= 0 || channelCount <= 0) {
            return false;
        }
        SwAlsaLibrary& alsa = SwAlsaLibrary::instance();
        if (!alsa.available()) {
            logOnce_("libasound is not available; audio output disabled.");
            return false;
        }

        int err = alsa.pcmOpen(&m_pcm, m_deviceName.c_str(), SwAlsaLibrary::kStreamPlayback, 0);
        if (err < 0 || !m_pcm) {
            swCWarning(kSwLogCategory_SwAlsaAudioSink)
                << "[SwAlsaAudioSink] snd_pcm_open(" << m_deviceName << ") failed: " << err;
            m_pcm = nullptr;
            return false;
        }

        const unsigned int latencyUs = 50000; // 50 ms of device buffering (low-latency bias)
        err = alsa.pcmSetParams(m_pcm,
                                SwAlsaLibrary::nativeFloatFormat(),
                                SwAlsaLibrary::kAccessRwInterleaved,
                                static_cast<unsigned int>(channelCount),
                                static_cast<unsigned int>(sampleRate),
                                1,
                                latencyUs);
        if (err < 0) {
            swCWarning(kSwLogCategory_SwAlsaAudioSink)
                << "[SwAlsaAudioSink] snd_pcm_set_params(" << sampleRate << "Hz x"
                << channelCount << ") failed: " << err;
            alsa.pcmClose(m_pcm);
            m_pcm = nullptr;
            return false;
        }

        m_sampleRate = sampleRate;
        m_channelCount = channelCount;
        m_bytesPerFrame = static_cast<std::size_t>(channelCount) * sizeof(float);
        m_stopRequested.store(false);
        m_failed.store(false);
        m_flushRequested.store(false);
        m_worker = std::thread([this]() { workerLoop_(); });
        m_open = true;
        return true;
    }

    void close() override {
        m_stopRequested.store(true);
        m_queueCv.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
        if (m_pcm) {
            SwAlsaLibrary& alsa = SwAlsaLibrary::instance();
            alsa.pcmDrop(m_pcm);
            alsa.pcmClose(m_pcm);
            m_pcm = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_chunks.clear();
        }
        m_open = false;
        m_failed.store(false);
        m_flushRequested.store(false);
        m_sampleRate = 0;
        m_channelCount = 0;
        m_bytesPerFrame = 0;
        m_lastPlayedTimestamp.store(-1);
    }

    void flush() override {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_chunks.clear();
        }
        m_lastPlayedTimestamp.store(-1);
        if (!m_open || !m_pcm) {
            return;
        }
        // ALSA PCM handles are not thread-safe: the drop/prepare is executed by the worker
        // thread (the only thread that touches m_pcm after open), not here.
        m_flushRequested.store(true);
        m_queueCv.notify_all();
    }

    bool pushFrame(const SwAudioFrame& frame) override {
        if (!m_open || m_failed.load() || !frame.isValid() ||
            frame.sampleFormat() != SwAudioFrame::SampleFormat::Float32) {
            return false;
        }
        if (frame.sampleRate() != m_sampleRate || frame.channelCount() != m_channelCount) {
            return false;
        }
        QueuedChunk chunk;
        chunk.payload = frame.payload();
        chunk.timestamp = frame.timestamp();
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_chunks.push_back(std::move(chunk));
        }
        m_queueCv.notify_one();
        return true;
    }

    std::int64_t playedTimestamp() const override {
        return m_lastPlayedTimestamp.load();
    }

private:
    struct QueuedChunk {
        SwByteArray payload{};
        std::int64_t timestamp{-1};
    };

    void workerLoop_() {
        SwAlsaLibrary& alsa = SwAlsaLibrary::instance();
        while (!m_stopRequested.load()) {
            if (m_flushRequested.exchange(false)) {
                alsa.pcmDrop(m_pcm);
                alsa.pcmPrepare(m_pcm);
            }

            QueuedChunk chunk;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_queueCv.wait(lock, [this]() {
                    return m_stopRequested.load() || m_flushRequested.load() ||
                           !m_chunks.empty();
                });
                if (m_stopRequested.load()) {
                    break;
                }
                if (m_flushRequested.load() || m_chunks.empty()) {
                    continue;
                }
                chunk = std::move(m_chunks.front());
                m_chunks.pop_front();
            }

            const char* data = chunk.payload.constData();
            unsigned long framesLeft =
                static_cast<unsigned long>(chunk.payload.size()) / m_bytesPerFrame;
            while (framesLeft > 0 && !m_stopRequested.load() && !m_flushRequested.load()) {
                const long written = alsa.pcmWritei(m_pcm, data, framesLeft);
                if (written < 0) {
                    const int recovered = alsa.pcmRecover(m_pcm, static_cast<int>(written), 1);
                    if (recovered < 0) {
                        swCWarning(kSwLogCategory_SwAlsaAudioSink)
                            << "[SwAlsaAudioSink] unrecoverable write error: " << recovered;
                        // Latch the failure so pushFrame() stops accepting (and thus
                        // queueing) audio nobody will ever drain.
                        m_failed.store(true);
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_chunks.clear();
                        return;
                    }
                    continue;
                }
                framesLeft -= static_cast<unsigned long>(written);
                data += static_cast<std::size_t>(written) * m_bytesPerFrame;
            }
            m_lastPlayedTimestamp.store(chunk.timestamp);
        }
    }

    static bool probeDefaultDevice_() {
        SwAlsaLibrary& alsa = SwAlsaLibrary::instance();
        if (!alsa.available()) {
            return false;
        }
        void* pcm = nullptr;
        const int err = alsa.pcmOpen(&pcm, "default", SwAlsaLibrary::kStreamPlayback, 0);
        if (err < 0 || !pcm) {
            swCWarning(kSwLogCategory_SwAlsaAudioSink)
                << "[SwAlsaAudioSink] no usable playback device (snd_pcm_open: " << err
                << "); audio output disabled.";
            return false;
        }
        alsa.pcmClose(pcm);
        return true;
    }

    static void logOnce_(const char* message) {
        static std::atomic<bool> g_logged{false};
        bool expected = false;
        if (g_logged.compare_exchange_strong(expected, true)) {
            swCWarning(kSwLogCategory_SwAlsaAudioSink) << "[SwAlsaAudioSink] " << message;
        }
    }

    std::string m_deviceName;
    void* m_pcm{nullptr};
    bool m_open{false};
    int m_sampleRate{0};
    int m_channelCount{0};
    std::size_t m_bytesPerFrame{0};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_failed{false};
    std::atomic<bool> m_flushRequested{false};
    std::thread m_worker;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<QueuedChunk> m_chunks;
    std::atomic<std::int64_t> m_lastPlayedTimestamp{-1};
};

#endif
