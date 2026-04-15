#pragma once

/**
 * @file src/media/SwLinuxVideoDecoder.h
 * @ingroup media
 * @brief Declares Linux video decoder backends exposed through the generic decoder API.
 */

#include "SwDebug.h"
#include "media/SwHevcBitstream.h"
#include "media/SwPlatformVideoDecoderIds.h"
#include "media/SwVideoDecoder.h"

#include <cstring>
#include <limits>
#include <memory>

static constexpr const char* kSwLogCategory_SwLinuxVideoDecoder = "sw.media.swlinuxvideodecoder";

#if defined(__linux__)

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__has_include)
#  if __has_include(<wels/codec_api.h>)
#    include <wels/codec_api.h>
#    define SW_MEDIA_LINUX_HAS_OPENH264_HEADERS 1
#  else
#    define SW_MEDIA_LINUX_HAS_OPENH264_HEADERS 0
#  endif
#  if __has_include(<libde265/de265.h>)
#    include <libde265/de265.h>
#    define SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS 1
#  else
#    define SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS 0
#  endif
#  if __has_include(<va/va.h>) && __has_include(<va/va_drm.h>) && __has_include(<va/va_drmcommon.h>)
#    include <va/va.h>
#    include <va/va_drm.h>
#    include <va/va_drmcommon.h>
#    define SW_MEDIA_LINUX_HAS_VAAPI_HEADERS 1
#  else
#    define SW_MEDIA_LINUX_HAS_VAAPI_HEADERS 0
#  endif
#else
#  define SW_MEDIA_LINUX_HAS_OPENH264_HEADERS 0
#  define SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS 0
#  define SW_MEDIA_LINUX_HAS_VAAPI_HEADERS 0
#endif

#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
inline bool swLinuxVaapiRuntimeHasSymbol_(const char* symbolName) {
    static const char* kVaCandidates[] = {"libva.so.2", "libva.so"};
    for (std::size_t i = 0; i < (sizeof(kVaCandidates) / sizeof(kVaCandidates[0])); ++i) {
        void* handle = dlopen(kVaCandidates[i], RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        const bool found = dlsym(handle, symbolName) != nullptr;
        dlclose(handle);
        if (found) {
            return true;
        }
    }
    return false;
}
#endif

inline bool swLinuxOpenH264BackendBuilt() {
    return SW_MEDIA_LINUX_HAS_OPENH264_HEADERS != 0;
}

inline bool swLinuxLibDe265BackendBuilt() {
    return SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS != 0;
}

inline bool swLinuxVaapiBackendBuilt() {
    return SW_MEDIA_LINUX_HAS_VAAPI_HEADERS != 0;
}

inline bool swLinuxOpenH264RuntimeAvailable() {
#if !SW_MEDIA_LINUX_HAS_OPENH264_HEADERS
    return false;
#else
    static const char* kLibraryCandidates[] = {
        "libopenh264.so.8",
        "libopenh264.so.7",
        "libopenh264.so.6",
        "libopenh264.so"
    };
    for (std::size_t i = 0; i < (sizeof(kLibraryCandidates) / sizeof(kLibraryCandidates[0])); ++i) {
        void* handle = dlopen(kLibraryCandidates[i], RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        const bool ok = dlsym(handle, "WelsCreateDecoder") != nullptr &&
                        dlsym(handle, "WelsDestroyDecoder") != nullptr;
        dlclose(handle);
        if (ok) {
            return true;
        }
    }
    return false;
#endif
}

inline bool swLinuxLibDe265RuntimeAvailable() {
#if !SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
    return false;
#else
    static const char* kLibraryCandidates[] = {
        "libde265.so.0",
        "libde265.so"
    };
    for (std::size_t i = 0; i < (sizeof(kLibraryCandidates) / sizeof(kLibraryCandidates[0])); ++i) {
        void* handle = dlopen(kLibraryCandidates[i], RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        const bool ok = dlsym(handle, "de265_new_decoder") != nullptr &&
                        dlsym(handle, "de265_free_decoder") != nullptr &&
                        dlsym(handle, "de265_push_data") != nullptr &&
                        dlsym(handle, "de265_decode") != nullptr &&
                        dlsym(handle, "de265_get_next_picture") != nullptr;
        dlclose(handle);
        if (ok) {
            return true;
        }
    }
    return false;
#endif
}

#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
inline bool swLinuxVaapiRuntimeSupportsProfile(VAProfile requestedProfile) {
    static const char* kVaCandidates[] = {"libva.so.2", "libva.so"};
    static const char* kVaDrmCandidates[] = {"libva-drm.so.2", "libva-drm.so"};
    static const char* kRenderNodes[] = {
        "/dev/dri/renderD128",
        "/dev/dri/renderD129",
        "/dev/dri/renderD130"
    };

    void* vaLibrary = nullptr;
    void* vaDrmLibrary = nullptr;
    for (std::size_t i = 0; i < (sizeof(kVaCandidates) / sizeof(kVaCandidates[0])); ++i) {
        vaLibrary = dlopen(kVaCandidates[i], RTLD_NOW | RTLD_LOCAL);
        if (vaLibrary) {
            break;
        }
    }
    for (std::size_t i = 0; i < (sizeof(kVaDrmCandidates) / sizeof(kVaDrmCandidates[0])); ++i) {
        vaDrmLibrary = dlopen(kVaDrmCandidates[i], RTLD_NOW | RTLD_LOCAL);
        if (vaDrmLibrary) {
            break;
        }
    }
    if (!vaLibrary || !vaDrmLibrary) {
        if (vaDrmLibrary) {
            dlclose(vaDrmLibrary);
        }
        if (vaLibrary) {
            dlclose(vaLibrary);
        }
        return false;
    }

    auto vaGetDisplayDrmFn =
        reinterpret_cast<VADisplay(*)(int)>(dlsym(vaDrmLibrary, "vaGetDisplayDRM"));
    auto vaInitializeFn =
        reinterpret_cast<VAStatus(*)(VADisplay, int*, int*)>(dlsym(vaLibrary, "vaInitialize"));
    auto vaTerminateFn =
        reinterpret_cast<VAStatus(*)(VADisplay)>(dlsym(vaLibrary, "vaTerminate"));
    auto vaMaxNumProfilesFn =
        reinterpret_cast<int (*)(VADisplay)>(dlsym(vaLibrary, "vaMaxNumProfiles"));
    auto vaQueryConfigProfilesFn =
        reinterpret_cast<VAStatus(*)(VADisplay, VAProfile*, int*)>(
            dlsym(vaLibrary, "vaQueryConfigProfiles"));
    if (!vaGetDisplayDrmFn || !vaInitializeFn || !vaTerminateFn ||
        !vaMaxNumProfilesFn || !vaQueryConfigProfilesFn) {
        dlclose(vaDrmLibrary);
        dlclose(vaLibrary);
        return false;
    }

    int drmFd = -1;
    for (std::size_t i = 0; i < (sizeof(kRenderNodes) / sizeof(kRenderNodes[0])); ++i) {
        drmFd = ::open(kRenderNodes[i], O_RDWR);
        if (drmFd >= 0) {
            break;
        }
    }
    if (drmFd < 0) {
        dlclose(vaDrmLibrary);
        dlclose(vaLibrary);
        return false;
    }

    bool supported = false;
    VADisplay display = vaGetDisplayDrmFn(drmFd);
    int major = 0;
    int minor = 0;
    if (display && vaInitializeFn(display, &major, &minor) == VA_STATUS_SUCCESS) {
        const int maxProfiles = vaMaxNumProfilesFn(display);
        if (maxProfiles > 0) {
            std::vector<VAProfile> profiles(static_cast<std::size_t>(maxProfiles));
            int profileCount = 0;
            if (vaQueryConfigProfilesFn(display, profiles.data(), &profileCount) ==
                VA_STATUS_SUCCESS) {
                for (int i = 0; i < profileCount; ++i) {
                    if (profiles[static_cast<std::size_t>(i)] == requestedProfile) {
                        supported = true;
                        break;
                    }
                }
            }
        }
        (void)vaTerminateFn(display);
    }

    ::close(drmFd);
    dlclose(vaDrmLibrary);
    dlclose(vaLibrary);
    return supported;
}
#endif

inline bool swLinuxVaapiH264RuntimeAvailable() {
#if !SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
    return false;
#else
    return swLinuxVaapiRuntimeSupportsProfile(VAProfileH264High) ||
           swLinuxVaapiRuntimeSupportsProfile(VAProfileH264Main) ||
           swLinuxVaapiRuntimeSupportsProfile(VAProfileH264ConstrainedBaseline);
#endif
}

inline bool swLinuxVaapiH265RuntimeAvailable() {
#if !SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
    return false;
#else
    return swLinuxVaapiRuntimeSupportsProfile(VAProfileHEVCMain)
#  ifdef VAProfileHEVCMain10
           || swLinuxVaapiRuntimeSupportsProfile(VAProfileHEVCMain10)
#  endif
        ;
#endif
}

inline bool swLinuxVaapiExportRuntimeAvailable() {
#if !SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
    return false;
#else
    return swLinuxVaapiRuntimeHasSymbol_("vaExportSurfaceHandle");
#endif
}

inline bool swLinuxH264DecoderRuntimeAvailable() {
    return swLinuxOpenH264RuntimeAvailable();
}

inline bool swLinuxH265DecoderRuntimeAvailable() {
    return swLinuxLibDe265RuntimeAvailable() || swLinuxVaapiH265RuntimeAvailable();
}

class SwLinuxVideoDecoderBase : public SwVideoDecoder {
public:
    enum class Backend {
        Generic,
        Vaapi,
        OpenH264,
        LibDe265
    };

    SwLinuxVideoDecoderBase(SwVideoPacket::Codec codec,
                            const char* decoderName,
                            Backend backend = Backend::Generic)
        : m_codec(codec), m_name(decoderName), m_backend(backend) {}

    ~SwLinuxVideoDecoderBase() override = default;

    const char* name() const override { return m_name; }

    RuntimeHealthEvent takeRuntimeHealthEvent() override {
        RuntimeHealthEvent event = m_pendingRuntimeHealthEvent;
        m_pendingRuntimeHealthEvent = RuntimeHealthEvent();
        return event;
    }

    void flush() override {}

protected:
    void setRuntimeHealthEvent_(RuntimeHealthEventKind kind, const SwString& reason) {
        m_pendingRuntimeHealthEvent.kind = kind;
        m_pendingRuntimeHealthEvent.reason = reason;
    }

    void clearRuntimeHealthEvent_() {
        m_pendingRuntimeHealthEvent = RuntimeHealthEvent();
    }

    void logUnavailableOnce_(const SwString& reason) {
        if (!m_loggedUnavailable) {
            m_loggedUnavailable = true;
            swCWarning(kSwLogCategory_SwLinuxVideoDecoder)
                << "[" << m_name << "] " << reason;
        }
    }

    SwVideoPacket::Codec m_codec{SwVideoPacket::Codec::Unknown};
    const char* m_name{nullptr};
    Backend m_backend{Backend::Generic};
    bool m_loggedUnavailable{false};
    RuntimeHealthEvent m_pendingRuntimeHealthEvent{};
};

class SwLinuxH264Decoder : public SwLinuxVideoDecoderBase {
public:
    explicit SwLinuxH264Decoder(Backend backend = Backend::Generic,
                                const char* decoderName = "SwLinuxH264Decoder")
        : SwLinuxVideoDecoderBase(SwVideoPacket::Codec::H264, decoderName, backend) {}

    ~SwLinuxH264Decoder() override {
        shutdown();
    }

    bool open(const SwVideoFormatInfo& expectedFormat) override {
        (void)expectedFormat;
        shutdown();
        clearRuntimeHealthEvent_();

        if (m_backend == Backend::Generic) {
            if (swLinuxOpenH264RuntimeAvailable()) {
                m_backend = Backend::OpenH264;
            } else if (swLinuxVaapiH264RuntimeAvailable()) {
                m_backend = Backend::Vaapi;
            }
        }

        switch (m_backend) {
        case Backend::OpenH264:
            return initializeOpenH264_();
        case Backend::Vaapi:
            // The VA-API decode path is wired separately; keep open() false until it can actually decode.
            return initializeVaapiProbe_() && false;
        default:
            logUnavailableOnce_("No Linux H264 backend available");
            return false;
        }
    }

    bool feed(const SwVideoPacket& packet) override {
        if (packet.codec() != SwVideoPacket::Codec::H264 || packet.payload().isEmpty()) {
            return false;
        }
        clearRuntimeHealthEvent_();
        if (!ensureOpened_()) {
            return false;
        }
        switch (m_backend) {
        case Backend::OpenH264:
            return decodeOpenH264_(packet);
        case Backend::Vaapi:
            setRuntimeHealthEvent_(RuntimeHealthEventKind::FatalBackendFailure,
                                   "VA-API H264 decode path not implemented yet");
            logUnavailableOnce_("VA-API H264 decode path not implemented yet");
            return false;
        default:
            logUnavailableOnce_("Decoder not initialized");
            return false;
        }
    }

    void flush() override {
#if SW_MEDIA_LINUX_HAS_OPENH264_HEADERS
        if (m_decoder && m_backend == Backend::OpenH264) {
            SBufferInfo bufferInfo;
            std::memset(&bufferInfo, 0, sizeof(bufferInfo));
            unsigned char* planes[3] = {nullptr, nullptr, nullptr};
            (void)m_decoder->DecodeFrameNoDelay(nullptr, 0, planes, &bufferInfo);
        }
#endif
    }

private:
#if SW_MEDIA_LINUX_HAS_OPENH264_HEADERS
    typedef long (*CreateDecoderFn)(ISVCDecoder**);
    typedef void (*DestroyDecoderFn)(ISVCDecoder*);

    static bool isOpenH264FatalState_(DECODING_STATE state) {
        const int fatalMask = dsInvalidArgument | dsInitialOptExpected | dsOutOfMemory |
                              dsDstBufNeedExpan;
        return (static_cast<int>(state) & fatalMask) != 0;
    }

    static bool isOpenH264OutputPendingState_(DECODING_STATE state) {
        const int pendingMask = dsErrorFree | dsFramePending;
        const int nonFatalBits = static_cast<int>(state) &
                                 ~(dsRefLost | dsBitstreamError | dsDepLayerLost |
                                   dsNoParamSets | dsDataErrorConcealed | dsRefListNullPtrs);
        return !isOpenH264FatalState_(state) &&
               (static_cast<int>(state) == dsFramePending ||
                static_cast<int>(state) == dsErrorFree ||
                (nonFatalBits & pendingMask) != 0);
    }
#endif

    void shutdown() {
#if SW_MEDIA_LINUX_HAS_OPENH264_HEADERS
        if (m_decoder) {
            m_decoder->Uninitialize();
            if (m_destroyDecoderFn) {
                m_destroyDecoderFn(m_decoder);
            }
            m_decoder = nullptr;
        }
#endif
        if (m_openH264Library) {
            dlclose(m_openH264Library);
            m_openH264Library = nullptr;
        }
        shutdownVaapiProbe_();
    }

    bool ensureOpened_() {
#if SW_MEDIA_LINUX_HAS_OPENH264_HEADERS
        if (m_backend == Backend::OpenH264 && m_decoder) {
            return true;
        }
#endif
        if (m_backend == Backend::Generic) {
            return open(SwVideoFormatInfo());
        }
        if (m_backend == Backend::Vaapi) {
            return initializeVaapiProbe_() && false;
        }
        return open(SwVideoFormatInfo());
    }

    bool initializeOpenH264_() {
#if !SW_MEDIA_LINUX_HAS_OPENH264_HEADERS
        logUnavailableOnce_("OpenH264 headers unavailable at build time");
        return false;
#else
        static const char* kLibraryCandidates[] = {
            "libopenh264.so.8",
            "libopenh264.so.7",
            "libopenh264.so.6",
            "libopenh264.so"
        };

        for (std::size_t i = 0; i < (sizeof(kLibraryCandidates) / sizeof(kLibraryCandidates[0])); ++i) {
            m_openH264Library = dlopen(kLibraryCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_openH264Library) {
                break;
            }
        }
        if (!m_openH264Library) {
            logUnavailableOnce_("OpenH264 runtime library not found");
            return false;
        }

        m_createDecoderFn =
            reinterpret_cast<CreateDecoderFn>(dlsym(m_openH264Library, "WelsCreateDecoder"));
        m_destroyDecoderFn =
            reinterpret_cast<DestroyDecoderFn>(dlsym(m_openH264Library, "WelsDestroyDecoder"));
        if (!m_createDecoderFn || !m_destroyDecoderFn) {
            logUnavailableOnce_("OpenH264 symbols missing from runtime library");
            shutdown();
            return false;
        }

        if (m_createDecoderFn(&m_decoder) != 0 || !m_decoder) {
            logUnavailableOnce_("Failed to create OpenH264 decoder");
            shutdown();
            return false;
        }

        SDecodingParam params;
        std::memset(&params, 0, sizeof(params));
        params.uiTargetDqLayer = static_cast<unsigned char>(std::numeric_limits<unsigned char>::max());
        params.eEcActiveIdc = ERROR_CON_SLICE_COPY;
        params.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
        if (m_decoder->Initialize(&params) != 0) {
            logUnavailableOnce_("Failed to initialize OpenH264 decoder");
            shutdown();
            return false;
        }
        return true;
#endif
    }

    bool decodeOpenH264_(const SwVideoPacket& packet) {
#if !SW_MEDIA_LINUX_HAS_OPENH264_HEADERS
        (void)packet;
        return false;
#else
        if (!m_decoder) {
            return false;
        }

        unsigned char* planes[3] = {nullptr, nullptr, nullptr};
        SBufferInfo bufferInfo;
        std::memset(&bufferInfo, 0, sizeof(bufferInfo));

        const unsigned char* payload =
            reinterpret_cast<const unsigned char*>(packet.payload().constData());
        DECODING_STATE state =
            m_decoder->DecodeFrameNoDelay(payload,
                                          static_cast<int>(packet.payload().size()),
                                          planes,
                                          &bufferInfo);
        if (bufferInfo.iBufferStatus != 1 && isOpenH264OutputPendingState_(state)) {
            unsigned char* delayedPlanes[3] = {nullptr, nullptr, nullptr};
            SBufferInfo delayedInfo;
            std::memset(&delayedInfo, 0, sizeof(delayedInfo));
            const DECODING_STATE delayedState =
                m_decoder->DecodeFrame2(nullptr, 0, delayedPlanes, &delayedInfo);
            if (delayedInfo.iBufferStatus == 1) {
                planes[0] = delayedPlanes[0];
                planes[1] = delayedPlanes[1];
                planes[2] = delayedPlanes[2];
                bufferInfo = delayedInfo;
                state = delayedState;
            } else if (isOpenH264OutputPendingState_(delayedState)) {
                return true;
            } else {
                state = delayedState;
            }
        }

        if (bufferInfo.iBufferStatus != 1) {
            if (isOpenH264OutputPendingState_(state)) {
                return true;
            }
            swCWarning(kSwLogCategory_SwLinuxVideoDecoder)
                << "[" << m_name << "] OpenH264 decode failed state=0x"
                << SwString::number(static_cast<int>(state), 16)
                << " bytes=" << packet.payload().size()
                << " key=" << (packet.isKeyFrame() ? 1 : 0);
            return false;
        }

        const SBufferInfo& info = bufferInfo;
        const int width = info.UsrData.sSystemBuffer.iWidth;
        const int height = info.UsrData.sSystemBuffer.iHeight;
        if (width <= 0 || height <= 0 || !planes[0] || !planes[1] || !planes[2]) {
            return false;
        }

        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::YUV420P);
        if (!frame.isValid()) {
            return false;
        }

        const int srcYStride = info.UsrData.sSystemBuffer.iStride[0];
        const int srcUStride = info.UsrData.sSystemBuffer.iStride[1];
        const int srcVStride = info.UsrData.sSystemBuffer.iStride[1];

        for (int y = 0; y < height; ++y) {
            std::memcpy(frame.planeData(0) + y * frame.planeStride(0),
                        planes[0] + y * srcYStride,
                        static_cast<std::size_t>(width));
        }
        const int chromaHeight = (height + 1) / 2;
        const int chromaWidth = (width + 1) / 2;
        for (int y = 0; y < chromaHeight; ++y) {
            std::memcpy(frame.planeData(1) + y * frame.planeStride(1),
                        planes[1] + y * srcUStride,
                        static_cast<std::size_t>(chromaWidth));
            std::memcpy(frame.planeData(2) + y * frame.planeStride(2),
                        planes[2] + y * srcVStride,
                        static_cast<std::size_t>(chromaWidth));
        }

        frame.setTimestamp(packet.pts());
        emitFrame(frame);
        return true;
#endif
    }

    bool initializeVaapiProbe_() {
#if !SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        logUnavailableOnce_("VA-API headers unavailable at build time");
        return false;
#else
        if (m_vaLibrary || m_vaDrmLibrary) {
            return true;
        }

        static const char* kVaCandidates[] = {"libva.so.2", "libva.so"};
        static const char* kVaDrmCandidates[] = {"libva-drm.so.2", "libva-drm.so"};
        for (std::size_t i = 0; i < (sizeof(kVaCandidates) / sizeof(kVaCandidates[0])); ++i) {
            m_vaLibrary = dlopen(kVaCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_vaLibrary) {
                break;
            }
        }
        for (std::size_t i = 0; i < (sizeof(kVaDrmCandidates) / sizeof(kVaDrmCandidates[0])); ++i) {
            m_vaDrmLibrary = dlopen(kVaDrmCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_vaDrmLibrary) {
                break;
            }
        }
        if (!m_vaLibrary || !m_vaDrmLibrary) {
            logUnavailableOnce_("VA-API runtime libraries not found");
            shutdownVaapiProbe_();
            return false;
        }

        m_vaGetDisplayDrmFn =
            reinterpret_cast<VADisplay(*)(int)>(dlsym(m_vaDrmLibrary, "vaGetDisplayDRM"));
        m_vaInitializeFn =
            reinterpret_cast<VAStatus(*)(VADisplay, int*, int*)>(dlsym(m_vaLibrary, "vaInitialize"));
        m_vaTerminateFn =
            reinterpret_cast<VAStatus(*)(VADisplay)>(dlsym(m_vaLibrary, "vaTerminate"));
        if (!m_vaGetDisplayDrmFn || !m_vaInitializeFn || !m_vaTerminateFn) {
            logUnavailableOnce_("VA-API symbols missing from runtime libraries");
            shutdownVaapiProbe_();
            return false;
        }

        static const char* kRenderNodes[] = {
            "/dev/dri/renderD128",
            "/dev/dri/renderD129",
            "/dev/dri/renderD130"
        };
        for (std::size_t i = 0; i < (sizeof(kRenderNodes) / sizeof(kRenderNodes[0])); ++i) {
            m_drmFd = ::open(kRenderNodes[i], O_RDWR);
            if (m_drmFd >= 0) {
                break;
            }
        }
        if (m_drmFd < 0) {
            logUnavailableOnce_("No VA-API DRM render node available");
            shutdownVaapiProbe_();
            return false;
        }

        m_vaDisplay = m_vaGetDisplayDrmFn(m_drmFd);
        if (!m_vaDisplay) {
            logUnavailableOnce_("vaGetDisplayDRM failed");
            shutdownVaapiProbe_();
            return false;
        }

        int major = 0;
        int minor = 0;
        if (m_vaInitializeFn(m_vaDisplay, &major, &minor) != VA_STATUS_SUCCESS) {
            logUnavailableOnce_("vaInitialize failed");
            shutdownVaapiProbe_();
            return false;
        }
        m_vaInitialized = true;
        return true;
#endif
    }

    void shutdownVaapiProbe_() {
#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        if (m_vaInitialized && m_vaTerminateFn && m_vaDisplay) {
            (void)m_vaTerminateFn(m_vaDisplay);
        }
        m_vaInitialized = false;
        m_vaDisplay = nullptr;
        m_vaGetDisplayDrmFn = nullptr;
        m_vaInitializeFn = nullptr;
        m_vaTerminateFn = nullptr;
#endif
        if (m_drmFd >= 0) {
            ::close(m_drmFd);
            m_drmFd = -1;
        }
        if (m_vaDrmLibrary) {
            dlclose(m_vaDrmLibrary);
            m_vaDrmLibrary = nullptr;
        }
        if (m_vaLibrary) {
            dlclose(m_vaLibrary);
            m_vaLibrary = nullptr;
        }
    }

    void* m_openH264Library{nullptr};
#if SW_MEDIA_LINUX_HAS_OPENH264_HEADERS
    CreateDecoderFn m_createDecoderFn{nullptr};
    DestroyDecoderFn m_destroyDecoderFn{nullptr};
    ISVCDecoder* m_decoder{nullptr};
#endif

    void* m_vaLibrary{nullptr};
    void* m_vaDrmLibrary{nullptr};
    int m_drmFd{-1};
#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
    VADisplay m_vaDisplay{nullptr};
    VADisplay (*m_vaGetDisplayDrmFn)(int){nullptr};
    VAStatus (*m_vaInitializeFn)(VADisplay, int*, int*){nullptr};
    VAStatus (*m_vaTerminateFn)(VADisplay){nullptr};
    bool m_vaInitialized{false};
#endif
};

class SwLinuxH265Decoder : public SwLinuxVideoDecoderBase {
public:
    explicit SwLinuxH265Decoder(Backend backend = Backend::Generic,
                                const char* decoderName = "SwLinuxH265Decoder",
                                bool emitNativeVaapiFrames = false)
        : SwLinuxVideoDecoderBase(SwVideoPacket::Codec::H265, decoderName, backend),
          m_emitNativeVaapiFrames(emitNativeVaapiFrames) {}

    ~SwLinuxH265Decoder() override {
        shutdown();
    }

    bool open(const SwVideoFormatInfo& expectedFormat) override {
        clearRuntimeHealthEvent_();
        shutdown();

        if (m_backend == Backend::Generic) {
            m_backend = swLinuxLibDe265RuntimeAvailable() ? Backend::LibDe265
                                                          : Backend::Vaapi;
        }
        if (m_backend == Backend::LibDe265) {
            return initializeLibDe265_();
        }
        if (m_backend != Backend::Vaapi) {
            logUnavailableOnce_("No Linux H265 backend available");
            return false;
        }
        if (!initializeVaapiProbe_()) {
            return false;
        }
        if (m_emitNativeVaapiFrames && !m_vaExportSurfaceHandleFn) {
            logUnavailableOnce_("vaExportSurfaceHandle unavailable for native VA-API export");
            shutdown();
            return false;
        }
        m_opened = true;
        if (expectedFormat.width > 0 && expectedFormat.height > 0) {
            m_inputWidth = expectedFormat.width;
            m_inputHeight = expectedFormat.height;
            (void)initializeVaapiContext_();
        }
        return true;
    }

    bool feed(const SwVideoPacket& packet) override {
        if (packet.codec() != SwVideoPacket::Codec::H265 || packet.payload().isEmpty()) {
            return false;
        }
        clearRuntimeHealthEvent_();
        if (!m_opened && !open(SwVideoFormatInfo())) {
            return false;
        }
        if (m_backend == Backend::LibDe265) {
            return decodeLibDe265_(packet);
        }
        cacheSequenceHeader_(packet);
#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        if (!m_vaContextReady && m_inputWidth > 0 && m_inputHeight > 0) {
            (void)initializeVaapiContext_();
        }
        if (m_vaContextReady) {
            if (submitCurrentAccessUnit_(packet)) {
                return true;
            }
        }
#endif
        setRuntimeHealthEvent_(RuntimeHealthEventKind::RecoverableOutputFailure,
                               "VA-API H265 decode failed for the current access unit");
        logUnavailableOnce_("VA-API H265 decode failed for the current access unit");
        return false;
    }

    void flush() override {
        if (m_backend == Backend::LibDe265) {
            flushLibDe265_();
        }
    }

private:
#if SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
    typedef de265_decoder_context* (*De265NewDecoderFn)();
    typedef de265_error (*De265FreeDecoderFn)(de265_decoder_context*);
    typedef de265_error (*De265PushDataFn)(de265_decoder_context*, const void*, int, de265_PTS, void*);
    typedef void (*De265PushEndOfFrameFn)(de265_decoder_context*);
    typedef de265_error (*De265DecodeFn)(de265_decoder_context*, int*);
    typedef const de265_image* (*De265GetNextPictureFn)(de265_decoder_context*);
    typedef void (*De265ReleaseNextPictureFn)(de265_decoder_context*);
    typedef const uint8_t* (*De265GetImagePlaneFn)(const de265_image*, int, int*);
    typedef int (*De265GetImageWidthFn)(const de265_image*, int);
    typedef int (*De265GetImageHeightFn)(const de265_image*, int);
    typedef int (*De265GetBitsPerPixelFn)(const de265_image*, int);
    typedef de265_chroma (*De265GetChromaFormatFn)(const de265_image*);
    typedef de265_PTS (*De265GetImagePtsFn)(const de265_image*);
    typedef const char* (*De265GetErrorTextFn)(de265_error);
    typedef int (*De265IsOkFn)(de265_error);
    typedef void (*De265SetVerbosityFn)(int);
    typedef void (*De265SetParameterBoolFn)(de265_decoder_context*, de265_param, int);
    typedef void (*De265SetParameterIntFn)(de265_decoder_context*, de265_param, int);
    typedef de265_error (*De265FlushDataFn)(de265_decoder_context*);
    typedef void (*De265ResetFn)(de265_decoder_context*);
#endif

    void shutdown() {
        shutdownLibDe265_();
        shutdownVaapiContext_();
        shutdownVaapiProbe_();
        m_opened = false;
        m_sequenceHeader.clear();
        m_inputWidth = 0;
        m_inputHeight = 0;
        m_loggedSequenceHeader = false;
        m_spsInfo = SwHevcSpsInfo();
        m_ppsInfo = SwHevcPpsInfo();
    }

    void cacheSequenceHeader_(const SwVideoPacket& packet) {
        if (!m_sequenceHeader.isEmpty()) {
            return;
        }
        const SwHevcSequenceHeaderInfo info = swCollectHevcSequenceHeaderInfo(packet.payload());
        if (!info.isComplete()) {
            return;
        }
        m_sequenceHeader = info.annexB;
        m_inputWidth = info.width;
        m_inputHeight = info.height;
        parseCachedSequenceHeader_();
        if (!m_loggedSequenceHeader) {
            m_loggedSequenceHeader = true;
            swCWarning(kSwLogCategory_SwLinuxVideoDecoder)
                << "[" << m_name << "] Cached HEVC sequence header bytes="
                << m_sequenceHeader.size() << " dimensions="
                << m_inputWidth << "x" << m_inputHeight;
        }
    }

    void parseCachedSequenceHeader_() {
        if (m_sequenceHeader.isEmpty()) {
            return;
        }
        const uint8_t* data = reinterpret_cast<const uint8_t*>(m_sequenceHeader.constData());
        const std::size_t size = static_cast<std::size_t>(m_sequenceHeader.size());
        (void)swForEachHevcAnnexBNalUnit(data, size, [&](const SwHevcAnnexBNalUnitView& nal) {
            if (nal.nalType == 33u) {
                (void)swParseHevcSps(nal.data, nal.size, m_spsInfo);
            } else if (nal.nalType == 34u) {
                (void)swParseHevcPps(nal.data, nal.size, m_ppsInfo);
            }
            return true;
        });
    }

    bool initializeLibDe265_() {
#if !SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        logUnavailableOnce_("libde265 headers unavailable at build time");
        return false;
#else
        static const char* kLibraryCandidates[] = {
            "libde265.so.0",
            "libde265.so"
        };
        for (std::size_t i = 0; i < (sizeof(kLibraryCandidates) / sizeof(kLibraryCandidates[0])); ++i) {
            m_libDe265Library = dlopen(kLibraryCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_libDe265Library) {
                break;
            }
        }
        if (!m_libDe265Library) {
            logUnavailableOnce_("libde265 runtime library not found");
            return false;
        }

        m_de265NewDecoderFn =
            reinterpret_cast<De265NewDecoderFn>(dlsym(m_libDe265Library, "de265_new_decoder"));
        m_de265FreeDecoderFn =
            reinterpret_cast<De265FreeDecoderFn>(dlsym(m_libDe265Library, "de265_free_decoder"));
        m_de265PushDataFn =
            reinterpret_cast<De265PushDataFn>(dlsym(m_libDe265Library, "de265_push_data"));
        m_de265PushEndOfFrameFn = reinterpret_cast<De265PushEndOfFrameFn>(
            dlsym(m_libDe265Library, "de265_push_end_of_frame"));
        m_de265DecodeFn =
            reinterpret_cast<De265DecodeFn>(dlsym(m_libDe265Library, "de265_decode"));
        m_de265GetNextPictureFn = reinterpret_cast<De265GetNextPictureFn>(
            dlsym(m_libDe265Library, "de265_get_next_picture"));
        m_de265ReleaseNextPictureFn = reinterpret_cast<De265ReleaseNextPictureFn>(
            dlsym(m_libDe265Library, "de265_release_next_picture"));
        m_de265GetImagePlaneFn = reinterpret_cast<De265GetImagePlaneFn>(
            dlsym(m_libDe265Library, "de265_get_image_plane"));
        m_de265GetImageWidthFn = reinterpret_cast<De265GetImageWidthFn>(
            dlsym(m_libDe265Library, "de265_get_image_width"));
        m_de265GetImageHeightFn = reinterpret_cast<De265GetImageHeightFn>(
            dlsym(m_libDe265Library, "de265_get_image_height"));
        m_de265GetBitsPerPixelFn = reinterpret_cast<De265GetBitsPerPixelFn>(
            dlsym(m_libDe265Library, "de265_get_bits_per_pixel"));
        m_de265GetChromaFormatFn = reinterpret_cast<De265GetChromaFormatFn>(
            dlsym(m_libDe265Library, "de265_get_chroma_format"));
        m_de265GetImagePtsFn = reinterpret_cast<De265GetImagePtsFn>(
            dlsym(m_libDe265Library, "de265_get_image_PTS"));
        m_de265GetErrorTextFn = reinterpret_cast<De265GetErrorTextFn>(
            dlsym(m_libDe265Library, "de265_get_error_text"));
        m_de265IsOkFn =
            reinterpret_cast<De265IsOkFn>(dlsym(m_libDe265Library, "de265_isOK"));
        m_de265SetVerbosityFn = reinterpret_cast<De265SetVerbosityFn>(
            dlsym(m_libDe265Library, "de265_set_verbosity"));
        m_de265SetParameterBoolFn = reinterpret_cast<De265SetParameterBoolFn>(
            dlsym(m_libDe265Library, "de265_set_parameter_bool"));
        m_de265SetParameterIntFn = reinterpret_cast<De265SetParameterIntFn>(
            dlsym(m_libDe265Library, "de265_set_parameter_int"));
        m_de265FlushDataFn =
            reinterpret_cast<De265FlushDataFn>(dlsym(m_libDe265Library, "de265_flush_data"));
        m_de265ResetFn =
            reinterpret_cast<De265ResetFn>(dlsym(m_libDe265Library, "de265_reset"));
        if (!m_de265NewDecoderFn || !m_de265FreeDecoderFn || !m_de265PushDataFn ||
            !m_de265PushEndOfFrameFn || !m_de265DecodeFn || !m_de265GetNextPictureFn ||
            !m_de265ReleaseNextPictureFn || !m_de265GetImagePlaneFn || !m_de265GetImageWidthFn ||
            !m_de265GetImageHeightFn || !m_de265GetBitsPerPixelFn || !m_de265GetChromaFormatFn ||
            !m_de265GetImagePtsFn || !m_de265GetErrorTextFn || !m_de265IsOkFn || !m_de265SetVerbosityFn ||
            !m_de265SetParameterBoolFn || !m_de265SetParameterIntFn || !m_de265FlushDataFn ||
            !m_de265ResetFn) {
            logUnavailableOnce_("libde265 symbols missing from runtime library");
            shutdownLibDe265_();
            return false;
        }

        m_de265Decoder = m_de265NewDecoderFn();
        if (!m_de265Decoder) {
            logUnavailableOnce_("Failed to create libde265 decoder");
            shutdownLibDe265_();
            return false;
        }

        m_de265SetVerbosityFn(0);
        m_de265SetParameterIntFn(m_de265Decoder,
                                 DE265_DECODER_PARAM_ACCELERATION_CODE,
                                 de265_acceleration_AUTO);
        m_de265SetParameterBoolFn(m_de265Decoder,
                                  DE265_DECODER_PARAM_SUPPRESS_FAULTY_PICTURES,
                                  0);
        m_opened = true;
        return true;
#endif
    }

    bool decodeLibDe265_(const SwVideoPacket& packet) {
#if !SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        (void)packet;
        return false;
#else
        if (!m_de265Decoder || !m_de265PushDataFn || !m_de265PushEndOfFrameFn ||
            !m_de265DecodeFn) {
            return false;
        }

        const de265_error pushStatus =
            m_de265PushDataFn(m_de265Decoder,
                              packet.payload().constData(),
                              packet.payload().size(),
                              static_cast<de265_PTS>(packet.pts()),
                              nullptr);
        if (!isLibDe265Ok_(pushStatus)) {
            const SwString reason = libDe265ErrorText_(pushStatus);
            setRuntimeHealthEvent_(RuntimeHealthEventKind::RecoverableOutputFailure, reason);
            swCWarning(kSwLogCategory_SwLinuxVideoDecoder)
                << "[" << m_name << "] libde265 push failed: " << reason;
            return false;
        }

        m_de265PushEndOfFrameFn(m_de265Decoder);

        bool sawProgress = false;
        bool ok = true;
        while (true) {
            int more = 0;
            const de265_error status = m_de265DecodeFn(m_de265Decoder, &more);
            if (isLibDe265Ok_(status)) {
                sawProgress = true;
            } else if (status != DE265_ERROR_WAITING_FOR_INPUT_DATA) {
                const SwString reason = libDe265ErrorText_(status);
                setRuntimeHealthEvent_(RuntimeHealthEventKind::RecoverableOutputFailure, reason);
                swCWarning(kSwLogCategory_SwLinuxVideoDecoder)
                    << "[" << m_name << "] libde265 decode failed: " << reason;
                ok = false;
                break;
            }

            if (!drainLibDe265Pictures_()) {
                ok = false;
                break;
            }
            if (status == DE265_ERROR_WAITING_FOR_INPUT_DATA || more == 0) {
                break;
            }
        }

        return ok && (drainLibDe265Pictures_() || sawProgress);
#endif
    }

    void flushLibDe265_() {
#if SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        if (!m_de265Decoder || !m_de265FlushDataFn || !m_de265DecodeFn) {
            return;
        }
        (void)m_de265FlushDataFn(m_de265Decoder);
        while (true) {
            int more = 0;
            const de265_error status = m_de265DecodeFn(m_de265Decoder, &more);
            if (!drainLibDe265Pictures_()) {
                break;
            }
            if (status == DE265_ERROR_WAITING_FOR_INPUT_DATA || more == 0) {
                break;
            }
        }
#endif
    }

    bool drainLibDe265Pictures_() {
#if !SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        return false;
#else
        if (!m_de265Decoder || !m_de265GetNextPictureFn || !m_de265ReleaseNextPictureFn ||
            !m_de265GetImagePlaneFn || !m_de265GetImageWidthFn || !m_de265GetImageHeightFn ||
            !m_de265GetBitsPerPixelFn || !m_de265GetChromaFormatFn || !m_de265GetImagePtsFn) {
            return false;
        }

        while (const de265_image* image = m_de265GetNextPictureFn(m_de265Decoder)) {
            const int width = m_de265GetImageWidthFn(image, 0);
            const int height = m_de265GetImageHeightFn(image, 0);
            if (width <= 0 || height <= 0) {
                m_de265ReleaseNextPictureFn(m_de265Decoder);
                continue;
            }

            SwVideoFrame frame =
                SwVideoFrame::allocate(width, height, SwVideoPixelFormat::YUV420P);
            if (!frame.isValid()) {
                m_de265ReleaseNextPictureFn(m_de265Decoder);
                return false;
            }

            const de265_chroma chroma = m_de265GetChromaFormatFn(image);
            for (int plane = 0; plane < 3; ++plane) {
                int srcStride = 0;
                const uint8_t* srcPlane = m_de265GetImagePlaneFn(image, plane, &srcStride);
                const int srcWidth = m_de265GetImageWidthFn(image, plane);
                const int srcHeight = m_de265GetImageHeightFn(image, plane);
                const int srcBitsPerPixel = m_de265GetBitsPerPixelFn(image, plane);
                const int dstWidth = (plane == 0) ? width : ((width + 1) / 2);
                const int dstHeight = (plane == 0) ? height : ((height + 1) / 2);
                if (plane > 0 && chroma == de265_chroma_mono) {
                    for (int y = 0; y < dstHeight; ++y) {
                        std::memset(frame.planeData(plane) + y * frame.planeStride(plane),
                                    0x80,
                                    static_cast<std::size_t>(dstWidth));
                    }
                    continue;
                }
                if (!srcPlane || srcStride <= 0 || srcWidth <= 0 || srcHeight <= 0 ||
                    srcBitsPerPixel <= 0) {
                    m_de265ReleaseNextPictureFn(m_de265Decoder);
                    return false;
                }
                if (srcBitsPerPixel <= 8) {
                    copyPlaneNearest_(frame.planeData(plane),
                                      frame.planeStride(plane),
                                      dstWidth,
                                      dstHeight,
                                      srcPlane,
                                      srcStride,
                                      srcWidth,
                                      srcHeight);
                } else {
                    copyPlaneTo8BitNearest_(frame.planeData(plane),
                                            frame.planeStride(plane),
                                            dstWidth,
                                            dstHeight,
                                            srcPlane,
                                            srcStride,
                                            srcWidth,
                                            srcHeight,
                                            srcBitsPerPixel);
                }
            }

            frame.setTimestamp(static_cast<std::int64_t>(m_de265GetImagePtsFn(image)));
            emitFrame(frame);
            m_de265ReleaseNextPictureFn(m_de265Decoder);
        }

        return true;
#endif
    }

    SwString libDe265ErrorText_(
#if SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        de265_error status
#else
        int /*status*/
#endif
    ) const {
#if !SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        return "libde265 unavailable";
#else
        if (m_de265GetErrorTextFn) {
            const char* text = m_de265GetErrorTextFn(status);
            if (text && *text) {
                return SwString(text);
            }
        }
        return "libde265 error";
#endif
    }

    bool isLibDe265Ok_(
#if SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        de265_error status
#else
        int /*status*/
#endif
    ) const {
#if !SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        return false;
#else
        return m_de265IsOkFn ? (m_de265IsOkFn(status) != 0) : (status == DE265_OK);
#endif
    }

    static void copyPlaneNearest_(uint8_t* dst,
                                  int dstStride,
                                  int dstWidth,
                                  int dstHeight,
                                  const uint8_t* src,
                                  int srcStride,
                                  int srcWidth,
                                  int srcHeight) {
        if (!dst || !src || dstStride <= 0 || srcStride <= 0 || dstWidth <= 0 || dstHeight <= 0 ||
            srcWidth <= 0 || srcHeight <= 0) {
            return;
        }

        if (dstWidth == srcWidth && dstHeight == srcHeight) {
            for (int y = 0; y < dstHeight; ++y) {
                std::memcpy(dst + y * dstStride,
                            src + y * srcStride,
                            static_cast<std::size_t>(dstWidth));
            }
            return;
        }

        for (int y = 0; y < dstHeight; ++y) {
            const int srcY = std::min(srcHeight - 1, (y * srcHeight) / dstHeight);
            const uint8_t* srcRow = src + srcY * srcStride;
            uint8_t* dstRow = dst + y * dstStride;
            for (int x = 0; x < dstWidth; ++x) {
                const int srcX = std::min(srcWidth - 1, (x * srcWidth) / dstWidth);
                dstRow[x] = srcRow[srcX];
            }
        }
    }

    static void copyPlaneTo8BitNearest_(uint8_t* dst,
                                        int dstStride,
                                        int dstWidth,
                                        int dstHeight,
                                        const uint8_t* src,
                                        int srcStride,
                                        int srcWidth,
                                        int srcHeight,
                                        int srcBitsPerPixel) {
        if (!dst || !src || dstStride <= 0 || srcStride <= 0 || dstWidth <= 0 || dstHeight <= 0 ||
            srcWidth <= 0 || srcHeight <= 0 || srcBitsPerPixel <= 8) {
            copyPlaneNearest_(dst, dstStride, dstWidth, dstHeight, src, srcStride, srcWidth, srcHeight);
            return;
        }

        const int srcBytesPerSample = (srcBitsPerPixel + 7) / 8;
        if (srcBytesPerSample <= 1) {
            copyPlaneNearest_(dst, dstStride, dstWidth, dstHeight, src, srcStride, srcWidth, srcHeight);
            return;
        }

        const std::uint64_t sampleMax =
            (srcBitsPerPixel >= 32) ? 0xFFFFFFFFull : ((1ull << srcBitsPerPixel) - 1ull);
        if (sampleMax == 0ull) {
            return;
        }

        for (int y = 0; y < dstHeight; ++y) {
            const int srcY = std::min(srcHeight - 1, (y * srcHeight) / dstHeight);
            const uint8_t* srcRow = src + srcY * srcStride;
            uint8_t* dstRow = dst + y * dstStride;
            for (int x = 0; x < dstWidth; ++x) {
                const int srcX = std::min(srcWidth - 1, (x * srcWidth) / dstWidth);
                const uint8_t* srcSample = srcRow + static_cast<std::size_t>(srcX) * srcBytesPerSample;
                std::uint64_t value = 0ull;
                for (int byteIndex = 0; byteIndex < srcBytesPerSample; ++byteIndex) {
                    value |= static_cast<std::uint64_t>(srcSample[byteIndex]) << (byteIndex * 8);
                }
                if (value > sampleMax) {
                    value = sampleMax;
                }
                dstRow[x] = static_cast<uint8_t>((value * 255ull + (sampleMax / 2ull)) / sampleMax);
            }
        }
    }

    void shutdownLibDe265_() {
#if SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
        if (m_de265Decoder && m_de265ResetFn) {
            m_de265ResetFn(m_de265Decoder);
        }
        if (m_de265Decoder && m_de265FreeDecoderFn) {
            (void)m_de265FreeDecoderFn(m_de265Decoder);
        }
        m_de265Decoder = nullptr;
        m_de265NewDecoderFn = nullptr;
        m_de265FreeDecoderFn = nullptr;
        m_de265PushDataFn = nullptr;
        m_de265PushEndOfFrameFn = nullptr;
        m_de265DecodeFn = nullptr;
        m_de265GetNextPictureFn = nullptr;
        m_de265ReleaseNextPictureFn = nullptr;
        m_de265GetImagePlaneFn = nullptr;
        m_de265GetImageWidthFn = nullptr;
        m_de265GetImageHeightFn = nullptr;
        m_de265GetBitsPerPixelFn = nullptr;
        m_de265GetChromaFormatFn = nullptr;
        m_de265GetImagePtsFn = nullptr;
        m_de265GetErrorTextFn = nullptr;
        m_de265IsOkFn = nullptr;
        m_de265SetVerbosityFn = nullptr;
        m_de265SetParameterBoolFn = nullptr;
        m_de265SetParameterIntFn = nullptr;
        m_de265FlushDataFn = nullptr;
        m_de265ResetFn = nullptr;
#endif
        if (m_libDe265Library) {
            dlclose(m_libDe265Library);
            m_libDe265Library = nullptr;
        }
    }

    bool initializeVaapiContext_() {
#if !SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        return false;
#else
        if (!m_vaInitialized || !m_opened) {
            return false;
        }
        if (m_vaContextReady) {
            return true;
        }
        if (m_inputWidth <= 0 || m_inputHeight <= 0) {
            return false;
        }
        if (!m_vaGetConfigAttributesFn || !m_vaCreateConfigFn || !m_vaCreateSurfacesFn ||
            !m_vaCreateContextFn || !m_vaDestroyConfigFn || !m_vaDestroySurfacesFn ||
            !m_vaDestroyContextFn) {
            logUnavailableOnce_("VA-API decode context symbols missing");
            return false;
        }

        VAConfigAttrib attrs[2];
        std::memset(attrs, 0, sizeof(attrs));
        attrs[0].type = VAConfigAttribRTFormat;
        attrs[1].type = VAConfigAttribDecSliceMode;
        if (m_vaGetConfigAttributesFn(m_vaDisplay, VAProfileHEVCMain, VAEntrypointVLD, attrs, 2) !=
            VA_STATUS_SUCCESS) {
            logUnavailableOnce_("vaGetConfigAttributes failed for HEVC");
            return false;
        }
        if (attrs[0].value == VA_ATTRIB_NOT_SUPPORTED ||
            (attrs[0].value & VA_RT_FORMAT_YUV420) == 0) {
            logUnavailableOnce_("VA-API HEVC does not advertise YUV420 decode surfaces");
            return false;
        }
        if (attrs[1].value != VA_ATTRIB_NOT_SUPPORTED &&
            (attrs[1].value & (VA_DEC_SLICE_MODE_BASE | VA_DEC_SLICE_MODE_NORMAL)) == 0) {
            logUnavailableOnce_("VA-API HEVC slice mode unsupported");
            return false;
        }

        VAConfigAttrib configAttrs[2];
        std::memset(configAttrs, 0, sizeof(configAttrs));
        configAttrs[0].type = VAConfigAttribRTFormat;
        configAttrs[0].value = VA_RT_FORMAT_YUV420;
        configAttrs[1].type = VAConfigAttribDecSliceMode;
        configAttrs[1].value =
            (attrs[1].value & VA_DEC_SLICE_MODE_BASE) ? VA_DEC_SLICE_MODE_BASE
                                                      : VA_DEC_SLICE_MODE_NORMAL;
        if (m_vaCreateConfigFn(m_vaDisplay,
                               VAProfileHEVCMain,
                               VAEntrypointVLD,
                               configAttrs,
                               2,
                               &m_vaConfig) != VA_STATUS_SUCCESS) {
            logUnavailableOnce_("vaCreateConfig failed for HEVC");
            m_vaConfig = VA_INVALID_ID;
            return false;
        }

        if (m_vaCreateSurfacesFn(m_vaDisplay,
                                 VA_RT_FORMAT_YUV420,
                                 static_cast<unsigned int>(m_inputWidth),
                                 static_cast<unsigned int>(m_inputHeight),
                                 m_surfaces,
                                 kSurfaceCount,
                                 nullptr,
                                 0) != VA_STATUS_SUCCESS) {
            logUnavailableOnce_("vaCreateSurfaces failed for HEVC");
            shutdownVaapiContext_();
            return false;
        }
        m_surfaceCountCreated = kSurfaceCount;

        if (m_vaCreateContextFn(m_vaDisplay,
                                m_vaConfig,
                                m_inputWidth,
                                m_inputHeight,
                                VA_PROGRESSIVE,
                                m_surfaces,
                                kSurfaceCount,
                                &m_vaContext) != VA_STATUS_SUCCESS) {
            logUnavailableOnce_("vaCreateContext failed for HEVC");
            shutdownVaapiContext_();
            return false;
        }

        m_vaContextReady = true;
        swCWarning(kSwLogCategory_SwLinuxVideoDecoder)
            << "[" << m_name << "] Prepared VA-API HEVC context "
            << m_inputWidth << "x" << m_inputHeight
            << " sliceMode=" << static_cast<int>(configAttrs[1].value);
        return true;
#endif
    }

    bool submitCurrentAccessUnit_(const SwVideoPacket& packet) {
#if !SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        (void)packet;
        return false;
#else
        if (!m_vaContextReady || !m_vaBeginPictureFn || !m_vaRenderPictureFn || !m_vaEndPictureFn ||
            !m_vaSyncSurfaceFn || !m_vaCreateBufferFn || !m_vaDestroyBufferFn) {
            return false;
        }

        const SwHevcAccessUnitInfo au = swParseHevcAccessUnit(packet.payload());
        if (!au.hasSlice) {
            return false;
        }
        if (!m_spsInfo.valid || !m_ppsInfo.valid || m_sequenceHeader.isEmpty()) {
            return false;
        }
        if (m_spsInfo.scaling_list_enabled_flag != 0u || !au.isIdr) {
            return false;
        }

        const int surfaceIndex = (m_nextSurfaceIndex++) % kSurfaceCount;
        VASurfaceID targetSurface = m_surfaces[surfaceIndex];
        if (targetSurface == VA_INVALID_SURFACE) {
            return false;
        }

        VAPictureParameterBufferHEVC pictureParams;
        std::memset(&pictureParams, 0, sizeof(pictureParams));
        pictureParams.CurrPic.picture_id = targetSurface;
        pictureParams.CurrPic.pic_order_cnt = 0;
        pictureParams.CurrPic.flags = 0;
        for (int i = 0; i < 15; ++i) {
            pictureParams.ReferenceFrames[i].picture_id = VA_INVALID_SURFACE;
            pictureParams.ReferenceFrames[i].pic_order_cnt = 0;
            pictureParams.ReferenceFrames[i].flags = VA_PICTURE_HEVC_INVALID;
        }
        pictureParams.pic_width_in_luma_samples = static_cast<uint16_t>(m_spsInfo.width);
        pictureParams.pic_height_in_luma_samples = static_cast<uint16_t>(m_spsInfo.height);
        pictureParams.pic_fields.bits.chroma_format_idc = m_spsInfo.chroma_format_idc;
        pictureParams.pic_fields.bits.separate_colour_plane_flag =
            m_spsInfo.separate_colour_plane_flag;
        pictureParams.pic_fields.bits.pcm_enabled_flag = m_spsInfo.pcm_enabled_flag;
        pictureParams.pic_fields.bits.scaling_list_enabled_flag =
            m_spsInfo.scaling_list_enabled_flag;
        pictureParams.pic_fields.bits.transform_skip_enabled_flag =
            m_ppsInfo.transform_skip_enabled_flag;
        pictureParams.pic_fields.bits.amp_enabled_flag = m_spsInfo.amp_enabled_flag;
        pictureParams.pic_fields.bits.strong_intra_smoothing_enabled_flag =
            m_spsInfo.strong_intra_smoothing_enabled_flag;
        pictureParams.pic_fields.bits.sign_data_hiding_enabled_flag =
            m_ppsInfo.sign_data_hiding_enabled_flag;
        pictureParams.pic_fields.bits.constrained_intra_pred_flag =
            m_ppsInfo.constrained_intra_pred_flag;
        pictureParams.pic_fields.bits.cu_qp_delta_enabled_flag =
            m_ppsInfo.cu_qp_delta_enabled_flag;
        pictureParams.pic_fields.bits.weighted_pred_flag = m_ppsInfo.weighted_pred_flag;
        pictureParams.pic_fields.bits.weighted_bipred_flag = m_ppsInfo.weighted_bipred_flag;
        pictureParams.pic_fields.bits.transquant_bypass_enabled_flag =
            m_ppsInfo.transquant_bypass_enabled_flag;
        pictureParams.pic_fields.bits.tiles_enabled_flag = m_ppsInfo.tiles_enabled_flag;
        pictureParams.pic_fields.bits.entropy_coding_sync_enabled_flag =
            m_ppsInfo.entropy_coding_sync_enabled_flag;
        pictureParams.pic_fields.bits.pps_loop_filter_across_slices_enabled_flag =
            m_ppsInfo.pps_loop_filter_across_slices_enabled_flag;
        pictureParams.pic_fields.bits.loop_filter_across_tiles_enabled_flag =
            m_ppsInfo.loop_filter_across_tiles_enabled_flag;
        pictureParams.pic_fields.bits.pcm_loop_filter_disabled_flag =
            m_spsInfo.pcm_loop_filter_disabled_flag;
        pictureParams.pic_fields.bits.NoPicReorderingFlag = 1;
        pictureParams.pic_fields.bits.NoBiPredFlag = 1;
        pictureParams.sps_max_dec_pic_buffering_minus1 =
            static_cast<uint8_t>(m_spsInfo.sps_max_dec_pic_buffering_minus1);
        pictureParams.bit_depth_luma_minus8 = static_cast<uint8_t>(m_spsInfo.bit_depth_luma_minus8);
        pictureParams.bit_depth_chroma_minus8 =
            static_cast<uint8_t>(m_spsInfo.bit_depth_chroma_minus8);
        pictureParams.pcm_sample_bit_depth_luma_minus1 =
            static_cast<uint8_t>(m_spsInfo.pcm_sample_bit_depth_luma_minus1);
        pictureParams.pcm_sample_bit_depth_chroma_minus1 =
            static_cast<uint8_t>(m_spsInfo.pcm_sample_bit_depth_chroma_minus1);
        pictureParams.log2_min_luma_coding_block_size_minus3 =
            static_cast<uint8_t>(m_spsInfo.log2_min_luma_coding_block_size_minus3);
        pictureParams.log2_diff_max_min_luma_coding_block_size =
            static_cast<uint8_t>(m_spsInfo.log2_diff_max_min_luma_coding_block_size);
        pictureParams.log2_min_transform_block_size_minus2 =
            static_cast<uint8_t>(m_spsInfo.log2_min_transform_block_size_minus2);
        pictureParams.log2_diff_max_min_transform_block_size =
            static_cast<uint8_t>(m_spsInfo.log2_diff_max_min_transform_block_size);
        pictureParams.log2_min_pcm_luma_coding_block_size_minus3 =
            static_cast<uint8_t>(m_spsInfo.log2_min_pcm_luma_coding_block_size_minus3);
        pictureParams.log2_diff_max_min_pcm_luma_coding_block_size =
            static_cast<uint8_t>(m_spsInfo.log2_diff_max_min_pcm_luma_coding_block_size);
        pictureParams.max_transform_hierarchy_depth_intra =
            static_cast<uint8_t>(m_spsInfo.max_transform_hierarchy_depth_intra);
        pictureParams.max_transform_hierarchy_depth_inter =
            static_cast<uint8_t>(m_spsInfo.max_transform_hierarchy_depth_inter);
        pictureParams.init_qp_minus26 = static_cast<int8_t>(m_ppsInfo.init_qp_minus26);
        pictureParams.diff_cu_qp_delta_depth =
            static_cast<uint8_t>(m_ppsInfo.diff_cu_qp_delta_depth);
        pictureParams.pps_cb_qp_offset = static_cast<int8_t>(m_ppsInfo.pps_cb_qp_offset);
        pictureParams.pps_cr_qp_offset = static_cast<int8_t>(m_ppsInfo.pps_cr_qp_offset);
        pictureParams.log2_parallel_merge_level_minus2 =
            static_cast<uint8_t>(m_ppsInfo.log2_parallel_merge_level_minus2);
        pictureParams.num_tile_columns_minus1 =
            static_cast<uint8_t>(m_ppsInfo.num_tile_columns_minus1);
        pictureParams.num_tile_rows_minus1 =
            static_cast<uint8_t>(m_ppsInfo.num_tile_rows_minus1);
        for (int i = 0; i < 19; ++i) {
            pictureParams.column_width_minus1[i] = m_ppsInfo.column_width_minus1[i];
        }
        for (int i = 0; i < 21; ++i) {
            pictureParams.row_height_minus1[i] = m_ppsInfo.row_height_minus1[i];
        }
        pictureParams.slice_parsing_fields.bits.lists_modification_present_flag =
            m_ppsInfo.lists_modification_present_flag;
        pictureParams.slice_parsing_fields.bits.long_term_ref_pics_present_flag =
            m_spsInfo.long_term_ref_pics_present_flag;
        pictureParams.slice_parsing_fields.bits.sps_temporal_mvp_enabled_flag =
            m_spsInfo.sps_temporal_mvp_enabled_flag;
        pictureParams.slice_parsing_fields.bits.cabac_init_present_flag =
            m_ppsInfo.cabac_init_present_flag;
        pictureParams.slice_parsing_fields.bits.output_flag_present_flag =
            m_ppsInfo.output_flag_present_flag;
        pictureParams.slice_parsing_fields.bits.dependent_slice_segments_enabled_flag =
            m_ppsInfo.dependent_slice_segments_enabled_flag;
        pictureParams.slice_parsing_fields.bits.pps_slice_chroma_qp_offsets_present_flag =
            m_ppsInfo.pps_slice_chroma_qp_offsets_present_flag;
        pictureParams.slice_parsing_fields.bits.sample_adaptive_offset_enabled_flag =
            m_spsInfo.sample_adaptive_offset_enabled_flag;
        pictureParams.slice_parsing_fields.bits.deblocking_filter_override_enabled_flag =
            m_ppsInfo.deblocking_filter_override_enabled_flag;
        pictureParams.slice_parsing_fields.bits.pps_disable_deblocking_filter_flag =
            m_ppsInfo.pps_disable_deblocking_filter_flag;
        pictureParams.slice_parsing_fields.bits.slice_segment_header_extension_present_flag =
            m_ppsInfo.slice_segment_header_extension_present_flag;
        pictureParams.slice_parsing_fields.bits.RapPicFlag = au.isRap ? 1u : 0u;
        pictureParams.slice_parsing_fields.bits.IdrPicFlag = au.isIdr ? 1u : 0u;
        pictureParams.slice_parsing_fields.bits.IntraPicFlag = 1u;
        pictureParams.log2_max_pic_order_cnt_lsb_minus4 =
            static_cast<uint8_t>(m_spsInfo.log2_max_pic_order_cnt_lsb_minus4);
        pictureParams.num_short_term_ref_pic_sets =
            static_cast<uint8_t>(m_spsInfo.num_short_term_ref_pic_sets);
        pictureParams.num_long_term_ref_pic_sps =
            static_cast<uint8_t>(m_spsInfo.num_long_term_ref_pic_sps);
        pictureParams.num_ref_idx_l0_default_active_minus1 =
            static_cast<uint8_t>(m_ppsInfo.num_ref_idx_l0_default_active_minus1);
        pictureParams.num_ref_idx_l1_default_active_minus1 =
            static_cast<uint8_t>(m_ppsInfo.num_ref_idx_l1_default_active_minus1);
        pictureParams.pps_beta_offset_div2 = static_cast<int8_t>(m_ppsInfo.pps_beta_offset_div2);
        pictureParams.pps_tc_offset_div2 = static_cast<int8_t>(m_ppsInfo.pps_tc_offset_div2);
        pictureParams.num_extra_slice_header_bits =
            static_cast<uint8_t>(m_ppsInfo.num_extra_slice_header_bits);
        pictureParams.st_rps_bits = 0;

        VABufferID picParamBuffer = VA_INVALID_ID;
        if (m_vaCreateBufferFn(m_vaDisplay,
                               m_vaContext,
                               VAPictureParameterBufferType,
                               static_cast<unsigned int>(sizeof(pictureParams)),
                               1,
                               &pictureParams,
                               &picParamBuffer) != VA_STATUS_SUCCESS) {
            return false;
        }

        VABufferID sliceDataBuffer = VA_INVALID_ID;
        void* slicePayload = const_cast<char*>(packet.payload().constData());
        const unsigned int sliceSize = static_cast<unsigned int>(packet.payload().size());
        if (m_vaCreateBufferFn(m_vaDisplay,
                               m_vaContext,
                               VASliceDataBufferType,
                               sliceSize,
                               1,
                               slicePayload,
                               &sliceDataBuffer) != VA_STATUS_SUCCESS) {
            (void)m_vaDestroyBufferFn(m_vaDisplay, picParamBuffer);
            return false;
        }

        VABufferID buffers[2] = {picParamBuffer, sliceDataBuffer};
        bool ok = m_vaBeginPictureFn(m_vaDisplay, m_vaContext, targetSurface) == VA_STATUS_SUCCESS;
        if (ok) {
            ok = m_vaRenderPictureFn(m_vaDisplay, m_vaContext, buffers, 2) == VA_STATUS_SUCCESS;
        }
        if (ok) {
            ok = m_vaEndPictureFn(m_vaDisplay, m_vaContext) == VA_STATUS_SUCCESS;
        }
        if (ok) {
            ok = m_vaSyncSurfaceFn(m_vaDisplay, targetSurface) == VA_STATUS_SUCCESS;
        }
        if (ok) {
            if (m_emitNativeVaapiFrames && m_vaExportSurfaceHandleFn) {
                ok = emitNativeVaapiFrame_(targetSurface, packet.pts());
                if (!ok) {
                    ok = copySurfaceToFrame_(targetSurface, packet.pts());
                }
            } else {
                ok = copySurfaceToFrame_(targetSurface, packet.pts());
            }
        }

        (void)m_vaDestroyBufferFn(m_vaDisplay, sliceDataBuffer);
        (void)m_vaDestroyBufferFn(m_vaDisplay, picParamBuffer);
        if (ok) {
            return true;
        }
        return false;
#endif
    }

 #if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
    bool copySurfaceToFrame_(VASurfaceID surface, std::int64_t pts) {
#else
    bool copySurfaceToFrame_(int /*surface*/, std::int64_t /*pts*/) {
#endif
#if !SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        return false;
#else
        if (!m_vaDeriveImageFn || !m_vaDestroyImageFn || !m_vaMapBufferFn || !m_vaUnmapBufferFn) {
            return false;
        }

        VAImage image;
        std::memset(&image, 0, sizeof(image));
        if (m_vaDeriveImageFn(m_vaDisplay, surface, &image) != VA_STATUS_SUCCESS) {
            return false;
        }

        void* mapped = nullptr;
        if (m_vaMapBufferFn(m_vaDisplay, image.buf, &mapped) != VA_STATUS_SUCCESS || !mapped) {
            (void)m_vaDestroyImageFn(m_vaDisplay, image.image_id);
            return false;
        }

        const uint8_t* srcBase = static_cast<const uint8_t*>(mapped);
        SwVideoFrame frame = SwVideoFrame::allocate(image.width, image.height, SwVideoPixelFormat::YUV420P);
        if (!frame.isValid()) {
            (void)m_vaUnmapBufferFn(m_vaDisplay, image.buf);
            (void)m_vaDestroyImageFn(m_vaDisplay, image.image_id);
            return false;
        }

        const uint32_t fourcc = image.format.fourcc;
        if (fourcc == VA_FOURCC_NV12) {
            const uint8_t* srcY = srcBase + image.offsets[0];
            const uint8_t* srcUV = srcBase + image.offsets[1];
            const int width = image.width;
            const int height = image.height;
            for (int y = 0; y < height; ++y) {
                std::memcpy(frame.planeData(0) + y * frame.planeStride(0),
                            srcY + y * image.pitches[0],
                            static_cast<std::size_t>(width));
            }
            const int chromaHeight = (height + 1) / 2;
            const int chromaWidth = (width + 1) / 2;
            for (int y = 0; y < chromaHeight; ++y) {
                const uint8_t* srcRow = srcUV + y * image.pitches[1];
                uint8_t* dstU = frame.planeData(1) + y * frame.planeStride(1);
                uint8_t* dstV = frame.planeData(2) + y * frame.planeStride(2);
                for (int x = 0; x < chromaWidth; ++x) {
                    dstU[x] = srcRow[x * 2];
                    dstV[x] = srcRow[x * 2 + 1];
                }
            }
        } else {
            (void)m_vaUnmapBufferFn(m_vaDisplay, image.buf);
            (void)m_vaDestroyImageFn(m_vaDisplay, image.image_id);
            return false;
        }

        frame.setTimestamp(pts);
        emitFrame(frame);
        (void)m_vaUnmapBufferFn(m_vaDisplay, image.buf);
        (void)m_vaDestroyImageFn(m_vaDisplay, image.image_id);
        return true;
#endif
    }

#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
    static void closeExportedPrimeDescriptorFds_(VADRMPRIMESurfaceDescriptor& descriptor) {
        for (uint32_t i = 0; i < descriptor.num_objects && i < 4u; ++i) {
            if (descriptor.objects[i].fd >= 0) {
                ::close(descriptor.objects[i].fd);
                descriptor.objects[i].fd = -1;
            }
        }
    }

    static unsigned int vaRtFormatForExportedFourcc_(uint32_t fourcc) {
        switch (fourcc) {
        case VA_FOURCC_NV12:
            return VA_RT_FORMAT_YUV420;
#ifdef VA_FOURCC_P010
        case VA_FOURCC_P010:
#  ifdef VA_RT_FORMAT_YUV420_10BPP
            return VA_RT_FORMAT_YUV420_10BPP;
#  else
            return 0u;
#  endif
#endif
#ifdef VA_FOURCC_P016
        case VA_FOURCC_P016:
#  ifdef VA_RT_FORMAT_YUV420_12
            return VA_RT_FORMAT_YUV420_12;
#  else
            return 0u;
#  endif
#endif
        default:
            return 0u;
        }
    }

    static SwVideoFormatInfo nativeFrameFormatForExportedFourcc_(uint32_t fourcc,
                                                                 int width,
                                                                 int height) {
        switch (fourcc) {
        case VA_FOURCC_NV12:
            return SwDescribeVideoFormat(SwVideoPixelFormat::NV12, width, height);
#ifdef VA_FOURCC_P010
        case VA_FOURCC_P010:
            return SwDescribeVideoFormat(SwVideoPixelFormat::P010, width, height);
#endif
#ifdef VA_FOURCC_P016
        case VA_FOURCC_P016:
            return SwDescribeVideoFormat(SwVideoPixelFormat::P016, width, height);
#endif
        default:
            return SwVideoFormatInfo();
        }
    }

    bool emitNativeVaapiFrame_(VASurfaceID surface, std::int64_t pts) {
        if (!m_vaExportSurfaceHandleFn) {
            return false;
        }

        VADRMPRIMESurfaceDescriptor descriptor;
        std::memset(&descriptor, 0, sizeof(descriptor));
        if (m_vaExportSurfaceHandleFn(m_vaDisplay,
                                      surface,
                                      VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                      VA_EXPORT_SURFACE_READ_ONLY |
                                          VA_EXPORT_SURFACE_SEPARATE_LAYERS,
                                      &descriptor) != VA_STATUS_SUCCESS) {
            return false;
        }

        std::shared_ptr<SwVideoFrame::NativeVaapiPrimeStorage> storage(
            new SwVideoFrame::NativeVaapiPrimeStorage());
        storage->fourcc = descriptor.fourcc;
        storage->width = descriptor.width;
        storage->height = descriptor.height;
        storage->rtFormat = vaRtFormatForExportedFourcc_(descriptor.fourcc);
        storage->numObjects = std::min<uint32_t>(descriptor.num_objects,
                                                 static_cast<uint32_t>(storage->objects.size()));
        storage->numLayers = std::min<uint32_t>(descriptor.num_layers,
                                                static_cast<uint32_t>(storage->layers.size()));
        for (uint32_t i = 0; i < storage->numObjects; ++i) {
            storage->objects[i].fd = descriptor.objects[i].fd;
            storage->objects[i].size = descriptor.objects[i].size;
            storage->objects[i].drmFormatModifier = descriptor.objects[i].drm_format_modifier;
            descriptor.objects[i].fd = -1;
        }
        for (uint32_t i = 0; i < storage->numLayers; ++i) {
            storage->layers[i].drmFormat = descriptor.layers[i].drm_format;
            storage->layers[i].numPlanes = descriptor.layers[i].num_planes;
            for (int plane = 0; plane < 4; ++plane) {
                storage->layers[i].objectIndex[plane] = descriptor.layers[i].object_index[plane];
                storage->layers[i].offset[plane] = descriptor.layers[i].offset[plane];
                storage->layers[i].pitch[plane] = descriptor.layers[i].pitch[plane];
            }
        }
        closeExportedPrimeDescriptorFds_(descriptor);

        SwVideoFormatInfo info = nativeFrameFormatForExportedFourcc_(storage->fourcc,
                                                                     static_cast<int>(storage->width),
                                                                     static_cast<int>(storage->height));
        if (!info.isValid() || storage->rtFormat == 0u || storage->numObjects == 0u ||
            storage->numLayers == 0u) {
            return false;
        }

        SwVideoFrame frame = SwVideoFrame::wrapNativeVaapiPrime(info, storage);
        if (!frame.isValid()) {
            return false;
        }
        frame.setTimestamp(pts);
        emitFrame(frame);
        return true;
    }
#endif

    bool initializeVaapiProbe_() {
#if !SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        logUnavailableOnce_("VA-API headers unavailable at build time");
        return false;
#else
        if (m_vaLibrary || m_vaDrmLibrary) {
            return true;
        }

        static const char* kVaCandidates[] = {"libva.so.2", "libva.so"};
        static const char* kVaDrmCandidates[] = {"libva-drm.so.2", "libva-drm.so"};
        for (std::size_t i = 0; i < (sizeof(kVaCandidates) / sizeof(kVaCandidates[0])); ++i) {
            m_vaLibrary = dlopen(kVaCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_vaLibrary) {
                break;
            }
        }
        for (std::size_t i = 0; i < (sizeof(kVaDrmCandidates) / sizeof(kVaDrmCandidates[0])); ++i) {
            m_vaDrmLibrary = dlopen(kVaDrmCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_vaDrmLibrary) {
                break;
            }
        }
        if (!m_vaLibrary || !m_vaDrmLibrary) {
            logUnavailableOnce_("VA-API runtime libraries not found");
            shutdownVaapiProbe_();
            return false;
        }

        m_vaGetDisplayDrmFn =
            reinterpret_cast<VADisplay(*)(int)>(dlsym(m_vaDrmLibrary, "vaGetDisplayDRM"));
        m_vaInitializeFn =
            reinterpret_cast<VAStatus(*)(VADisplay, int*, int*)>(dlsym(m_vaLibrary, "vaInitialize"));
        m_vaTerminateFn =
            reinterpret_cast<VAStatus(*)(VADisplay)>(dlsym(m_vaLibrary, "vaTerminate"));
        m_vaGetConfigAttributesFn = reinterpret_cast<VAStatus(*)(
            VADisplay, VAProfile, VAEntrypoint, VAConfigAttrib*, int)>(
            dlsym(m_vaLibrary, "vaGetConfigAttributes"));
        m_vaCreateConfigFn = reinterpret_cast<VAStatus(*)(
            VADisplay, VAProfile, VAEntrypoint, VAConfigAttrib*, int, VAConfigID*)>(
            dlsym(m_vaLibrary, "vaCreateConfig"));
        m_vaDestroyConfigFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VAConfigID)>(dlsym(m_vaLibrary, "vaDestroyConfig"));
        m_vaCreateSurfacesFn = reinterpret_cast<VAStatus(*)(
            VADisplay, unsigned int, unsigned int, unsigned int, VASurfaceID*, unsigned int,
            VASurfaceAttrib*, unsigned int)>(dlsym(m_vaLibrary, "vaCreateSurfaces"));
        m_vaDestroySurfacesFn = reinterpret_cast<VAStatus(*)(
            VADisplay, VASurfaceID*, int)>(dlsym(m_vaLibrary, "vaDestroySurfaces"));
        m_vaCreateContextFn = reinterpret_cast<VAStatus(*)(
            VADisplay, VAConfigID, int, int, int, VASurfaceID*, int, VAContextID*)>(
            dlsym(m_vaLibrary, "vaCreateContext"));
        m_vaDestroyContextFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VAContextID)>(dlsym(m_vaLibrary, "vaDestroyContext"));
        m_vaCreateBufferFn = reinterpret_cast<VAStatus(*)(
            VADisplay, VAContextID, VABufferType, unsigned int, unsigned int, void*, VABufferID*)>(
            dlsym(m_vaLibrary, "vaCreateBuffer"));
        m_vaDestroyBufferFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VABufferID)>(dlsym(m_vaLibrary, "vaDestroyBuffer"));
        m_vaBeginPictureFn = reinterpret_cast<VAStatus(*)(
            VADisplay, VAContextID, VASurfaceID)>(dlsym(m_vaLibrary, "vaBeginPicture"));
        m_vaRenderPictureFn = reinterpret_cast<VAStatus(*)(
            VADisplay, VAContextID, VABufferID*, int)>(dlsym(m_vaLibrary, "vaRenderPicture"));
        m_vaEndPictureFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VAContextID)>(dlsym(m_vaLibrary, "vaEndPicture"));
        m_vaSyncSurfaceFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VASurfaceID)>(dlsym(m_vaLibrary, "vaSyncSurface"));
        m_vaDeriveImageFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VASurfaceID, VAImage*)>(dlsym(m_vaLibrary, "vaDeriveImage"));
        m_vaDestroyImageFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VAImageID)>(dlsym(m_vaLibrary, "vaDestroyImage"));
        m_vaMapBufferFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VABufferID, void**)>(dlsym(m_vaLibrary, "vaMapBuffer"));
        m_vaUnmapBufferFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VABufferID)>(dlsym(m_vaLibrary, "vaUnmapBuffer"));
        m_vaExportSurfaceHandleFn =
            reinterpret_cast<VAStatus(*)(VADisplay, VASurfaceID, uint32_t, uint32_t, void*)>(
                dlsym(m_vaLibrary, "vaExportSurfaceHandle"));
        if (!m_vaGetDisplayDrmFn || !m_vaInitializeFn || !m_vaTerminateFn ||
            !m_vaGetConfigAttributesFn || !m_vaCreateConfigFn || !m_vaDestroyConfigFn ||
            !m_vaCreateSurfacesFn || !m_vaDestroySurfacesFn || !m_vaCreateContextFn ||
            !m_vaDestroyContextFn || !m_vaCreateBufferFn || !m_vaDestroyBufferFn ||
            !m_vaBeginPictureFn || !m_vaRenderPictureFn || !m_vaEndPictureFn ||
            !m_vaSyncSurfaceFn || !m_vaDeriveImageFn || !m_vaDestroyImageFn ||
            !m_vaMapBufferFn || !m_vaUnmapBufferFn) {
            logUnavailableOnce_("VA-API symbols missing from runtime libraries");
            shutdownVaapiProbe_();
            return false;
        }

        static const char* kRenderNodes[] = {
            "/dev/dri/renderD128",
            "/dev/dri/renderD129",
            "/dev/dri/renderD130"
        };
        for (std::size_t i = 0; i < (sizeof(kRenderNodes) / sizeof(kRenderNodes[0])); ++i) {
            m_drmFd = ::open(kRenderNodes[i], O_RDWR);
            if (m_drmFd >= 0) {
                break;
            }
        }
        if (m_drmFd < 0) {
            logUnavailableOnce_("No VA-API DRM render node available");
            shutdownVaapiProbe_();
            return false;
        }

        m_vaDisplay = m_vaGetDisplayDrmFn(m_drmFd);
        if (!m_vaDisplay) {
            logUnavailableOnce_("vaGetDisplayDRM failed");
            shutdownVaapiProbe_();
            return false;
        }

        int major = 0;
        int minor = 0;
        if (m_vaInitializeFn(m_vaDisplay, &major, &minor) != VA_STATUS_SUCCESS) {
            logUnavailableOnce_("vaInitialize failed");
            shutdownVaapiProbe_();
            return false;
        }
        m_vaInitialized = true;
        return true;
#endif
    }

    void shutdownVaapiContext_() {
#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        if (m_vaContext != VA_INVALID_ID && m_vaDestroyContextFn && m_vaDisplay) {
            (void)m_vaDestroyContextFn(m_vaDisplay, m_vaContext);
        }
        m_vaContext = VA_INVALID_ID;
        if (m_surfaceCountCreated > 0 && m_vaDestroySurfacesFn && m_vaDisplay) {
            (void)m_vaDestroySurfacesFn(m_vaDisplay, m_surfaces, m_surfaceCountCreated);
        }
        for (int i = 0; i < kSurfaceCount; ++i) {
            m_surfaces[i] = VA_INVALID_SURFACE;
        }
        m_surfaceCountCreated = 0;
        if (m_vaConfig != VA_INVALID_ID && m_vaDestroyConfigFn && m_vaDisplay) {
            (void)m_vaDestroyConfigFn(m_vaDisplay, m_vaConfig);
        }
        m_vaConfig = VA_INVALID_ID;
        m_vaContextReady = false;
#endif
    }

    void shutdownVaapiProbe_() {
#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
        if (m_vaInitialized && m_vaTerminateFn && m_vaDisplay) {
            (void)m_vaTerminateFn(m_vaDisplay);
        }
        m_vaInitialized = false;
        m_vaDisplay = nullptr;
        m_vaGetDisplayDrmFn = nullptr;
        m_vaInitializeFn = nullptr;
        m_vaTerminateFn = nullptr;
        m_vaGetConfigAttributesFn = nullptr;
        m_vaCreateConfigFn = nullptr;
        m_vaDestroyConfigFn = nullptr;
        m_vaCreateSurfacesFn = nullptr;
        m_vaDestroySurfacesFn = nullptr;
        m_vaCreateContextFn = nullptr;
        m_vaDestroyContextFn = nullptr;
        m_vaCreateBufferFn = nullptr;
        m_vaDestroyBufferFn = nullptr;
        m_vaBeginPictureFn = nullptr;
        m_vaRenderPictureFn = nullptr;
        m_vaEndPictureFn = nullptr;
        m_vaSyncSurfaceFn = nullptr;
        m_vaDeriveImageFn = nullptr;
        m_vaDestroyImageFn = nullptr;
        m_vaMapBufferFn = nullptr;
        m_vaUnmapBufferFn = nullptr;
        m_vaExportSurfaceHandleFn = nullptr;
#endif
        if (m_drmFd >= 0) {
            ::close(m_drmFd);
            m_drmFd = -1;
        }
        if (m_vaDrmLibrary) {
            dlclose(m_vaDrmLibrary);
            m_vaDrmLibrary = nullptr;
        }
        if (m_vaLibrary) {
            dlclose(m_vaLibrary);
            m_vaLibrary = nullptr;
        }
    }

    void* m_libDe265Library{nullptr};
#if SW_MEDIA_LINUX_HAS_LIBDE265_HEADERS
    de265_decoder_context* m_de265Decoder{nullptr};
    De265NewDecoderFn m_de265NewDecoderFn{nullptr};
    De265FreeDecoderFn m_de265FreeDecoderFn{nullptr};
    De265PushDataFn m_de265PushDataFn{nullptr};
    De265PushEndOfFrameFn m_de265PushEndOfFrameFn{nullptr};
    De265DecodeFn m_de265DecodeFn{nullptr};
    De265GetNextPictureFn m_de265GetNextPictureFn{nullptr};
    De265ReleaseNextPictureFn m_de265ReleaseNextPictureFn{nullptr};
    De265GetImagePlaneFn m_de265GetImagePlaneFn{nullptr};
    De265GetImageWidthFn m_de265GetImageWidthFn{nullptr};
    De265GetImageHeightFn m_de265GetImageHeightFn{nullptr};
    De265GetBitsPerPixelFn m_de265GetBitsPerPixelFn{nullptr};
    De265GetChromaFormatFn m_de265GetChromaFormatFn{nullptr};
    De265GetImagePtsFn m_de265GetImagePtsFn{nullptr};
    De265GetErrorTextFn m_de265GetErrorTextFn{nullptr};
    De265IsOkFn m_de265IsOkFn{nullptr};
    De265SetVerbosityFn m_de265SetVerbosityFn{nullptr};
    De265SetParameterBoolFn m_de265SetParameterBoolFn{nullptr};
    De265SetParameterIntFn m_de265SetParameterIntFn{nullptr};
    De265FlushDataFn m_de265FlushDataFn{nullptr};
    De265ResetFn m_de265ResetFn{nullptr};
#endif
    void* m_vaLibrary{nullptr};
    void* m_vaDrmLibrary{nullptr};
    int m_drmFd{-1};
    bool m_opened{false};
    SwByteArray m_sequenceHeader;
    int m_inputWidth{0};
    int m_inputHeight{0};
    bool m_loggedSequenceHeader{false};
    SwHevcSpsInfo m_spsInfo;
    SwHevcPpsInfo m_ppsInfo;
    bool m_emitNativeVaapiFrames{false};
#if SW_MEDIA_LINUX_HAS_VAAPI_HEADERS
    static constexpr int kSurfaceCount = 4;
    VADisplay m_vaDisplay{nullptr};
    VADisplay (*m_vaGetDisplayDrmFn)(int){nullptr};
    VAStatus (*m_vaInitializeFn)(VADisplay, int*, int*){nullptr};
    VAStatus (*m_vaTerminateFn)(VADisplay){nullptr};
    VAStatus (*m_vaGetConfigAttributesFn)(VADisplay,
                                          VAProfile,
                                          VAEntrypoint,
                                          VAConfigAttrib*,
                                          int){nullptr};
    VAStatus (*m_vaCreateConfigFn)(VADisplay,
                                   VAProfile,
                                   VAEntrypoint,
                                   VAConfigAttrib*,
                                   int,
                                   VAConfigID*){nullptr};
    VAStatus (*m_vaDestroyConfigFn)(VADisplay, VAConfigID){nullptr};
    VAStatus (*m_vaCreateSurfacesFn)(VADisplay,
                                     unsigned int,
                                     unsigned int,
                                     unsigned int,
                                     VASurfaceID*,
                                     unsigned int,
                                     VASurfaceAttrib*,
                                     unsigned int){nullptr};
    VAStatus (*m_vaDestroySurfacesFn)(VADisplay, VASurfaceID*, int){nullptr};
    VAStatus (*m_vaCreateContextFn)(VADisplay,
                                    VAConfigID,
                                    int,
                                    int,
                                    int,
                                    VASurfaceID*,
                                    int,
                                    VAContextID*){nullptr};
    VAStatus (*m_vaDestroyContextFn)(VADisplay, VAContextID){nullptr};
    VAStatus (*m_vaCreateBufferFn)(VADisplay,
                                   VAContextID,
                                   VABufferType,
                                   unsigned int,
                                   unsigned int,
                                   void*,
                                   VABufferID*){nullptr};
    VAStatus (*m_vaDestroyBufferFn)(VADisplay, VABufferID){nullptr};
    VAStatus (*m_vaBeginPictureFn)(VADisplay, VAContextID, VASurfaceID){nullptr};
    VAStatus (*m_vaRenderPictureFn)(VADisplay, VAContextID, VABufferID*, int){nullptr};
    VAStatus (*m_vaEndPictureFn)(VADisplay, VAContextID){nullptr};
    VAStatus (*m_vaSyncSurfaceFn)(VADisplay, VASurfaceID){nullptr};
    VAStatus (*m_vaDeriveImageFn)(VADisplay, VASurfaceID, VAImage*){nullptr};
    VAStatus (*m_vaDestroyImageFn)(VADisplay, VAImageID){nullptr};
    VAStatus (*m_vaMapBufferFn)(VADisplay, VABufferID, void**){nullptr};
    VAStatus (*m_vaUnmapBufferFn)(VADisplay, VABufferID){nullptr};
    VAStatus (*m_vaExportSurfaceHandleFn)(VADisplay, VASurfaceID, uint32_t, uint32_t, void*){nullptr};
    bool m_vaInitialized{false};
    bool m_vaContextReady{false};
    VAConfigID m_vaConfig{VA_INVALID_ID};
    VAContextID m_vaContext{VA_INVALID_ID};
    VASurfaceID m_surfaces[kSurfaceCount] = {
        VA_INVALID_SURFACE,
        VA_INVALID_SURFACE,
        VA_INVALID_SURFACE,
        VA_INVALID_SURFACE
    };
    int m_surfaceCountCreated{0};
    int m_nextSurfaceIndex{0};
#endif
};

inline void swRegisterLinuxVideoDecoderAliases(SwVideoPacket::Codec codec,
                                               const SwString& displayName,
                                               SwVideoDecoderFactory::Creator autoCreator,
                                               SwVideoDecoderFactory::Creator hardwareCreator,
                                               SwVideoDecoderFactory::Creator softwareCreator,
                                               bool autoAvailable,
                                               bool hardwareAvailable,
                                               bool softwareAvailable) {
    SwVideoDecoderFactory::instance().registerDecoder(codec,
                                                      swPlatformVideoDecoderId(),
                                                      displayName,
                                                      autoCreator,
                                                      100,
                                                      false,
                                                      autoAvailable);
    SwVideoDecoderFactory::instance().registerDecoder(codec,
                                                      swPlatformHardwareVideoDecoderId(),
                                                      displayName + " Hardware",
                                                      hardwareCreator,
                                                      90,
                                                      false,
                                                      hardwareAvailable);
    SwVideoDecoderFactory::instance().registerDecoder(codec,
                                                      swPlatformSoftwareVideoDecoderId(),
                                                      displayName + " Software",
                                                      softwareCreator,
                                                      80,
                                                      false,
                                                      softwareAvailable);
}

inline bool swRegisterLinuxVideoDecoders() {
    static bool registered = false;
    if (registered) {
        return true;
    }
    registered = true;

    const bool hasOpenH264 = swLinuxOpenH264RuntimeAvailable();
    const bool hasVaapiH264 = false;
    const bool hasLibDe265 = swLinuxLibDe265RuntimeAvailable();
    const bool hasVaapiH265 = swLinuxVaapiH265RuntimeAvailable();
    const bool hasVaapiH265Native = hasVaapiH265 && swLinuxVaapiExportRuntimeAvailable();

    swRegisterLinuxVideoDecoderAliases(
        SwVideoPacket::Codec::H264,
        "Linux Decoder",
        []() {
            return std::make_shared<SwLinuxH264Decoder>(
                swLinuxOpenH264RuntimeAvailable() ? SwLinuxVideoDecoderBase::Backend::OpenH264
                                                  : SwLinuxVideoDecoderBase::Backend::Vaapi,
                "SwLinuxH264Decoder");
        },
        []() {
            return std::make_shared<SwLinuxH264Decoder>(SwLinuxVideoDecoderBase::Backend::Vaapi,
                                                        "SwLinuxH264DecoderHW");
        },
        []() {
            return std::make_shared<SwLinuxH264Decoder>(SwLinuxVideoDecoderBase::Backend::OpenH264,
                                                        "SwLinuxH264DecoderSW");
        },
        hasOpenH264,
        hasVaapiH264,
        hasOpenH264);

    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "linux-vaapi",
        "Linux VA-API",
        []() {
            return std::make_shared<SwLinuxH264Decoder>(SwLinuxVideoDecoderBase::Backend::Vaapi,
                                                        "SwLinuxH264DecoderVAAPI");
        },
        70,
        false,
        hasVaapiH264);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "linux-openh264",
        "Linux OpenH264",
        []() {
            return std::make_shared<SwLinuxH264Decoder>(SwLinuxVideoDecoderBase::Backend::OpenH264,
                                                        "SwLinuxH264DecoderOpenH264");
        },
        60,
        false,
        hasOpenH264);

    swRegisterLinuxVideoDecoderAliases(
        SwVideoPacket::Codec::H265,
        "Linux Decoder",
        []() {
            return std::make_shared<SwLinuxH265Decoder>(
                swLinuxLibDe265RuntimeAvailable() ? SwLinuxVideoDecoderBase::Backend::LibDe265
                                                  : SwLinuxVideoDecoderBase::Backend::Vaapi,
                                                        "SwLinuxH265Decoder");
        },
        []() {
            return std::make_shared<SwLinuxH265Decoder>(SwLinuxVideoDecoderBase::Backend::Vaapi,
                                                        "SwLinuxH265DecoderHW");
        },
        []() {
            return std::make_shared<SwLinuxH265Decoder>(
                SwLinuxVideoDecoderBase::Backend::LibDe265,
                "SwLinuxH265DecoderSW");
        },
        hasLibDe265 || hasVaapiH265,
        hasVaapiH265,
        hasLibDe265);

    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "linux-libde265",
        "Linux libde265",
        []() {
            return std::make_shared<SwLinuxH265Decoder>(
                SwLinuxVideoDecoderBase::Backend::LibDe265,
                "SwLinuxH265DecoderLibDe265");
        },
        65,
        false,
        hasLibDe265);

    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "linux-vaapi",
        "Linux VA-API",
        []() {
            return std::make_shared<SwLinuxH265Decoder>(SwLinuxVideoDecoderBase::Backend::Vaapi,
                                                        "SwLinuxH265DecoderVAAPI");
        },
        70,
        false,
        hasVaapiH265);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "linux-vaapi-native",
        "Linux VA-API Native",
        []() {
            return std::make_shared<SwLinuxH265Decoder>(SwLinuxVideoDecoderBase::Backend::Vaapi,
                                                        "SwLinuxH265DecoderVAAPINative",
                                                        true);
        },
        69,
        false,
        hasVaapiH265Native);

    return true;
}

static const bool g_swLinuxVideoDecodersRegistered = swRegisterLinuxVideoDecoders();

#endif
