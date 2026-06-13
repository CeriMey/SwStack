#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwHttpServer.h"
#include "SwObject.h"
#include "SwTimer.h"
#include "SwUdpSocket.h"
#include "media/SwVideoPacket.h"
#include "media/server/SwMediaServer.h"
#include "media/server/SwMediaServerFactory.h"
#include "media/server/SwVideoPublishStream.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <combaseapi.h>
#include <dshow.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace {

struct GatewayConfig {
    SwString bindAddress{"0.0.0.0"};
    SwString publicHost{"127.0.0.1"};
    uint16_t rtspPort{8554};
    uint16_t onvifPort{8899};
    bool ptzUdpEnabled{true};
    uint16_t ptzUdpPort{8900};
    SwString oscHost{"127.0.0.1"};
    uint16_t oscPort{16284};
    int oscDeviceIndex{0};
    bool oscIndexedArgs{true};
    bool oscEnabled{true};
    bool syntheticVideo{true};
    int syntheticFps{10};
    SwString streamCodec{"h264"};
    int streamBitrateKbps{500};
    int defaultSpeed{55};
    int ptzRepeatMs{10};
    int ptzDefaultTimeoutMs{150};
    bool ptzInvertPan{false};
    bool ptzInvertTilt{false};
    double ptzJsonSpeedScale{1.0};
    double ptzAxisSnapRatio{0.25};
    bool cameraVideo{true};
    int cameraDeviceIndex{0};
    SwString cameraDeviceName{"OBSBOT Tiny 2"};
    int cameraMaxWidth{1920};
    int cameraMaxHeight{1080};
    bool ffmpegFallback{true};
    SwString ffmpegPath{"ffmpeg"};
    SwString ffmpegDeviceName{"OBSBOT Virtual Camera"};
    int ffmpegWidth{720};
    int ffmpegHeight{1280};
    int ffmpegFps{60};
    SwString ffmpegInputCodec;
    SwString ffmpegPixelFormat{"nv12"};
    SwString ffmpegEncoder;
    bool startObsbotCenter{false};
    SwString obsbotCenterPath{"C:\\Program Files\\OBSBOT Center\\bin\\OBSBOT_Main.exe"};
    bool directShowPtz{true};
    bool directShowPan{true};
    bool directShowTilt{true};
    bool directShowZoom{true};
    SwString directShowPtzName{"OBSBOT Tiny 2"};
    SwString directShowTiltProperty{"auto"};
    bool directShowDumpControls{false};
    bool directShowReleaseAfterCommand{true};
    double directShowPtzSpeedScale{0.35};
};

int clampInt(int value, int low, int high) {
    return (std::max)(low, (std::min)(high, value));
}

double clampDouble(double value, double low, double high) {
    return (std::max)(low, (std::min)(high, value));
}

int speedFromAxis(double axis, int fallbackSpeed) {
    const double magnitude = std::fabs(axis);
    if (magnitude <= 0.0001) {
        return 0;
    }
    const int scaled = static_cast<int>(magnitude * 100.0 + 0.5);
    return clampInt(scaled <= 0 ? fallbackSpeed : scaled, 1, 100);
}

SwString xmlEscape(const SwString& value) {
    std::string out = value.toStdString();
    std::string escaped;
    escaped.reserve(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        switch (out[i]) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped += out[i]; break;
        }
    }
    return SwString(escaped);
}

SwString hostForUrls(const GatewayConfig& config) {
    const SwString host = config.publicHost.trimmed();
    if (host.isEmpty() || host == "0.0.0.0" || host == "::") {
        return "127.0.0.1";
    }
    return host;
}

SwString onvifBaseUrl(const GatewayConfig& config) {
    return SwString("http://") + hostForUrls(config) + ":" + SwString::number(config.onvifPort);
}

SwString rtspStreamUrl(const GatewayConfig& config) {
    return SwString("rtsp://") + hostForUrls(config) + ":" + SwString::number(config.rtspPort) + "/obsbot";
}

std::string lowerAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

SwVideoPacket::Codec streamPacketCodec(const GatewayConfig& config) {
    const std::string codec = lowerAscii(config.streamCodec.trimmed().toStdString());
    if (codec == "h265" || codec == "hevc") {
        return SwVideoPacket::Codec::H265;
    }
    return SwVideoPacket::Codec::H264;
}

bool streamCodecIsH265(const GatewayConfig& config) {
    return streamPacketCodec(config) == SwVideoPacket::Codec::H265;
}

SwString streamCodecLabel(const GatewayConfig& config) {
    return streamCodecIsH265(config) ? "H265" : "H264";
}

std::string quoteWindowsArg(const std::string& value) {
    std::string out = "\"";
    size_t backslashes = 0U;
    for (char ch : value) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            out.append(backslashes * 2U + 1U, '\\');
            out.push_back('"');
            backslashes = 0U;
            continue;
        }
        out.append(backslashes, '\\');
        backslashes = 0U;
        out.push_back(ch);
    }
    out.append(backslashes * 2U, '\\');
    out.push_back('"');
    return out;
}

bool startObsbotCenterIfRequested(const GatewayConfig& config, SwString* errorMessage) {
    if (!config.startObsbotCenter) {
        return true;
    }

#ifdef _WIN32
    const std::wstring path = config.obsbotCenterPath.toStdWString();
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        if (errorMessage) {
            *errorMessage = SwString("OBSBOT Center start failed: ") + config.obsbotCenterPath +
                            " (pass --start_obsbot_center=0 or --obsbot_center_path=...)";
        }
        return false;
    }
    return true;
#else
    if (errorMessage) {
        *errorMessage = "OBSBOT Center auto-start is only implemented on Windows";
    }
    return false;
#endif
}

SwString soapEnvelope(const SwString& body) {
    return SwString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
        "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
        "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" "
        "xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
        "<s:Body>") + body + "</s:Body></s:Envelope>";
}

SwHttpResponse soapResponse(const SwString& body) {
    SwHttpResponse response = swHttpTextResponse(
        200,
        soapEnvelope(body),
        "application/soap+xml; charset=utf-8");
    response.headers["cache-control"] = "no-store";
    return response;
}

SwHttpResponse soapFault(const SwString& reason) {
    return soapResponse(
        SwString("<s:Fault><s:Code><s:Value>s:Sender</s:Value></s:Code>")
        + "<s:Reason><s:Text xml:lang=\"en\">" + xmlEscape(reason)
        + "</s:Text></s:Reason></s:Fault>");
}

bool bodyContains(const SwHttpRequest& request, const char* token) {
    return request.body.toStdString().find(token) != std::string::npos;
}

double parseTagAttribute(const std::string& xml,
                         const std::string& tagName,
                         const std::string& attributeName,
                         double fallback) {
    try {
        const std::string number = "([-+]?(?:[0-9]*\\.)?[0-9]+(?:[eE][-+]?[0-9]+)?)";
        const std::regex rx("<[^>]*" + tagName + "[^>]*\\b" + attributeName +
                            "\\s*=\\s*[\"']" + number + "[\"']");
        std::smatch match;
        if (std::regex_search(xml, match, rx) && match.size() >= 2) {
            return std::atof(match[1].str().c_str());
        }
    } catch (const std::regex_error&) {
    }
    return fallback;
}

std::string trimAscii(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool jsonStringField(const std::string& json,
                     const std::string& name,
                     std::string& valueOut) {
    try {
        const std::regex rx("\"" + name + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch match;
        if (std::regex_search(json, match, rx) && match.size() >= 2) {
            valueOut = match[1].str();
            return true;
        }
    } catch (const std::regex_error&) {
    }
    return false;
}

double jsonNumberField(const std::string& json,
                       const std::string& name,
                       double fallback) {
    try {
        const std::string number = "([-+]?(?:[0-9]*\\.)?[0-9]+(?:[eE][-+]?[0-9]+)?)";
        const std::regex rx("\"" + name + "\"\\s*:\\s*" + number);
        std::smatch match;
        if (std::regex_search(json, match, rx) && match.size() >= 2) {
            return std::atof(match[1].str().c_str());
        }
    } catch (const std::regex_error&) {
    }
    return fallback;
}

bool jsonBoolField(const std::string& json,
                   const std::string& name,
                   bool fallback) {
    try {
        const std::regex rx("\"" + name + "\"\\s*:\\s*(true|false|1|0)",
                            std::regex_constants::icase);
        std::smatch match;
        if (std::regex_search(json, match, rx) && match.size() >= 2) {
            const std::string value = lowerAscii(match[1].str());
            return value == "true" || value == "1";
        }
    } catch (const std::regex_error&) {
    }
    return fallback;
}

int parseXsDurationMs(const std::string& value, int fallbackMs) {
    try {
        const std::string text = trimAscii(value);
        const std::string number = "([0-9]+(?:\\.[0-9]+)?)";
        const std::regex rx("^P(?:" + number + "D)?(?:T(?:" + number + "H)?(?:" +
                            number + "M)?(?:" + number + "S)?)?$",
                            std::regex_constants::icase);
        std::smatch match;
        if (!std::regex_match(text, match, rx) || match.size() < 5) {
            return fallbackMs;
        }

        const auto captureSeconds = [&match](size_t index) -> double {
            return match[index].matched ? std::atof(match[index].str().c_str()) : 0.0;
        };
        const double totalSeconds =
            captureSeconds(1) * 86400.0 +
            captureSeconds(2) * 3600.0 +
            captureSeconds(3) * 60.0 +
            captureSeconds(4);
        if (totalSeconds <= 0.0) {
            return fallbackMs;
        }
        return clampInt(static_cast<int>(totalSeconds * 1000.0 + 0.5), 1, 60000);
    } catch (const std::regex_error&) {
        return fallbackMs;
    }
}

int parseTagDurationMs(const std::string& xml,
                       const std::string& tagName,
                       int fallbackMs) {
    try {
        const std::regex rx("<[^>]*" + tagName + "\\b[^>]*>\\s*([^<]+)\\s*</[^>]*" +
                            tagName + "\\s*>",
                            std::regex_constants::icase);
        std::smatch match;
        if (std::regex_search(xml, match, rx) && match.size() >= 2) {
            return parseXsDurationMs(match[1].str(), fallbackMs);
        }
    } catch (const std::regex_error&) {
    }
    return fallbackMs;
}

void appendOscString(SwByteArray& out, const std::string& text) {
    out.append(text.data(), text.size());
    out.append('\0');
    while ((out.size() % 4U) != 0U) {
        out.append('\0');
    }
}

void appendOscInt32(SwByteArray& out, int value) {
    const uint32_t encoded = static_cast<uint32_t>(value);
    const char bytes[4] = {
        static_cast<char>((encoded >> 24U) & 0xFFU),
        static_cast<char>((encoded >> 16U) & 0xFFU),
        static_cast<char>((encoded >> 8U) & 0xFFU),
        static_cast<char>(encoded & 0xFFU)
    };
    out.append(bytes, sizeof(bytes));
}

SwByteArray makeOscMessage(const SwString& address, const std::vector<int>& args) {
    SwByteArray packet;
    appendOscString(packet, address.toStdString());
    std::string tags = ",";
    for (size_t i = 0; i < args.size(); ++i) {
        tags += "i";
    }
    appendOscString(packet, tags);
    for (size_t i = 0; i < args.size(); ++i) {
        appendOscInt32(packet, args[i]);
    }
    return packet;
}

SwByteArray makeSyntheticH264Payload(uint64_t frameIndex) {
    SwByteArray payload;
    const char startCode[] = {0x00, 0x00, 0x00, 0x01};
    const unsigned char sps[] = {
        0x67, 0x42, 0xC0, 0x1E, 0xDA, 0x02, 0x80, 0xF6,
        0xC0, 0x44, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00,
        0x00, 0x03, 0x00, 0xF1, 0x83, 0x19, 0x60
    };
    const unsigned char pps[] = {0x68, 0xCE, 0x06, 0xE2};

    payload.append(startCode, sizeof(startCode));
    payload.append(reinterpret_cast<const char*>(sps), sizeof(sps));
    payload.append(startCode, sizeof(startCode));
    payload.append(reinterpret_cast<const char*>(pps), sizeof(pps));
    payload.append(startCode, sizeof(startCode));
    payload.append(static_cast<char>(0x65));
    for (int i = 0; i < 180; ++i) {
        payload.append(static_cast<char>((static_cast<int>(frameIndex) * 17 + i * 7) & 0xFF));
    }
    return payload;
}

} // namespace

#ifdef _WIN32
class DirectShowPtzController {
public:
    explicit DirectShowPtzController(const GatewayConfig& config)
        : m_config(config) {}

    ~DirectShowPtzController() {
        release_();
        if (m_comInitialized) {
            CoUninitialize();
        }
    }

    bool continuousMove(double pan, double tilt, double zoom, SwString* message) {
        if (!m_config.directShowPtz || !ensureControl_(message)) {
            return false;
        }

        bool handled = false;
        bool changed = false;
        if (m_config.directShowPan && std::fabs(pan) >= 0.01) {
            handled = setDirectionalTarget_(CameraControl_Pan, pan, message) || isActiveTarget_(CameraControl_Pan) || handled;
            changed = isActiveTarget_(CameraControl_Pan) || changed;
        } else if (m_config.directShowPan) {
            changed = holdActiveTarget_(CameraControl_Pan, message) || changed;
            resetAccumulator_(CameraControl_Pan);
        }
        if (m_config.directShowTilt && std::fabs(tilt) >= 0.01) {
            handled = true;
            changed = adjust_(CameraControl_Tilt, tilt, message) || changed;
        } else if (m_config.directShowTilt) {
            changed = holdTiltTargets_(message) || changed;
            resetAccumulator_(CameraControl_Tilt);
            resetAccumulator_(CameraControl_Roll);
        }
        if (m_config.directShowZoom && std::fabs(zoom) >= 0.01) {
            const bool zoomChanged = adjust_(CameraControl_Zoom, zoom, message);
            handled = zoomChanged || isActiveTarget_(CameraControl_Zoom) || handled;
            changed = zoomChanged || changed;
        } else if (m_config.directShowZoom) {
            changed = holdActiveTarget_(CameraControl_Zoom, message) || changed;
            resetAccumulator_(CameraControl_Zoom);
        }
        if (changed && message) {
            *message = "DirectShow PTZ move";
        } else if (message) {
            *message = SwString();
        }
        releaseAfterCommand_();
        return handled || changed;
    }

    bool stop(SwString* message) {
        if (!m_config.directShowPtz) {
            return false;
        }
        if (m_config.directShowPan) {
            holdActiveTarget_(CameraControl_Pan, nullptr);
        }
        if (m_config.directShowTilt) {
            holdTiltTargets_(nullptr);
        }
        if (m_config.directShowZoom) {
            holdActiveTarget_(CameraControl_Zoom, nullptr);
        }
        resetAccumulators_();
        if (message) {
            *message = "DirectShow PTZ stop";
        }
        releaseAfterCommand_();
        return true;
    }

    bool gotoHome(SwString* message) {
        if (!m_config.directShowPtz || !ensureControl_(message)) {
            return false;
        }
        clearActiveTargets_();
        resetAccumulators_();
        bool ok = true;
        if (m_config.directShowPan) {
            ok = setDefault_(CameraControl_Pan, message) && ok;
        }
        if (m_config.directShowTilt) {
            ok = setTiltDefault_(message) && ok;
        }
        if (m_config.directShowZoom) {
            ok = setDefault_(CameraControl_Zoom, message) && ok;
        }
        if (ok && message) {
            *message = "DirectShow PTZ home";
        }
        releaseAfterCommand_();
        return ok;
    }

private:
    bool ensureCom_(SwString* message) {
        if (m_comReady) {
            return true;
        }
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            m_comInitialized = true;
            m_comReady = true;
            return true;
        }
        if (hr == RPC_E_CHANGED_MODE) {
            m_comReady = true;
            return true;
        }
        if (message) {
            *message = hresultMessage_("CoInitializeEx", hr);
        }
        return false;
    }

    bool ensureControl_(SwString* message) {
        if (m_control) {
            return true;
        }
        if (!ensureCom_(message)) {
            return false;
        }

        Microsoft::WRL::ComPtr<ICreateDevEnum> devEnum;
        HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&devEnum));
        if (FAILED(hr)) {
            if (message) {
                *message = hresultMessage_("CoCreateInstance(SystemDeviceEnum)", hr);
            }
            return false;
        }

        Microsoft::WRL::ComPtr<IEnumMoniker> enumMoniker;
        hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0);
        if (hr != S_OK || !enumMoniker) {
            if (message) {
                *message = "DirectShow PTZ camera enumeration failed";
            }
            return false;
        }

        Microsoft::WRL::ComPtr<IMoniker> moniker;
        ULONG fetched = 0;
        while (enumMoniker->Next(1, &moniker, &fetched) == S_OK) {
            const SwString name = friendlyName_(moniker.Get());
            if (name.toStdString().find(m_config.directShowPtzName.toStdString()) == std::string::npos) {
                moniker.Reset();
                continue;
            }

            Microsoft::WRL::ComPtr<IBaseFilter> filter;
            hr = moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&filter));
            if (FAILED(hr) || !filter) {
                if (message) {
                    *message = hresultMessage_("BindToObject(IBaseFilter)", hr);
                }
                return false;
            }

            hr = filter.As(&m_control);
            if (FAILED(hr) || !m_control) {
                if (message) {
                    *message = SwString("DirectShow PTZ unavailable on ") + name;
                }
                return false;
            }

            m_deviceName = name;
            dumpControls_();
            if (message) {
                *message = SwString("DirectShow PTZ ready on ") + name;
            }
            return true;
        }

        if (message) {
            *message = SwString("DirectShow PTZ camera not found: ") + m_config.directShowPtzName;
        }
        return false;
    }

    bool adjust_(CameraControlProperty property, double axis, SwString* message) {
        if (isUnsupported_(property)) {
            return false;
        }
        long minimum = 0;
        long maximum = 0;
        long step = 0;
        long defaultValue = 0;
        long caps = 0;
        HRESULT hr = m_control->GetRange(property, &minimum, &maximum, &step, &defaultValue, &caps);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                markUnsupported_(property);
            }
            if (message) {
                *message = SwString("DirectShow PTZ property unsupported: ") + propertyName_(property);
            }
            return false;
        }

        long current = 0;
        long flags = 0;
        hr = m_control->Get(property, &current, &flags);
        if (FAILED(hr)) {
            if (message) {
                *message = hresultMessage_("IAMCameraControl::Get", hr);
            }
            return false;
        }

        const long range = (std::max)(1L, maximum - minimum);
        const long minimumStep = (std::max)(1L, step);
        const double repeatScale = (std::max)(1.0, 100.0 / static_cast<double>((std::max)(1, m_config.ptzRepeatMs)));
        const double stepDivisor = 28.0 * repeatScale;
        const double speedScale = clampDouble(m_config.directShowPtzSpeedScale, 0.05, 2.0);
        const double rawStep = std::fabs(axis) * speedScale * static_cast<double>(range) / stepDivisor;
        double& accumulator = accumulatorFor_(property);
        accumulator += rawStep;
        const long stepUnits = static_cast<long>(accumulator / static_cast<double>(minimumStep));
        if (stepUnits <= 0) {
            return false;
        }
        long scaledStep = stepUnits * minimumStep;
        scaledStep = (std::max)(minimumStep, (std::min)(range, scaledStep));
        accumulator -= static_cast<double>(scaledStep);
        if (accumulator < 0.0) {
            accumulator = 0.0;
        }
        const long next = (std::max)(minimum, (std::min)(maximum, current + (axis > 0.0 ? scaledStep : -scaledStep)));
        if (next == current) {
            return false;
        }
        hr = m_control->Set(property, next, CameraControl_Flags_Manual);
        if (FAILED(hr)) {
            if (message) {
                *message = hresultMessage_("IAMCameraControl::Set", hr);
            }
            return false;
        }
        return true;
    }

    bool setDefault_(CameraControlProperty property, SwString* message) {
        if (isUnsupported_(property)) {
            return true;
        }
        long minimum = 0;
        long maximum = 0;
        long step = 0;
        long defaultValue = 0;
        long caps = 0;
        HRESULT hr = m_control->GetRange(property, &minimum, &maximum, &step, &defaultValue, &caps);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                markUnsupported_(property);
                return true;
            }
            if (message) {
                *message = hresultMessage_("IAMCameraControl::GetRange", hr);
            }
            return false;
        }
        const long target = (std::max)(minimum, (std::min)(maximum, defaultValue));
        hr = m_control->Set(property, target, CameraControl_Flags_Manual);
        if (FAILED(hr)) {
            if (message) {
                *message = hresultMessage_("IAMCameraControl::SetDefault", hr);
            }
            return false;
        }
        return true;
    }

    bool setDirectionalTarget_(CameraControlProperty property, double axis, SwString* message) {
        if (isUnsupported_(property)) {
            return false;
        }
        long minimum = 0;
        long maximum = 0;
        long step = 0;
        long defaultValue = 0;
        long caps = 0;
        HRESULT hr = m_control->GetRange(property, &minimum, &maximum, &step, &defaultValue, &caps);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                markUnsupported_(property);
            }
            if (message) {
                *message = SwString("DirectShow PTZ property unsupported: ") + propertyName_(property);
            }
            return false;
        }

        const long target = axis > 0.0 ? maximum : minimum;
        if (isActiveTarget_(property) && activeTarget_(property) == target) {
            return false;
        }
        hr = m_control->Set(property, target, CameraControl_Flags_Manual);
        if (FAILED(hr)) {
            if (message) {
                *message = hresultMessage_("IAMCameraControl::SetDirectionalTarget", hr);
            }
            return false;
        }
        setActiveTarget_(property, target);
        return true;
    }

    bool setTiltDirectionalTarget_(double axis, SwString* message) {
        const CameraControlProperty primary = tiltProperty_();
        if (!tiltAutoProperty_()) {
            return setDirectionalTarget_(primary, axis, message) || isActiveTarget_(primary);
        }
        bool handled = setDirectionalTarget_(CameraControl_Tilt, axis, message) || isActiveTarget_(CameraControl_Tilt);
        handled = setDirectionalTarget_(CameraControl_Roll, axis, message) || isActiveTarget_(CameraControl_Roll) || handled;
        return handled;
    }

    bool setTiltDefault_(SwString* message) {
        if (!tiltAutoProperty_()) {
            return setDefault_(tiltProperty_(), message);
        }
        bool ok = setDefault_(CameraControl_Tilt, message);
        ok = setDefault_(CameraControl_Roll, message) && ok;
        return ok;
    }

    bool holdTiltTargets_(SwString* message) {
        bool changed = holdActiveTarget_(CameraControl_Tilt, message);
        changed = holdActiveTarget_(CameraControl_Roll, message) || changed;
        return changed;
    }

    bool holdActiveTarget_(CameraControlProperty property, SwString* message) {
        if (!isActiveTarget_(property) || isUnsupported_(property)) {
            return false;
        }

        long current = 0;
        long flags = 0;
        HRESULT hr = m_control->Get(property, &current, &flags);
        if (FAILED(hr)) {
            if (message) {
                *message = hresultMessage_("IAMCameraControl::GetHoldTarget", hr);
            }
            return false;
        }
        hr = m_control->Set(property, current, CameraControl_Flags_Manual);
        if (FAILED(hr)) {
            if (message) {
                *message = hresultMessage_("IAMCameraControl::SetHoldTarget", hr);
            }
            return false;
        }
        clearActiveTarget_(property);
        return true;
    }

    SwString friendlyName_(IMoniker* moniker) {
        Microsoft::WRL::ComPtr<IPropertyBag> bag;
        HRESULT hr = moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag));
        if (FAILED(hr) || !bag) {
            return SwString();
        }
        VARIANT value;
        VariantInit(&value);
        hr = bag->Read(L"FriendlyName", &value, nullptr);
        SwString result;
        if (SUCCEEDED(hr) && value.vt == VT_BSTR && value.bstrVal) {
            result = SwString::fromWString(std::wstring(value.bstrVal, SysStringLen(value.bstrVal)));
        }
        VariantClear(&value);
        return result;
    }

    static SwString hresultMessage_(const char* label, HRESULT hr) {
        std::ostringstream out;
        out << label << " failed: 0x" << std::hex << static_cast<unsigned long>(hr);
        return SwString(out.str());
    }

    static SwString propertyName_(CameraControlProperty property) {
        switch (property) {
        case CameraControl_Pan: return "Pan";
        case CameraControl_Tilt: return "Tilt";
        case CameraControl_Roll: return "Roll";
        case CameraControl_Zoom: return "Zoom";
        default: return SwString::number(static_cast<int>(property));
        }
    }

    CameraControlProperty tiltProperty_() const {
        const std::string value = lowerAscii(m_config.directShowTiltProperty.trimmed().toStdString());
        if (value == "roll") {
            return CameraControl_Roll;
        }
        return CameraControl_Tilt;
    }

    bool tiltAutoProperty_() const {
        return lowerAscii(m_config.directShowTiltProperty.trimmed().toStdString()) == "auto";
    }

    bool isUnsupported_(CameraControlProperty property) const {
        return std::find(m_unsupportedProperties.begin(), m_unsupportedProperties.end(), property) !=
               m_unsupportedProperties.end();
    }

    void markUnsupported_(CameraControlProperty property) {
        if (!isUnsupported_(property)) {
            m_unsupportedProperties.push_back(property);
        }
    }

    double& accumulatorFor_(CameraControlProperty property) {
        switch (property) {
        case CameraControl_Pan: return m_panStepAccumulator;
        case CameraControl_Tilt: return m_tiltStepAccumulator;
        case CameraControl_Roll: return m_rollStepAccumulator;
        case CameraControl_Zoom: return m_zoomStepAccumulator;
        default: return m_panStepAccumulator;
        }
    }

    void resetAccumulator_(CameraControlProperty property) {
        accumulatorFor_(property) = 0.0;
    }

    void resetAccumulators_() {
        m_panStepAccumulator = 0.0;
        m_tiltStepAccumulator = 0.0;
        m_rollStepAccumulator = 0.0;
        m_zoomStepAccumulator = 0.0;
    }

    bool isActiveTarget_(CameraControlProperty property) const {
        switch (property) {
        case CameraControl_Pan: return m_panTargetActive;
        case CameraControl_Tilt: return m_tiltTargetActive;
        case CameraControl_Roll: return m_rollTargetActive;
        case CameraControl_Zoom: return m_zoomTargetActive;
        default: return false;
        }
    }

    long activeTarget_(CameraControlProperty property) const {
        switch (property) {
        case CameraControl_Pan: return m_panTarget;
        case CameraControl_Tilt: return m_tiltTarget;
        case CameraControl_Roll: return m_rollTarget;
        case CameraControl_Zoom: return m_zoomTarget;
        default: return 0;
        }
    }

    void setActiveTarget_(CameraControlProperty property, long target) {
        switch (property) {
        case CameraControl_Pan:
            m_panTargetActive = true;
            m_panTarget = target;
            break;
        case CameraControl_Tilt:
            m_tiltTargetActive = true;
            m_tiltTarget = target;
            break;
        case CameraControl_Roll:
            m_rollTargetActive = true;
            m_rollTarget = target;
            break;
        case CameraControl_Zoom:
            m_zoomTargetActive = true;
            m_zoomTarget = target;
            break;
        default:
            break;
        }
    }

    void clearActiveTarget_(CameraControlProperty property) {
        switch (property) {
        case CameraControl_Pan:
            m_panTargetActive = false;
            break;
        case CameraControl_Tilt:
            m_tiltTargetActive = false;
            break;
        case CameraControl_Roll:
            m_rollTargetActive = false;
            break;
        case CameraControl_Zoom:
            m_zoomTargetActive = false;
            break;
        default:
            break;
        }
    }

    void clearActiveTargets_() {
        m_panTargetActive = false;
        m_tiltTargetActive = false;
        m_rollTargetActive = false;
        m_zoomTargetActive = false;
    }

    void dumpControls_() {
        if (!m_config.directShowDumpControls || !m_control) {
            return;
        }
        const CameraControlProperty properties[] = {
            CameraControl_Pan,
            CameraControl_Tilt,
            CameraControl_Roll,
            CameraControl_Zoom,
        };
        for (CameraControlProperty property : properties) {
            long minimum = 0;
            long maximum = 0;
            long step = 0;
            long defaultValue = 0;
            long caps = 0;
            HRESULT hr = m_control->GetRange(property, &minimum, &maximum, &step, &defaultValue, &caps);
            if (FAILED(hr)) {
                std::cerr << "[ObsbotOnvifRtspGateway] DirectShow control "
                          << propertyName_(property).toStdString()
                          << " range failed: 0x" << std::hex << static_cast<unsigned long>(hr)
                          << std::dec << std::endl;
                continue;
            }
            long current = 0;
            long flags = 0;
            hr = m_control->Get(property, &current, &flags);
            std::cerr << "[ObsbotOnvifRtspGateway] DirectShow control "
                      << propertyName_(property).toStdString()
                      << " min=" << minimum
                      << " max=" << maximum
                      << " step=" << step
                      << " default=" << defaultValue
                      << " current=" << (SUCCEEDED(hr) ? current : 0)
                      << " caps=" << caps
                      << (SUCCEEDED(hr) ? "" : " current_failed")
                      << std::endl;
        }
    }

    void release_() {
        m_control.Reset();
    }

    void releaseAfterCommand_() {
        if (m_config.directShowReleaseAfterCommand) {
            release_();
        }
    }

    GatewayConfig m_config;
    bool m_comReady{false};
    bool m_comInitialized{false};
    SwString m_deviceName;
    Microsoft::WRL::ComPtr<IAMCameraControl> m_control;
    std::vector<CameraControlProperty> m_unsupportedProperties;
    double m_panStepAccumulator{0.0};
    double m_tiltStepAccumulator{0.0};
    double m_rollStepAccumulator{0.0};
    double m_zoomStepAccumulator{0.0};
    bool m_panTargetActive{false};
    bool m_tiltTargetActive{false};
    bool m_rollTargetActive{false};
    bool m_zoomTargetActive{false};
    long m_panTarget{0};
    long m_tiltTarget{0};
    long m_rollTarget{0};
    long m_zoomTarget{0};
};
#endif

class ObsbotOscPtzClient : public SwObject {
    SW_OBJECT(ObsbotOscPtzClient, SwObject)

public:
    explicit ObsbotOscPtzClient(const GatewayConfig& config, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_config(config)
        , m_moveTimer(this)
#ifdef _WIN32
        , m_directShow(config)
#endif
    {
        m_moveTimer.setInterval(m_config.ptzRepeatMs);
        SwObject::connect(&m_moveTimer, &SwTimer::timeout, this, &ObsbotOscPtzClient::onMoveTimeout);
    }

    ~ObsbotOscPtzClient() {
        m_moveTimer.stop();
    }

public slots:
    void continuousMove(double pan, double tilt, double zoom, int timeoutMs) {
        const bool active = std::fabs(pan) >= 0.01 ||
                            std::fabs(tilt) >= 0.01 ||
                            std::fabs(zoom) >= 0.01;
        if (!active) {
            stop(true, true);
            return;
        }

        const bool timerWasActive = m_moveTimer.isActive();
        m_activePan = pan;
        m_activeTilt = tilt;
        m_activeZoom = zoom;
        const int effectiveTimeoutMs = clampInt(timeoutMs > 0 ? timeoutMs : m_config.ptzDefaultTimeoutMs,
                                                m_config.ptzRepeatMs,
                                                60000);
        m_moveDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(effectiveTimeoutMs);
        m_moveActive = true;

        if (!timerWasActive) {
            executeContinuousMove(pan, tilt, zoom, true);
            m_moveTimer.start(m_config.ptzRepeatMs);
        }
    }

    void stop(bool panTilt, bool zoom) {
        if (!panTilt && !zoom) {
            return;
        }
        stopMoveTimer();
        executeStop(true);
    }

    void gotoHome() {
        stopMoveTimer();
        bool directShowHome = false;
#ifdef _WIN32
        SwString directShowMessage;
        if (m_directShow.gotoHome(&directShowMessage)) {
            commandSent(directShowMessage);
            directShowHome = true;
        } else if (!directShowMessage.isEmpty()) {
            errorOccurred(directShowMessage);
            if (!m_config.oscEnabled) {
                return;
            }
        }
#endif
        if (!m_config.oscEnabled) {
            return;
        }
        if (directShowHome && m_config.directShowPan && m_config.directShowTilt && m_config.directShowZoom) {
            return;
        }
        sendGeneralCommand("ResetGimbal", std::vector<int>());
    }

private slots:
    void onMoveTimeout() {
        if (!m_moveActive) {
            m_moveTimer.stop();
            return;
        }
        if (std::chrono::steady_clock::now() >= m_moveDeadline) {
            stopMoveTimer();
            executeStop(false);
            return;
        }
        executeContinuousMove(m_activePan, m_activeTilt, m_activeZoom, false);
    }

signals:
    DECLARE_SIGNAL(commandSent, const SwString&)
    DECLARE_SIGNAL(errorOccurred, const SwString&)

private:
    bool executeContinuousMove(double pan, double tilt, double zoom, bool report) {
        bool sent = false;
#ifdef _WIN32
        if (m_config.directShowPtz &&
            (m_config.directShowPan || m_config.directShowTilt || m_config.directShowZoom)) {
            SwString directShowMessage;
            const double directShowPan = m_config.directShowPan ? pan : 0.0;
            const double directShowTilt = m_config.directShowTilt ? tilt : 0.0;
            const double directShowZoom = m_config.directShowZoom ? zoom : 0.0;
            if (m_directShow.continuousMove(directShowPan, directShowTilt, directShowZoom, &directShowMessage)) {
                if (report && !directShowMessage.isEmpty()) {
                    commandSent(directShowMessage);
                }
                sent = true;
            } else if (!directShowMessage.isEmpty()) {
                if (report) {
                    errorOccurred(directShowMessage);
                }
            }
        }
#endif
        if (!m_config.oscEnabled) {
            return sent;
        }

        const double oscPan = m_config.directShowPan ? 0.0 : pan;
        const double oscTilt = m_config.directShowTilt ? 0.0 : tilt;
        SW_UNUSED(zoom);

        const int panSpeed = speedFromAxis(oscPan, m_config.defaultSpeed);
        const int tiltSpeed = speedFromAxis(oscTilt, m_config.defaultSpeed);

        if (panSpeed > 0) {
            sendGimbalCommand(oscPan > 0.0 ? "SetGimbalRight" : "SetGimbalLeft", panSpeed, report);
        } else {
            sendGimbalCommand("SetGimbalLeft", 0, report);
            sendGimbalCommand("SetGimbalRight", 0, report);
        }

        if (tiltSpeed > 0) {
            sendGimbalCommand(oscTilt > 0.0 ? "SetGimbalUp" : "SetGimbalDown", tiltSpeed, report);
        } else {
            sendGimbalCommand("SetGimbalUp", 0, report);
            sendGimbalCommand("SetGimbalDown", 0, report);
        }
        return sent || panSpeed > 0 || tiltSpeed > 0;
    }

    bool executeStop(bool report) {
        bool stopped = false;
#ifdef _WIN32
        if (m_config.directShowPtz &&
            (m_config.directShowPan || m_config.directShowTilt || m_config.directShowZoom)) {
            SwString directShowMessage;
            if (m_directShow.stop(&directShowMessage)) {
                if (report) {
                    commandSent(directShowMessage);
                }
                stopped = true;
            } else if (!directShowMessage.isEmpty()) {
                if (report) {
                    errorOccurred(directShowMessage);
                }
            }
        }
#endif
        if (!m_config.oscEnabled) {
            return stopped;
        }
        sendGimbalCommand("SetGimbalLeft", 0, report);
        sendGimbalCommand("SetGimbalRight", 0, report);
        sendGimbalCommand("SetGimbalUp", 0, report);
        sendGimbalCommand("SetGimbalDown", 0, report);
        return true;
    }

    void stopMoveTimer() {
        m_moveActive = false;
        if (m_moveTimer.isActive()) {
            m_moveTimer.stop();
        }
    }

    void sendGimbalCommand(const SwString& command, int speed, bool report = true) {
        sendGeneralCommand(command, std::vector<int>(1, clampInt(speed, 0, 100)), report);
    }

    void sendGeneralCommand(const SwString& command, const std::vector<int>& args, bool report = true) {
        std::vector<int> effectiveArgs;
        if (m_config.oscIndexedArgs) {
            effectiveArgs.push_back(m_config.oscDeviceIndex);
        }
        effectiveArgs.insert(effectiveArgs.end(), args.begin(), args.end());

        const SwString address = SwString("/OBSBOT/WebCam/General/") + command;
        const SwByteArray packet = makeOscMessage(address, effectiveArgs);
        const int64_t sent = m_socket.writeDatagram(packet.constData(),
                                                    static_cast<int64_t>(packet.size()),
                                                    m_config.oscHost,
                                                    m_config.oscPort);
        if (sent != static_cast<int64_t>(packet.size())) {
            errorOccurred(SwString("OSC send failed for ") + address);
            return;
        }
        if (report) {
            commandSent(address);
        }
    }

    GatewayConfig m_config;
    SwTimer m_moveTimer;
    bool m_moveActive{false};
    double m_activePan{0.0};
    double m_activeTilt{0.0};
    double m_activeZoom{0.0};
    std::chrono::steady_clock::time_point m_moveDeadline{};
#ifdef _WIN32
    DirectShowPtzController m_directShow;
#endif
    SwUdpSocket m_socket;
};

class UdpPtzGatewayService : public SwObject {
    SW_OBJECT(UdpPtzGatewayService, SwObject)

public:
    explicit UdpPtzGatewayService(const GatewayConfig& config, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_config(config)
        , m_socket(this) {
        SwObject::connect(&m_socket, &SwUdpSocket::readyRead, this, &UdpPtzGatewayService::onReadyRead);
    }

    bool start() {
        if (!m_config.ptzUdpEnabled) {
            return true;
        }
        if (!m_socket.bind(m_config.bindAddress,
                           m_config.ptzUdpPort,
                           SwUdpSocket::ShareAddress | SwUdpSocket::ReuseAddressHint)) {
            errorOccurred(SwString("UDP PTZ listen failed on ") + m_config.bindAddress + ":" +
                          SwString::number(m_config.ptzUdpPort) + " error=" +
                          SwString::number(m_socket.systemError()));
            return false;
        }
        started(SwString("udp://") + hostForUrls(m_config) + ":" + SwString::number(m_config.ptzUdpPort));
        return true;
    }

signals:
    DECLARE_SIGNAL(continuousMoveRequested, double, double, double, int)
    DECLARE_SIGNAL(stopRequested, bool, bool)
    DECLARE_SIGNAL_VOID(gotoHomeRequested)
    DECLARE_SIGNAL(started, const SwString&)
    DECLARE_SIGNAL(errorOccurred, const SwString&)
    DECLARE_SIGNAL(commandReceived, const SwString&)

private slots:
    void onReadyRead() {
        while (m_socket.hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            const SwByteArray datagram = m_socket.receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                continue;
            }
            handleDatagram(datagram, sender, senderPort);
        }
    }

private:
    void handleDatagram(const SwByteArray& datagram, const SwString& sender, uint16_t senderPort) {
        const std::string text(datagram.constData(), datagram.constData() + datagram.size());
        const std::string trimmed = trimAscii(text);
        if (!trimmed.empty() && trimmed[0] == '{') {
            handleJsonDatagram(trimmed, sender, senderPort);
            return;
        }

        std::istringstream in(trimmed);
        std::string magic;
        uint64_t sequence = 0;
        std::string command;
        in >> magic >> sequence >> command;
        if (magic != "VPTZ1" || command.empty()) {
            errorOccurred(SwString("UDP PTZ invalid packet from ") + sender + ":" +
                          SwString::number(senderPort));
            return;
        }
        if (m_haveSequence && sequence <= m_lastSequence) {
            return;
        }
        m_haveSequence = true;
        m_lastSequence = sequence;

        if (command == "MOVE") {
            double pan = 0.0;
            double tilt = 0.0;
            double zoom = 0.0;
            int timeoutMs = m_config.ptzDefaultTimeoutMs;
            in >> pan >> tilt >> zoom >> timeoutMs;
            if (!in) {
                errorOccurred(SwString("UDP PTZ invalid MOVE from ") + sender + ":" +
                              SwString::number(senderPort));
                return;
            }
            pan = (std::max)(-1.0, (std::min)(1.0, pan));
            tilt = (std::max)(-1.0, (std::min)(1.0, tilt));
            zoom = (std::max)(-1.0, (std::min)(1.0, zoom));
            timeoutMs = clampInt(timeoutMs, m_config.ptzRepeatMs, 60000);
            continuousMoveRequested(pan, tilt, zoom, timeoutMs);
            commandReceived(SwString("UDP MOVE seq=") + SwString::number(static_cast<unsigned long long>(sequence)) +
                            " from " + sender + ":" + SwString::number(senderPort));
            return;
        }
        if (command == "STOP") {
            stopRequested(true, true);
            commandReceived(SwString("UDP STOP seq=") + SwString::number(static_cast<unsigned long long>(sequence)));
            return;
        }
        if (command == "HOME") {
            gotoHomeRequested();
            commandReceived(SwString("UDP HOME seq=") + SwString::number(static_cast<unsigned long long>(sequence)));
            return;
        }
        errorOccurred(SwString("UDP PTZ unknown command: ") + SwString(command));
    }

    void handleJsonDatagram(const std::string& json, const SwString& sender, uint16_t senderPort) {
        std::string type;
        jsonStringField(json, "type", type);
        type = lowerAscii(type);

        if (type == "button") {
            std::string command;
            if (!jsonStringField(json, "value", command) &&
                !jsonStringField(json, "rawValue", command)) {
                jsonStringField(json, "label", command);
            }
            command = lowerAscii(trimAscii(command));
            if (command == "home") {
                gotoHomeRequested();
                commandReceived(SwString("UDP JSON HOME from ") + sender + ":" +
                                SwString::number(senderPort));
                return;
            }
            stopRequested(true, true);
            commandReceived(SwString("UDP JSON STOP from ") + sender + ":" +
                            SwString::number(senderPort));
            return;
        }

        if (type == "joystick" || json.find("\"x\"") != std::string::npos ||
            json.find("\"pan\"") != std::string::npos) {
            const bool active = jsonBoolField(json, "active", true);
            const double x = jsonNumberField(json, "pan", jsonNumberField(json, "x", 0.0));
            const double y = jsonNumberField(json, "tilt", jsonNumberField(json, "y", 0.0));
            const double zoomInput = jsonNumberField(json, "zoom", 0.0);
            const double speedScale = clampDouble(m_config.ptzJsonSpeedScale, 0.01, 1.0);
            const double rawMagnitude = std::sqrt(x * x + y * y);
            const double joystickSpeed = clampDouble(jsonNumberField(json, "speed", rawMagnitude), 0.0, 1.0);
            const double magnitude = clampDouble(joystickSpeed * speedScale, 0.0, 1.0);
            const double directionX = rawMagnitude >= 0.0001 ? x / rawMagnitude : 0.0;
            const double directionY = rawMagnitude >= 0.0001 ? y / rawMagnitude : 0.0;
            double pan = (std::max)(-1.0, (std::min)(1.0, directionX * magnitude));
            double tilt = (std::max)(-1.0, (std::min)(1.0, -directionY * magnitude));
            double zoom = (std::max)(-1.0, (std::min)(1.0, zoomInput));
            if (m_config.ptzInvertPan) {
                pan = -pan;
            }
            if (m_config.ptzInvertTilt) {
                tilt = -tilt;
            }
            const double axisSnapRatio = clampDouble(m_config.ptzAxisSnapRatio, 0.0, 1.0);
            if (axisSnapRatio > 0.0) {
                const double absPan = std::fabs(pan);
                const double absTilt = std::fabs(tilt);
                if (absPan > 0.0 && absPan < absTilt * axisSnapRatio) {
                    pan = 0.0;
                }
                if (absTilt > 0.0 && absTilt < absPan * axisSnapRatio) {
                    tilt = 0.0;
                }
            }
            if (!active || (std::fabs(pan) < 0.01 && std::fabs(tilt) < 0.01 && std::fabs(zoom) < 0.01)) {
                stopRequested(true, true);
                commandReceived(SwString("UDP JSON STOP from ") + sender + ":" +
                                SwString::number(senderPort));
                return;
            }
            const int timeoutMs = clampInt(m_config.ptzDefaultTimeoutMs,
                                           m_config.ptzRepeatMs,
                                           60000);
            continuousMoveRequested(pan, tilt, zoom, timeoutMs);
            commandReceived(SwString("UDP JSON MOVE pan=") + SwString::number(pan, 'f', 3) +
                            " tilt=" + SwString::number(tilt, 'f', 3) +
                            " speed=" + SwString::number(magnitude, 'f', 3) +
                            " from " + sender + ":" + SwString::number(senderPort));
            return;
        }

        errorOccurred(SwString("UDP PTZ unknown JSON packet from ") + sender + ":" +
                      SwString::number(senderPort));
    }

    GatewayConfig m_config;
    SwUdpSocket m_socket;
    bool m_haveSequence{false};
    uint64_t m_lastSequence{0};
};

class SyntheticRtspPublisher : public SwObject {
    SW_OBJECT(SyntheticRtspPublisher, SwObject)

public:
    explicit SyntheticRtspPublisher(int fps, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_timer(this)
        , m_fps(clampInt(fps, 1, 30)) {
        SwObject::connect(&m_timer, &SwTimer::timeout, this, &SyntheticRtspPublisher::onTimeout);
    }

    void start() {
        m_timer.start((std::max)(1, 1000 / m_fps));
    }

    void stop() {
        m_timer.stop();
    }

signals:
    DECLARE_SIGNAL(videoPacketReady, SwVideoPacket)

private slots:
    void onTimeout() {
        const int64_t pts = static_cast<int64_t>(m_frameIndex * 90000ULL / static_cast<uint64_t>(m_fps));
        SwVideoPacket packet(SwVideoPacket::Codec::H264,
                             makeSyntheticH264Payload(m_frameIndex),
                             pts,
                             pts,
                             true);
        if (m_frameIndex == 0U) {
            packet.setDiscontinuity(true);
        }
        ++m_frameIndex;
        videoPacketReady(packet);
    }

private:
    SwTimer m_timer;
    int m_fps{10};
    uint64_t m_frameIndex{0};
};

class NativeH264CameraPublisher : public SwObject {
    SW_OBJECT(NativeH264CameraPublisher, SwObject)

public:
    explicit NativeH264CameraPublisher(const GatewayConfig& config, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_config(config) {}

    ~NativeH264CameraPublisher() override {
        stop();
    }

    bool start() {
        if (!m_config.cameraVideo) {
            return false;
        }
        if (m_running.load()) {
            return true;
        }

        m_stopRequested.store(false);
        m_initReady = false;
        m_initOk = false;
        m_initMessage = SwString();

        m_thread = std::thread([this]() {
            run();
        });

        std::unique_lock<std::mutex> lock(m_initMutex);
        if (!m_initCv.wait_for(lock, std::chrono::seconds(4), [this]() { return m_initReady; })) {
            m_initMessage = "native H264 camera startup timed out";
            lock.unlock();
            stop();
            errorOccurred(m_initMessage);
            return false;
        }
        const bool ok = m_initOk;
        const SwString message = m_initMessage;
        lock.unlock();

        if (!ok) {
            stop();
            if (!message.isEmpty()) {
                errorOccurred(message);
            }
            return false;
        }

        started(message);
        return true;
    }

    void stop() {
        m_stopRequested.store(true);
#ifdef _WIN32
        {
            std::lock_guard<std::mutex> lock(m_readerMutex);
            if (m_reader) {
                m_reader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
            }
        }
#endif
        if (m_thread.joinable() && std::this_thread::get_id() != m_thread.get_id()) {
            m_thread.join();
        }
        m_running.store(false);
    }

signals:
    DECLARE_SIGNAL(videoPacketReady, SwVideoPacket)
    DECLARE_SIGNAL(started, const SwString&)
    DECLARE_SIGNAL(errorOccurred, const SwString&)

private:
    void notifyInitialized(bool ok, const SwString& message) {
        {
            std::lock_guard<std::mutex> lock(m_initMutex);
            m_initOk = ok;
            m_initMessage = message;
            m_initReady = true;
        }
        m_initCv.notify_all();
    }

#ifdef _WIN32
    struct NativeTypeChoice {
        Microsoft::WRL::ComPtr<IMFMediaType> type;
        UINT32 width{0};
        UINT32 height{0};
        UINT32 fpsNum{30};
        UINT32 fpsDen{1};
    };

    void run() {
        m_running.store(true);

        bool comInitialized = false;
        bool mfStarted = false;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized = true;
        } else if (hr != RPC_E_CHANGED_MODE) {
            notifyInitialized(false, hresultMessage_("CoInitializeEx", hr));
            m_running.store(false);
            return;
        }

        hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(hr)) {
            notifyInitialized(false, hresultMessage_("MFStartup", hr));
            cleanupCom_(mfStarted, comInitialized);
            m_running.store(false);
            return;
        }
        mfStarted = true;

        Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource;
        Microsoft::WRL::ComPtr<IMFSourceReader> reader;
        NativeTypeChoice choice;
        if (!openNativeH264Reader_(mediaSource, reader, choice)) {
            cleanupCom_(mfStarted, comInitialized);
            m_running.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_readerMutex);
            m_reader = reader;
        }

        notifyInitialized(true,
                          SwString("native camera H264 publisher enabled ")
                              + SwString::number(choice.width) + "x" + SwString::number(choice.height)
                              + "@" + SwString::number(choice.fpsNum) + "/" + SwString::number(choice.fpsDen)
                              + " device=" + SwString::number(m_config.cameraDeviceIndex)
                              + " name=\"" + m_config.cameraDeviceName + "\"");

        readSamples_(reader);

        {
            std::lock_guard<std::mutex> lock(m_readerMutex);
            m_reader.Reset();
        }
        if (mediaSource) {
            mediaSource->Shutdown();
        }
        cleanupCom_(mfStarted, comInitialized);
        m_running.store(false);
    }

    bool openNativeH264Reader_(Microsoft::WRL::ComPtr<IMFMediaSource>& mediaSource,
                               Microsoft::WRL::ComPtr<IMFSourceReader>& reader,
                               NativeTypeChoice& choice) {
        Microsoft::WRL::ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) {
            notifyInitialized(false, hresultMessage_("MFCreateAttributes", hr));
            return false;
        }
        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                 MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr)) {
            notifyInitialized(false, hresultMessage_("SetGUID", hr));
            return false;
        }

        IMFActivate** devices = nullptr;
        UINT32 deviceCount = 0;
        hr = MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount);
        if (FAILED(hr) || deviceCount == 0) {
            if (devices) {
                CoTaskMemFree(devices);
            }
            notifyInitialized(false, "no Media Foundation video capture device found");
            return false;
        }

        UINT32 index = static_cast<UINT32>(
            (std::min)(m_config.cameraDeviceIndex, static_cast<int>(deviceCount - 1)));
        const std::wstring requestedName = m_config.cameraDeviceName.toStdWString();
        if (!requestedName.empty()) {
            const std::wstring requestedLower = lowerWide_(requestedName);
            for (UINT32 i = 0; i < deviceCount; ++i) {
                WCHAR* friendlyName = nullptr;
                const HRESULT nameHr = devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                                                       &friendlyName,
                                                                       nullptr);
                if (SUCCEEDED(nameHr) && friendlyName) {
                    const std::wstring friendlyLower = lowerWide_(friendlyName);
                    CoTaskMemFree(friendlyName);
                    if (friendlyLower.find(requestedLower) != std::wstring::npos) {
                        index = i;
                        break;
                    }
                }
            }
        }
        hr = devices[index]->ActivateObject(IID_PPV_ARGS(&mediaSource));
        for (UINT32 i = 0; i < deviceCount; ++i) {
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
        if (FAILED(hr)) {
            notifyInitialized(false, hresultMessage_("ActivateObject", hr));
            return false;
        }

        Microsoft::WRL::ComPtr<IMFAttributes> readerAttributes;
        hr = MFCreateAttributes(&readerAttributes, 1);
        if (FAILED(hr)) {
            notifyInitialized(false, hresultMessage_("MFCreateAttributes(reader)", hr));
            return false;
        }
        readerAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);

        hr = MFCreateSourceReaderFromMediaSource(mediaSource.Get(), readerAttributes.Get(), &reader);
        if (FAILED(hr)) {
            notifyInitialized(false, hresultMessage_("MFCreateSourceReaderFromMediaSource", hr));
            return false;
        }

        hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (FAILED(hr)) {
            notifyInitialized(false, hresultMessage_("SetStreamSelection(video)", hr));
            return false;
        }

        if (!selectBestH264Type_(reader.Get(), choice)) {
            notifyInitialized(false, "camera does not expose a native H264 Media Foundation mode");
            return false;
        }

        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, choice.type.Get());
        if (FAILED(hr)) {
            notifyInitialized(false, hresultMessage_("SetCurrentMediaType(H264)", hr));
            return false;
        }
        return true;
    }

    bool selectBestH264Type_(IMFSourceReader* reader, NativeTypeChoice& choice) const {
        if (!reader) {
            return false;
        }

        uint64_t bestScore = 0;
        for (DWORD typeIndex = 0;; ++typeIndex) {
            Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
            const HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                          typeIndex,
                                                          &mediaType);
            if (hr == MF_E_NO_MORE_TYPES) {
                break;
            }
            if (FAILED(hr) || !mediaType) {
                continue;
            }

            GUID majorType = GUID_NULL;
            GUID subtype = GUID_NULL;
            if (FAILED(mediaType->GetGUID(MF_MT_MAJOR_TYPE, &majorType)) ||
                FAILED(mediaType->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
                !IsEqualGUID(majorType, MFMediaType_Video) ||
                !IsEqualGUID(subtype, MFVideoFormat_H264)) {
                continue;
            }

            UINT32 width = 0;
            UINT32 height = 0;
            UINT32 fpsNum = 30;
            UINT32 fpsDen = 1;
            MFGetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, &width, &height);
            MFGetAttributeRatio(mediaType.Get(), MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
            const uint64_t pixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
            const bool withinLimit =
                (m_config.cameraMaxWidth <= 0 || width <= static_cast<UINT32>(m_config.cameraMaxWidth)) &&
                (m_config.cameraMaxHeight <= 0 || height <= static_cast<UINT32>(m_config.cameraMaxHeight));
            const uint64_t fpsScore = fpsDen == 0 ? fpsNum : (fpsNum / fpsDen);
            const uint64_t score = (withinLimit ? (UINT64_C(1) << 62) : 0U) + pixels * 100U + fpsScore;
            if (!choice.type || score > bestScore) {
                choice.type = mediaType;
                choice.width = width;
                choice.height = height;
                choice.fpsNum = fpsNum == 0 ? 30 : fpsNum;
                choice.fpsDen = fpsDen == 0 ? 1 : fpsDen;
                bestScore = score;
            }
        }
        return choice.type != nullptr;
    }

    void readSamples_(const Microsoft::WRL::ComPtr<IMFSourceReader>& reader) {
        uint64_t frameIndex = 0;
        while (!m_stopRequested.load()) {
            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            Microsoft::WRL::ComPtr<IMFSample> sample;
            const HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                  0,
                                                  &streamIndex,
                                                  &flags,
                                                  &timestamp,
                                                  &sample);
            if (FAILED(hr)) {
                if (!m_stopRequested.load()) {
                    errorOccurred(hresultMessage_("ReadSample", hr));
                }
                break;
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) || m_stopRequested.load()) {
                break;
            }
            if (!sample) {
                continue;
            }

            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) {
                continue;
            }

            BYTE* data = nullptr;
            DWORD maxLength = 0;
            DWORD currentLength = 0;
            if (FAILED(buffer->Lock(&data, &maxLength, &currentLength))) {
                continue;
            }
            if (!data || currentLength == 0) {
                buffer->Unlock();
                continue;
            }

            SwByteArray payload(static_cast<size_t>(currentLength), '\0');
            std::memcpy(payload.data(), data, currentLength);
            buffer->Unlock();

            const int64_t ptsMs = timestamp >= 0 ? static_cast<int64_t>(timestamp / 10000) : -1;
            const bool keyFrame = h264LooksLikeKeyFrame_(payload);
            SwVideoPacket packet(SwVideoPacket::Codec::H264,
                                 std::move(payload),
                                 ptsMs,
                                 ptsMs,
                                 keyFrame);
            if (frameIndex == 0U) {
                packet.setDiscontinuity(true);
            }
            ++frameIndex;
            if (frameIndex <= 5U || frameIndex % 150U == 0U) {
                std::cerr << "[ObsbotOnvifRtspGateway] native H264 frame "
                          << frameIndex << " bytes=" << currentLength
                          << " key=" << (keyFrame ? 1 : 0)
                          << " pts_ms=" << ptsMs << std::endl;
            }
            videoPacketReady(packet);
        }
    }

    static std::wstring lowerWide_(std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    static bool h264LooksLikeKeyFrame_(const SwByteArray& payload) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
        const size_t size = payload.size();
        if (!data || size == 0U) {
            return false;
        }
        for (size_t i = 0; i + 4U < size; ++i) {
            size_t startCodeSize = 0;
            if (data[i] == 0x00U && data[i + 1U] == 0x00U && data[i + 2U] == 0x01U) {
                startCodeSize = 3U;
            } else if (i + 4U < size &&
                       data[i] == 0x00U && data[i + 1U] == 0x00U &&
                       data[i + 2U] == 0x00U && data[i + 3U] == 0x01U) {
                startCodeSize = 4U;
            }
            if (startCodeSize == 0U || i + startCodeSize >= size) {
                continue;
            }
            const uint8_t nalType = data[i + startCodeSize] & 0x1FU;
            if (nalType == 5U || nalType == 7U) {
                return true;
            }
        }
        return false;
    }

    static SwString hresultMessage_(const char* label, HRESULT hr) {
        std::ostringstream out;
        out << label << " failed: 0x" << std::hex << static_cast<unsigned long>(hr);
        return SwString(out.str());
    }

    static void cleanupCom_(bool mfStarted, bool comInitialized) {
        if (mfStarted) {
            MFShutdown();
        }
        if (comInitialized) {
            CoUninitialize();
        }
    }

    std::mutex m_readerMutex;
    Microsoft::WRL::ComPtr<IMFSourceReader> m_reader;
#else
    void run() {
        notifyInitialized(false, "native H264 camera publisher is only implemented on Windows");
    }
#endif

    GatewayConfig m_config;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
    std::mutex m_initMutex;
    std::condition_variable m_initCv;
    bool m_initReady{false};
    bool m_initOk{false};
    SwString m_initMessage;
};

class FfmpegH264CameraPublisher : public SwObject {
    SW_OBJECT(FfmpegH264CameraPublisher, SwObject)

public:
    explicit FfmpegH264CameraPublisher(const GatewayConfig& config, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_config(config) {}

    ~FfmpegH264CameraPublisher() override {
        stop();
    }

    bool start() {
        if (!m_config.ffmpegFallback || m_running.load()) {
            return false;
        }

        m_stopRequested.store(false);
        m_firstPacketSent.store(false);
        m_initReady = false;
        m_initOk = false;
        m_initMessage = SwString();
        m_buffer.clear();
        m_frameIndex = 0U;

        m_thread = std::thread([this]() {
            run();
        });

        std::unique_lock<std::mutex> lock(m_initMutex);
        if (!m_initCv.wait_for(lock, std::chrono::seconds(8), [this]() { return m_initReady; })) {
            m_initMessage = "ffmpeg camera startup timed out";
            lock.unlock();
            stop();
            errorOccurred(m_initMessage);
            return false;
        }
        const bool ok = m_initOk;
        const SwString message = m_initMessage;
        lock.unlock();

        if (!ok) {
            stop();
            if (!message.isEmpty()) {
                errorOccurred(message);
            }
            return false;
        }

        started(message);
        return true;
    }

    void stop() {
        m_stopRequested.store(true);
#ifdef _WIN32
        HANDLE process = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            process = m_processHandle;
        }
        if (m_thread.joinable()) {
            CancelSynchronousIo(static_cast<HANDLE>(m_thread.native_handle()));
        }
        if (process) {
            TerminateProcess(process, 0);
        }
#endif
        if (m_thread.joinable() && std::this_thread::get_id() != m_thread.get_id()) {
            m_thread.join();
        }
        m_running.store(false);
    }

signals:
    DECLARE_SIGNAL(videoPacketReady, SwVideoPacket)
    DECLARE_SIGNAL(started, const SwString&)
    DECLARE_SIGNAL(errorOccurred, const SwString&)

private:
    void notifyInitialized(bool ok, const SwString& message) {
        {
            std::lock_guard<std::mutex> lock(m_initMutex);
            if (m_initReady) {
                return;
            }
            m_initOk = ok;
            m_initMessage = message;
            m_initReady = true;
        }
        m_initCv.notify_all();
    }

#ifdef _WIN32
    void run() {
        m_running.store(true);

        SwString startError;
        if (!startFfmpeg_(&startError)) {
            notifyInitialized(false, startError);
            cleanupProcess_();
            m_running.store(false);
            return;
        }

        readH264Stream_();

        if (!m_firstPacketSent.load()) {
            notifyInitialized(false, SwString("ffmpeg camera stopped before first ") + streamCodecLabel(m_config) + " frame");
        }
        cleanupProcess_();
        m_running.store(false);
    }

    bool startFfmpeg_(SwString* message) {
        SECURITY_ATTRIBUTES securityAttributes;
        ZeroMemory(&securityAttributes, sizeof(securityAttributes));
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
            if (message) {
                *message = win32Message_("CreatePipe");
            }
            return false;
        }
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA startupInfo;
        ZeroMemory(&startupInfo, sizeof(startupInfo));
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startupInfo.hStdOutput = writePipe;
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION processInfo;
        ZeroMemory(&processInfo, sizeof(processInfo));

        const std::string command = ffmpegCommandLine_();
        std::vector<char> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back('\0');

        const BOOL created = CreateProcessA(nullptr,
                                            mutableCommand.data(),
                                            nullptr,
                                            nullptr,
                                            TRUE,
                                            CREATE_NO_WINDOW,
                                            nullptr,
                                            nullptr,
                                            &startupInfo,
                                            &processInfo);
        CloseHandle(writePipe);
        if (!created) {
            const SwString error = win32Message_("CreateProcess(ffmpeg)");
            CloseHandle(readPipe);
            if (message) {
                *message = error + " command=" + SwString(command);
            }
            return false;
        }

        CloseHandle(processInfo.hThread);
        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            m_processHandle = processInfo.hProcess;
            m_stdoutRead = readPipe;
        }
        return true;
    }

    std::string ffmpegCommandLine_() const {
        const int fps = clampInt(m_config.ffmpegFps, 1, 120);
        const int width = clampInt(m_config.ffmpegWidth, 1, 7680);
        const int height = clampInt(m_config.ffmpegHeight, 1, 4320);
        const int bitrate = clampInt(m_config.streamBitrateKbps, 64, 50000);
        const bool h265 = streamCodecIsH265(m_config);
        const std::string encoder =
            m_config.ffmpegEncoder.trimmed().isEmpty()
                ? (h265 ? "libx265" : "libx264")
                : m_config.ffmpegEncoder.toStdString();

        std::ostringstream command;
        command << quoteWindowsArg(m_config.ffmpegPath.toStdString())
                << " -hide_banner -loglevel warning"
                << " -f dshow"
                << " -rtbufsize 100M"
                << " -video_size " << width << "x" << height
                << " -framerate " << fps;
        if (!m_config.ffmpegInputCodec.trimmed().isEmpty()) {
            command << " -vcodec " << m_config.ffmpegInputCodec.toStdString();
        } else if (!m_config.ffmpegPixelFormat.trimmed().isEmpty()) {
            command << " -pixel_format " << m_config.ffmpegPixelFormat.toStdString();
        }
        command << " -i " << quoteWindowsArg(std::string("video=") + m_config.ffmpegDeviceName.toStdString())
                << " -an"
                << " -vf format=yuv420p"
                << " -c:v " << encoder
                << " -b:v " << bitrate << "k"
                << " -maxrate " << bitrate << "k"
                << " -bufsize " << bitrate << "k"
                << " -preset ultrafast"
                << " -tune zerolatency"
                << (h265
                        ? (std::string(" -x265-params ") + quoteWindowsArg("aud=1:repeat-headers=1:keyint=30:min-keyint=30:scenecut=0"))
                        : (std::string(" -x264-params ") + quoteWindowsArg("aud=1:repeat-headers=1:keyint=30:min-keyint=30:scenecut=0")))
                << " -g 30 -bf 0 -pix_fmt yuv420p"
                << (h265 ? " -f hevc pipe:1" : " -f h264 pipe:1");
        return command.str();
    }

    void readH264Stream_() {
        HANDLE readPipe = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            readPipe = m_stdoutRead;
        }
        if (!readPipe) {
            return;
        }

        uint8_t chunk[16384];
        while (!m_stopRequested.load()) {
            DWORD bytesRead = 0;
            const BOOL ok = ReadFile(readPipe, chunk, sizeof(chunk), &bytesRead, nullptr);
            if (!ok || bytesRead == 0U) {
                break;
            }
            appendEncodedBytes_(chunk, static_cast<size_t>(bytesRead));
        }
    }

    void appendEncodedBytes_(const uint8_t* data, size_t size) {
        if (!data || size == 0U) {
            return;
        }
        m_buffer.insert(m_buffer.end(), data, data + size);
        const bool h265 = streamCodecIsH265(m_config);

        while (true) {
            size_t firstAud = findAud_(m_buffer, 0U, h265);
            if (firstAud == npos_()) {
                if (m_buffer.size() > 1024U * 1024U) {
                    m_buffer.erase(m_buffer.begin(), m_buffer.end() - 4);
                }
                return;
            }
            if (firstAud > 0U) {
                m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(firstAud));
            }

            const size_t firstCodeSize = startCodeSizeAt_(m_buffer, 0U);
            const size_t secondAud = findAud_(m_buffer, firstCodeSize + 1U, h265);
            if (secondAud == npos_()) {
                if (m_buffer.size() > 8U * 1024U * 1024U) {
                    m_buffer.clear();
                }
                return;
            }

            emitAccessUnit_(m_buffer.data(), secondAud);
            m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(secondAud));
        }
    }

    void emitAccessUnit_(const uint8_t* data, size_t size) {
        if (!data || size == 0U || m_stopRequested.load()) {
            return;
        }

        SwByteArray payload;
        payload.reserve(size);
        payload.append(reinterpret_cast<const char*>(data), size);

        const int fps = clampInt(m_config.ffmpegFps, 1, 120);
        const int64_t ptsMs = static_cast<int64_t>((m_frameIndex * 1000ULL) / static_cast<uint64_t>(fps));
        const SwVideoPacket::Codec codec = streamPacketCodec(m_config);
        SwVideoPacket packet(codec,
                             payload,
                             ptsMs,
                             ptsMs,
                             containsKeyFrameNal_(data, size, codec));
        if (m_frameIndex == 0U) {
            packet.setDiscontinuity(true);
        }
        ++m_frameIndex;

        if (!m_firstPacketSent.exchange(true)) {
            notifyInitialized(true,
                              SwString("ffmpeg camera ") + streamCodecLabel(m_config) + " publisher enabled "
                                  + SwString::number(m_config.ffmpegWidth) + "x" + SwString::number(m_config.ffmpegHeight)
                                  + "@" + SwString::number(fps)
                                  + " device=\"" + m_config.ffmpegDeviceName + "\"");
        }
        videoPacketReady(packet);
    }

    static size_t npos_() {
        return static_cast<size_t>(-1);
    }

    static size_t startCodeSizeAt_(const std::vector<uint8_t>& data, size_t pos) {
        if (pos + 3U > data.size()) {
            return 0U;
        }
        if (data[pos] == 0x00U && data[pos + 1U] == 0x00U && data[pos + 2U] == 0x01U) {
            return 3U;
        }
        if (pos + 4U <= data.size() &&
            data[pos] == 0x00U && data[pos + 1U] == 0x00U &&
            data[pos + 2U] == 0x00U && data[pos + 3U] == 0x01U) {
            return 4U;
        }
        return 0U;
    }

    static size_t findStartCode_(const std::vector<uint8_t>& data, size_t from) {
        for (size_t pos = from; pos + 3U <= data.size(); ++pos) {
            if (startCodeSizeAt_(data, pos) != 0U) {
                return pos;
            }
        }
        return npos_();
    }

    static uint8_t nalTypeAt_(const std::vector<uint8_t>& data, size_t nalStart, bool h265) {
        if (nalStart >= data.size()) {
            return 0U;
        }
        if (h265) {
            if (nalStart + 1U >= data.size()) {
                return 0U;
            }
            return static_cast<uint8_t>((data[nalStart] >> 1U) & 0x3FU);
        }
        return static_cast<uint8_t>(data[nalStart] & 0x1FU);
    }

    static size_t findAud_(const std::vector<uint8_t>& data, size_t from, bool h265) {
        size_t pos = from;
        while (pos + 4U <= data.size()) {
            const size_t start = findStartCode_(data, pos);
            if (start == npos_()) {
                return npos_();
            }
            const size_t codeSize = startCodeSizeAt_(data, start);
            const size_t nalStart = start + codeSize;
            const uint8_t nalType = nalTypeAt_(data, nalStart, h265);
            if (nalType == (h265 ? 35U : 9U)) {
                return start;
            }
            pos = nalStart + 1U;
        }
        return npos_();
    }

    static size_t startCodeSizeAtRaw_(const uint8_t* data, size_t size, size_t pos) {
        if (!data || pos + 3U > size) {
            return 0U;
        }
        if (data[pos] == 0x00U && data[pos + 1U] == 0x00U && data[pos + 2U] == 0x01U) {
            return 3U;
        }
        if (pos + 4U <= size &&
            data[pos] == 0x00U && data[pos + 1U] == 0x00U &&
            data[pos + 2U] == 0x00U && data[pos + 3U] == 0x01U) {
            return 4U;
        }
        return 0U;
    }

    static size_t findStartCodeRaw_(const uint8_t* data, size_t size, size_t from) {
        for (size_t pos = from; pos + 3U <= size; ++pos) {
            if (startCodeSizeAtRaw_(data, size, pos) != 0U) {
                return pos;
            }
        }
        return npos_();
    }

    static uint8_t nalTypeAtRaw_(const uint8_t* data, size_t size, size_t nalStart, SwVideoPacket::Codec codec) {
        if (!data || nalStart >= size) {
            return 0U;
        }
        if (codec == SwVideoPacket::Codec::H265) {
            if (nalStart + 1U >= size) {
                return 0U;
            }
            return static_cast<uint8_t>((data[nalStart] >> 1U) & 0x3FU);
        }
        return static_cast<uint8_t>(data[nalStart] & 0x1FU);
    }

    static bool isKeyFrameNalType_(uint8_t nalType, SwVideoPacket::Codec codec) {
        if (codec == SwVideoPacket::Codec::H265) {
            return nalType >= 16U && nalType <= 21U;
        }
        return nalType == 5U;
    }

    static bool containsKeyFrameNal_(const uint8_t* data, size_t size, SwVideoPacket::Codec codec) {
        size_t pos = 0U;
        while (pos + 4U <= size) {
            const size_t start = findStartCodeRaw_(data, size, pos);
            if (start == npos_()) {
                return false;
            }
            const size_t codeSize = startCodeSizeAtRaw_(data, size, start);
            const size_t nalStart = start + codeSize;
            if (isKeyFrameNalType_(nalTypeAtRaw_(data, size, nalStart, codec), codec)) {
                return true;
            }
            pos = nalStart + 1U;
        }
        return false;
    }

    void cleanupProcess_() {
        std::lock_guard<std::mutex> lock(m_processMutex);
        if (m_stdoutRead) {
            CloseHandle(m_stdoutRead);
            m_stdoutRead = nullptr;
        }
        if (m_processHandle) {
            WaitForSingleObject(m_processHandle, 500);
            CloseHandle(m_processHandle);
            m_processHandle = nullptr;
        }
    }

    static SwString win32Message_(const char* label) {
        return SwString(label) + " failed: " + SwString::number(static_cast<int>(GetLastError()));
    }

    std::mutex m_processMutex;
    HANDLE m_processHandle{nullptr};
    HANDLE m_stdoutRead{nullptr};
#else
    void run() {
        notifyInitialized(false, "ffmpeg camera publisher is only implemented on Windows");
    }
#endif

    GatewayConfig m_config;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_firstPacketSent{false};
    std::thread m_thread;
    std::mutex m_initMutex;
    std::condition_variable m_initCv;
    bool m_initReady{false};
    bool m_initOk{false};
    SwString m_initMessage;
    std::vector<uint8_t> m_buffer;
    uint64_t m_frameIndex{0};
};

class RtspGatewayService : public SwObject {
    SW_OBJECT(RtspGatewayService, SwObject)

public:
    explicit RtspGatewayService(const GatewayConfig& config, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_config(config)
        , m_server(makeConfig(config)) {
        m_transport = SwMediaServerFactory::createTransport(makeConfig(config));
        m_server.setTransport(m_transport);
    }

    bool start() {
        SwVideoPublishStream stream;
        stream.id = "main";
        stream.trackId = "video";
        stream.codec = streamPacketCodec(m_config);
        stream.width = static_cast<uint32_t>(clampInt(m_config.ffmpegWidth, 1, 7680));
        stream.height = static_cast<uint32_t>(clampInt(m_config.ffmpegHeight, 1, 4320));
        stream.fpsNumerator = static_cast<uint32_t>(clampInt(m_config.ffmpegFps, 1, 120));
        stream.fpsDenominator = 1;
        stream.startBitrateKbps = m_config.streamBitrateKbps;
        stream.minBitrateKbps = m_config.streamBitrateKbps;
        stream.maxBitrateKbps = m_config.streamBitrateKbps;
        if (!m_server.addVideoStream(stream)) {
            errorOccurred("RTSP stream registration failed");
            return false;
        }
        if (!m_server.start()) {
            errorOccurred(SwString("RTSP listen failed on ") + m_config.bindAddress + ":" +
                          SwString::number(m_config.rtspPort));
            return false;
        }
        started(rtspStreamUrl(m_config));
        return true;
    }

    void stop() {
        m_server.stop();
    }

public slots:
    void publishPacket(SwVideoPacket packet) {
        m_server.publishVideoPacket("main", packet);
    }

signals:
    DECLARE_SIGNAL(started, const SwString&)
    DECLARE_SIGNAL(errorOccurred, const SwString&)

private:
    static SwMediaServerConfig makeConfig(const GatewayConfig& config) {
        SwMediaServerConfig mediaConfig;
        mediaConfig.name = "OBSBOT ONVIF RTSP Gateway";
        mediaConfig.endpoint.protocol = SwMediaTransportProtocol::Rtsp;
        mediaConfig.endpoint.host = hostForUrls(config);
        mediaConfig.endpoint.bindAddress = config.bindAddress;
        mediaConfig.endpoint.port = config.rtspPort;
        mediaConfig.endpoint.deliveryMode = SwMediaTransportDeliveryMode::Unicast;
        mediaConfig.lowLatency = true;
        mediaConfig.maxClients = 16;
        mediaConfig.mtuBytes = 1200;
        return mediaConfig;
    }

    GatewayConfig m_config;
    SwMediaServer m_server;
    std::shared_ptr<SwVideoTransportServer> m_transport;
};

class OnvifGatewayService : public SwObject {
    SW_OBJECT(OnvifGatewayService, SwObject)

public:
    explicit OnvifGatewayService(const GatewayConfig& config, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_config(config) {
        installRoutes();
    }

    bool start() {
        if (!m_server.listenHttp(m_config.bindAddress, m_config.onvifPort)) {
            errorOccurred(SwString("ONVIF listen failed on ") + m_config.bindAddress + ":" +
                          SwString::number(m_config.onvifPort));
            return false;
        }
        started(onvifBaseUrl(m_config));
        return true;
    }

signals:
    DECLARE_SIGNAL(continuousMoveRequested, double, double, double, int)
    DECLARE_SIGNAL(stopRequested, bool, bool)
    DECLARE_SIGNAL_VOID(gotoHomeRequested)
    DECLARE_SIGNAL(started, const SwString&)
    DECLARE_SIGNAL(errorOccurred, const SwString&)

private:
    void installRoutes() {
        m_server.addRoute("GET", "/", [this](const SwHttpRequest& request) {
            SW_UNUSED(request);
            return handleBrowserInfo();
        });
        m_server.addRoute("GET", "/onvif/device_service", [this](const SwHttpRequest& request) {
            SW_UNUSED(request);
            return handleBrowserInfo();
        });
        m_server.addRoute("GET", "/onvif/media_service", [this](const SwHttpRequest& request) {
            SW_UNUSED(request);
            return handleBrowserInfo();
        });
        m_server.addRoute("GET", "/onvif/ptz_service", [this](const SwHttpRequest& request) {
            SW_UNUSED(request);
            return handleBrowserInfo();
        });
        m_server.addRoute("GET", "/onvif/Media", [this](const SwHttpRequest& request) {
            SW_UNUSED(request);
            return handleBrowserInfo();
        });
        m_server.addRoute("GET", "/onvif/PTZ", [this](const SwHttpRequest& request) {
            SW_UNUSED(request);
            return handleBrowserInfo();
        });

        m_server.addRoute("POST", "/onvif/device_service", [this](const SwHttpRequest& request) {
            return handleSoap(request);
        });
        m_server.addRoute("POST", "/onvif/media_service", [this](const SwHttpRequest& request) {
            return handleSoap(request);
        });
        m_server.addRoute("POST", "/onvif/ptz_service", [this](const SwHttpRequest& request) {
            return handleSoap(request);
        });
        m_server.addRoute("POST", "/onvif/Media", [this](const SwHttpRequest& request) {
            return handleSoap(request);
        });
        m_server.addRoute("POST", "/onvif/PTZ", [this](const SwHttpRequest& request) {
            return handleSoap(request);
        });
    }

    SwHttpResponse handleBrowserInfo() const {
        return swHttpTextResponse(
            200,
            SwString("OBSBOT ONVIF/RTSP gateway\n")
                + "ONVIF device service: " + onvifBaseUrl(m_config) + "/onvif/device_service\n"
                + "RTSP stream: " + rtspStreamUrl(m_config) + "\n"
                + "Note: ONVIF clients must use SOAP POST on /onvif/device_service, /onvif/media_service and /onvif/ptz_service.\n");
    }

    SwHttpResponse handleSoap(const SwHttpRequest& request) {
        if (bodyContains(request, "GetCapabilities")) {
            return handleGetCapabilities();
        }
        if (bodyContains(request, "GetServices")) {
            return handleGetServices();
        }
        if (bodyContains(request, "GetDeviceInformation")) {
            return handleGetDeviceInformation();
        }
        if (bodyContains(request, "GetSystemDateAndTime")) {
            return handleGetSystemDateAndTime();
        }
        if (bodyContains(request, "GetProfiles")) {
            return handleGetProfiles();
        }
        if (bodyContains(request, "GetStreamUri")) {
            return handleGetStreamUri();
        }
        if (bodyContains(request, "ContinuousMove")) {
            return handleContinuousMove(request);
        }
        if (bodyContains(request, "Stop")) {
            stopRequested(true, true);
            return soapResponse("<tptz:StopResponse/>");
        }
        if (bodyContains(request, "GotoHomePosition")) {
            gotoHomeRequested();
            return soapResponse("<tptz:GotoHomePositionResponse/>");
        }
        if (bodyContains(request, "GetStatus")) {
            return handleGetStatus();
        }
        return soapFault("ONVIF action not implemented by this gateway example");
    }

    SwHttpResponse handleGetCapabilities() const {
        const SwString base = onvifBaseUrl(m_config);
        return soapResponse(
            SwString("<tds:GetCapabilitiesResponse><tds:Capabilities>")
            + "<tt:Device><tt:XAddr>" + base + "/onvif/device_service</tt:XAddr></tt:Device>"
            + "<tt:Media><tt:XAddr>" + base + "/onvif/media_service</tt:XAddr></tt:Media>"
            + "<tt:PTZ><tt:XAddr>" + base + "/onvif/ptz_service</tt:XAddr></tt:PTZ>"
            + "</tds:Capabilities></tds:GetCapabilitiesResponse>");
    }

    SwHttpResponse handleGetServices() const {
        const SwString base = onvifBaseUrl(m_config);
        return soapResponse(
            SwString("<tds:GetServicesResponse>")
            + serviceXml("http://www.onvif.org/ver10/device/wsdl", base + "/onvif/device_service")
            + serviceXml("http://www.onvif.org/ver10/media/wsdl", base + "/onvif/media_service")
            + serviceXml("http://www.onvif.org/ver20/ptz/wsdl", base + "/onvif/ptz_service")
            + "</tds:GetServicesResponse>");
    }

    SwHttpResponse handleGetDeviceInformation() const {
        return soapResponse(
            "<tds:GetDeviceInformationResponse>"
            "<tds:Manufacturer>OBSBOT</tds:Manufacturer>"
            "<tds:Model>Tiny 2 via SwStack Gateway</tds:Model>"
            "<tds:FirmwareVersion>swstack-example</tds:FirmwareVersion>"
            "<tds:SerialNumber>local-usb</tds:SerialNumber>"
            "<tds:HardwareId>obsbot-tiny2-usb</tds:HardwareId>"
            "</tds:GetDeviceInformationResponse>");
    }

    SwHttpResponse handleGetSystemDateAndTime() const {
        return soapResponse(
            "<tds:GetSystemDateAndTimeResponse>"
            "<tds:SystemDateAndTime>"
            "<tt:DateTimeType>Manual</tt:DateTimeType>"
            "<tt:DaylightSavings>false</tt:DaylightSavings>"
            "</tds:SystemDateAndTime>"
            "</tds:GetSystemDateAndTimeResponse>");
    }

    SwHttpResponse handleGetProfiles() const {
        return soapResponse(
            "<trt:GetProfilesResponse>"
            "<trt:Profiles token=\"profile0\" fixed=\"true\">"
            "<tt:Name>OBSBOT Tiny 2</tt:Name>"
            "<tt:VideoSourceConfiguration token=\"video_source0\">"
            "<tt:Name>VideoSource</tt:Name><tt:UseCount>1</tt:UseCount>"
            "<tt:SourceToken>source0</tt:SourceToken>"
            "</tt:VideoSourceConfiguration>"
            "<tt:VideoEncoderConfiguration token=\"video_encoder0\">"
            "<tt:Name>H264</tt:Name><tt:UseCount>1</tt:UseCount>"
            "<tt:Encoding>H264</tt:Encoding>"
            "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
            "<tt:Quality>5</tt:Quality>"
            "</tt:VideoEncoderConfiguration>"
            "<tt:PTZConfiguration token=\"ptz0\">"
            "<tt:Name>PTZ</tt:Name><tt:UseCount>1</tt:UseCount><tt:NodeToken>ptz_node0</tt:NodeToken>"
            "</tt:PTZConfiguration>"
            "</trt:Profiles>"
            "</trt:GetProfilesResponse>");
    }

    SwHttpResponse handleGetStreamUri() const {
        return soapResponse(
            SwString("<trt:GetStreamUriResponse><trt:MediaUri>")
            + "<tt:Uri>" + xmlEscape(rtspStreamUrl(m_config)) + "</tt:Uri>"
            + "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
            + "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
            + "<tt:Timeout>PT60S</tt:Timeout>"
            + "</trt:MediaUri></trt:GetStreamUriResponse>");
    }

    SwHttpResponse handleContinuousMove(const SwHttpRequest& request) {
        const std::string xml = request.body.toStdString();
        const double pan = parseTagAttribute(xml, "PanTilt", "x", 0.0);
        const double tilt = parseTagAttribute(xml, "PanTilt", "y", 0.0);
        const double zoom = parseTagAttribute(xml, "Zoom", "x", 0.0);
        const int timeoutMs = parseTagDurationMs(xml, "Timeout", m_config.ptzDefaultTimeoutMs);
        continuousMoveRequested((std::max)(-1.0, (std::min)(1.0, pan)),
                                (std::max)(-1.0, (std::min)(1.0, tilt)),
                                (std::max)(-1.0, (std::min)(1.0, zoom)),
                                clampInt(timeoutMs, m_config.ptzRepeatMs, 60000));
        return soapResponse("<tptz:ContinuousMoveResponse/>");
    }

    SwHttpResponse handleGetStatus() const {
        return soapResponse(
            "<tptz:GetStatusResponse>"
            "<tptz:PTZStatus>"
            "<tt:Position><tt:PanTilt x=\"0\" y=\"0\"/><tt:Zoom x=\"0\"/></tt:Position>"
            "<tt:MoveStatus><tt:PanTilt>IDLE</tt:PanTilt><tt:Zoom>IDLE</tt:Zoom></tt:MoveStatus>"
            "</tptz:PTZStatus>"
            "</tptz:GetStatusResponse>");
    }

    static SwString serviceXml(const SwString& ns, const SwString& xaddr) {
        return SwString("<tds:Service><tds:Namespace>") + ns +
               "</tds:Namespace><tds:XAddr>" + xaddr +
               "</tds:XAddr><tds:Version><tt:Major>2</tt:Major><tt:Minor>0</tt:Minor></tds:Version></tds:Service>";
    }

    GatewayConfig m_config;
    SwHttpServer m_server;
};

GatewayConfig parseConfig(SwCoreApplication& app) {
    GatewayConfig config;
    config.bindAddress = app.getArgument("bind", config.bindAddress);
    config.publicHost = app.getArgument("public_host", config.publicHost);
    config.rtspPort = static_cast<uint16_t>(clampInt(app.getArgument("rtsp_port", "8554").toInt(), 1, 65535));
    config.onvifPort = static_cast<uint16_t>(clampInt(app.getArgument("onvif_port", "8899").toInt(), 1, 65535));
    config.ptzUdpEnabled = app.getArgument("ptz_udp_enabled", "1").toInt() != 0;
    config.ptzUdpPort = static_cast<uint16_t>(clampInt(app.getArgument("ptz_udp_port", "8900").toInt(), 1, 65535));
    config.oscHost = app.getArgument("osc_host", config.oscHost);
    config.oscPort = static_cast<uint16_t>(clampInt(app.getArgument("osc_port", "16284").toInt(), 1, 65535));
    config.oscDeviceIndex = (std::max)(0, app.getArgument("osc_device", "0").toInt());
    config.oscIndexedArgs = app.getArgument("osc_indexed", "1").toInt() != 0;
    config.oscEnabled = app.getArgument("osc_enabled", "1").toInt() != 0;
    config.syntheticVideo = app.getArgument("synthetic_video", "1").toInt() != 0;
    config.syntheticFps = clampInt(app.getArgument("synthetic_fps", "10").toInt(), 1, 30);
    config.streamCodec = app.getArgument("stream_codec", config.streamCodec);
    config.streamBitrateKbps = clampInt(app.getArgument("stream_bitrate_kbps", "500").toInt(), 64, 50000);
    config.defaultSpeed = clampInt(app.getArgument("speed", "55").toInt(), 1, 100);
    config.ptzRepeatMs = clampInt(app.getArgument("ptz_repeat_ms", "10").toInt(), 1, 1000);
    config.ptzDefaultTimeoutMs = clampInt(app.getArgument("ptz_default_timeout_ms", "150").toInt(),
                                          config.ptzRepeatMs,
                                          60000);
    config.ptzInvertPan = app.getArgument("ptz_invert_pan", "0").toInt() != 0;
    config.ptzInvertTilt = app.getArgument("ptz_invert_tilt", "0").toInt() != 0;
    config.ptzJsonSpeedScale = clampDouble(app.getArgument("ptz_json_speed_scale", "1.0").toDouble(), 0.01, 1.0);
    config.ptzAxisSnapRatio = clampDouble(app.getArgument("ptz_axis_snap_ratio", "0.25").toDouble(), 0.0, 1.0);
    config.cameraVideo = app.getArgument("camera_video", "1").toInt() != 0;
    config.cameraDeviceIndex = (std::max)(0, app.getArgument("camera_device", "0").toInt());
    config.cameraDeviceName = app.getArgument("camera_device_name", config.cameraDeviceName);
    config.cameraMaxWidth = clampInt(app.getArgument("camera_max_width", "1920").toInt(), 0, 7680);
    config.cameraMaxHeight = clampInt(app.getArgument("camera_max_height", "1080").toInt(), 0, 4320);
    config.ffmpegFallback = app.getArgument("ffmpeg_fallback", "1").toInt() != 0;
    config.ffmpegPath = app.getArgument("ffmpeg_path", config.ffmpegPath);
    config.ffmpegDeviceName = app.getArgument("ffmpeg_device_name", config.ffmpegDeviceName);
    config.ffmpegWidth = clampInt(app.getArgument("ffmpeg_width", "720").toInt(), 1, 7680);
    config.ffmpegHeight = clampInt(app.getArgument("ffmpeg_height", "1280").toInt(), 1, 4320);
    config.ffmpegFps = clampInt(app.getArgument("ffmpeg_fps", "60").toInt(), 1, 120);
    config.ffmpegInputCodec = app.getArgument("ffmpeg_input_codec", config.ffmpegInputCodec);
    config.ffmpegPixelFormat = app.getArgument("ffmpeg_pixel_format", config.ffmpegPixelFormat);
    config.ffmpegEncoder = app.getArgument("ffmpeg_encoder", config.ffmpegEncoder);
    config.startObsbotCenter = app.getArgument("start_obsbot_center", "0").toInt() != 0;
    config.obsbotCenterPath = app.getArgument("obsbot_center_path", config.obsbotCenterPath);
    config.directShowPtz = app.getArgument("directshow_ptz", "1").toInt() != 0;
    config.directShowPan = app.getArgument("directshow_pan", "1").toInt() != 0;
    config.directShowTilt = app.getArgument("directshow_tilt", "1").toInt() != 0;
    config.directShowZoom = app.getArgument("directshow_zoom", "1").toInt() != 0;
    config.directShowPtzName = app.getArgument("directshow_ptz_name", config.directShowPtzName);
    config.directShowTiltProperty = app.getArgument("directshow_tilt_property", config.directShowTiltProperty);
    config.directShowDumpControls = app.getArgument("directshow_dump_controls", "0").toInt() != 0;
    config.directShowReleaseAfterCommand = app.getArgument("directshow_release_after_command", "1").toInt() != 0;
    config.directShowPtzSpeedScale = clampDouble(app.getArgument("directshow_ptz_speed_scale", "0.35").toDouble(),
                                                 0.05,
                                                 2.0);
    return config;
}

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);
    const GatewayConfig config = parseConfig(app);
    SwObject signalContext;

    ObsbotOscPtzClient ptz(config, &signalContext);
    OnvifGatewayService onvif(config, &signalContext);
    UdpPtzGatewayService udpPtz(config, &signalContext);
    RtspGatewayService rtsp(config, &signalContext);
    SyntheticRtspPublisher publisher(config.syntheticFps, &signalContext);
    NativeH264CameraPublisher cameraPublisher(config, &signalContext);
    FfmpegH264CameraPublisher ffmpegPublisher(config, &signalContext);

    SwObject::connect(&onvif,
                      &OnvifGatewayService::continuousMoveRequested,
                      &ptz,
                      &ObsbotOscPtzClient::continuousMove);
    SwObject::connect(&onvif,
                      &OnvifGatewayService::stopRequested,
                      &ptz,
                      &ObsbotOscPtzClient::stop);
    SwObject::connect(&onvif,
                      &OnvifGatewayService::gotoHomeRequested,
                      &ptz,
                      &ObsbotOscPtzClient::gotoHome);
    SwObject::connect(&udpPtz,
                      &UdpPtzGatewayService::continuousMoveRequested,
                      &ptz,
                      &ObsbotOscPtzClient::continuousMove);
    SwObject::connect(&udpPtz,
                      &UdpPtzGatewayService::stopRequested,
                      &ptz,
                      &ObsbotOscPtzClient::stop);
    SwObject::connect(&udpPtz,
                      &UdpPtzGatewayService::gotoHomeRequested,
                      &ptz,
                      &ObsbotOscPtzClient::gotoHome);
    SwObject::connect(&publisher,
                      &SyntheticRtspPublisher::videoPacketReady,
                      &rtsp,
                      &RtspGatewayService::publishPacket);
    SwObject::connect(&cameraPublisher,
                      &NativeH264CameraPublisher::videoPacketReady,
                      &rtsp,
                      &RtspGatewayService::publishPacket);
    SwObject::connect(&ffmpegPublisher,
                      &FfmpegH264CameraPublisher::videoPacketReady,
                      &rtsp,
                      &RtspGatewayService::publishPacket);

    SwObject::connect(&ptz, &ObsbotOscPtzClient::commandSent, &signalContext, [](const SwString& command) {
        std::cerr << "[ObsbotOnvifRtspGateway] PTZ " << command.toStdString() << std::endl;
    });
    SwObject::connect(&ptz, &ObsbotOscPtzClient::errorOccurred, &signalContext, [](const SwString& message) {
        std::cerr << "[ObsbotOnvifRtspGateway] " << message.toStdString() << std::endl;
    });
    SwObject::connect(&onvif, &OnvifGatewayService::started, &signalContext, [](const SwString& url) {
        std::cerr << "[ObsbotOnvifRtspGateway] ONVIF " << url.toStdString() << std::endl;
    });
    SwObject::connect(&rtsp, &RtspGatewayService::started, &signalContext, [](const SwString& url) {
        std::cerr << "[ObsbotOnvifRtspGateway] RTSP " << url.toStdString() << std::endl;
    });
    SwObject::connect(&udpPtz, &UdpPtzGatewayService::started, &signalContext, [](const SwString& url) {
        std::cerr << "[ObsbotOnvifRtspGateway] UDP PTZ " << url.toStdString() << std::endl;
    });
    SwObject::connect(&udpPtz, &UdpPtzGatewayService::commandReceived, &signalContext, [](const SwString& command) {
        std::cerr << "[ObsbotOnvifRtspGateway] PTZ " << command.toStdString() << std::endl;
    });
    SwObject::connect(&onvif, &OnvifGatewayService::errorOccurred, &signalContext, [](const SwString& message) {
        std::cerr << "[ObsbotOnvifRtspGateway] " << message.toStdString() << std::endl;
    });
    SwObject::connect(&udpPtz, &UdpPtzGatewayService::errorOccurred, &signalContext, [](const SwString& message) {
        std::cerr << "[ObsbotOnvifRtspGateway] " << message.toStdString() << std::endl;
    });
    SwObject::connect(&rtsp, &RtspGatewayService::errorOccurred, &signalContext, [](const SwString& message) {
        std::cerr << "[ObsbotOnvifRtspGateway] " << message.toStdString() << std::endl;
    });
    SwObject::connect(&cameraPublisher, &NativeH264CameraPublisher::started, &signalContext, [](const SwString& message) {
        std::cerr << "[ObsbotOnvifRtspGateway] " << message.toStdString() << std::endl;
    });
    SwObject::connect(&cameraPublisher, &NativeH264CameraPublisher::errorOccurred, &signalContext, [](const SwString& message) {
        std::cerr << "[ObsbotOnvifRtspGateway] " << message.toStdString() << std::endl;
    });
    SwObject::connect(&ffmpegPublisher, &FfmpegH264CameraPublisher::started, &signalContext, [](const SwString& message) {
        std::cerr << "[ObsbotOnvifRtspGateway] " << message.toStdString() << std::endl;
    });
    SwObject::connect(&ffmpegPublisher, &FfmpegH264CameraPublisher::errorOccurred, &signalContext, [](const SwString& message) {
        std::cerr << "[ObsbotOnvifRtspGateway] " << message.toStdString() << std::endl;
    });

    if (!rtsp.start()) {
        return EXIT_FAILURE;
    }
    if (!onvif.start()) {
        rtsp.stop();
        return EXIT_FAILURE;
    }
    if (!udpPtz.start()) {
        rtsp.stop();
        return EXIT_FAILURE;
    }

    const SwVideoPacket::Codec requestedCodec = streamPacketCodec(config);
    bool videoStarted = requestedCodec == SwVideoPacket::Codec::H264 && config.cameraVideo && cameraPublisher.start();
    if (!videoStarted && config.ffmpegFallback) {
        videoStarted = ffmpegPublisher.start();
    }
    if (!videoStarted && requestedCodec == SwVideoPacket::Codec::H264 && config.syntheticVideo) {
        publisher.start();
        std::cerr << "[ObsbotOnvifRtspGateway] synthetic H264 publisher enabled" << std::endl;
    } else if (!videoStarted && config.syntheticVideo) {
        std::cerr << "[ObsbotOnvifRtspGateway] synthetic publisher skipped for "
                  << streamCodecLabel(config).toStdString() << std::endl;
    }

    SwString obsbotCenterError;
    if (startObsbotCenterIfRequested(config, &obsbotCenterError)) {
        if (config.startObsbotCenter) {
            std::cerr << "[ObsbotOnvifRtspGateway] OBSBOT Center start requested: "
                      << config.obsbotCenterPath.toStdString() << std::endl;
        }
    } else {
        std::cerr << "[ObsbotOnvifRtspGateway] " << obsbotCenterError.toStdString() << std::endl;
    }

    std::cerr << "[ObsbotOnvifRtspGateway] OSC target "
              << config.oscHost.toStdString() << ":" << config.oscPort
              << " indexed=" << (config.oscIndexedArgs ? "yes" : "no")
              << " device=" << config.oscDeviceIndex
              << " enabled=" << (config.oscEnabled ? "yes" : "no") << std::endl;
    std::cerr << "[ObsbotOnvifRtspGateway] DirectShow PTZ "
              << (config.directShowPtz ? "enabled" : "disabled")
              << " device_name=\"" << config.directShowPtzName.toStdString() << "\""
              << " pan=" << (config.directShowPan ? "yes" : "no")
              << " tilt=" << (config.directShowTilt ? "yes" : "no")
              << " zoom=" << (config.directShowZoom ? "yes" : "no")
              << " repeat=" << config.ptzRepeatMs << "ms"
              << " default_timeout=" << config.ptzDefaultTimeoutMs << "ms"
              << " tilt_property=" << config.directShowTiltProperty.toStdString()
              << " release_after_command=" << (config.directShowReleaseAfterCommand ? "yes" : "no")
              << " speed_scale=" << config.directShowPtzSpeedScale << std::endl;
    std::cerr << "[ObsbotOnvifRtspGateway] UDP PTZ "
              << (config.ptzUdpEnabled ? "enabled" : "disabled")
              << " port=" << config.ptzUdpPort
              << " json_speed_scale=" << config.ptzJsonSpeedScale
              << " invert_pan=" << (config.ptzInvertPan ? "yes" : "no")
              << " invert_tilt=" << (config.ptzInvertTilt ? "yes" : "no")
              << " axis_snap_ratio=" << config.ptzAxisSnapRatio
              << " protocol=\"VPTZ1 <seq> MOVE <pan> <tilt> <zoom> <timeoutMs>\"" << std::endl;
    std::cerr << "[ObsbotOnvifRtspGateway] FFmpeg fallback "
              << (config.ffmpegFallback ? "enabled" : "disabled")
              << " device_name=\"" << config.ffmpegDeviceName.toStdString() << "\" "
              << config.ffmpegWidth << "x" << config.ffmpegHeight
              << "@" << config.ffmpegFps
              << " stream_codec=" << streamCodecLabel(config).toStdString()
              << " bitrate=" << config.streamBitrateKbps << "kbps"
              << " input_codec=" << (config.ffmpegInputCodec.isEmpty() ? "(auto)" : config.ffmpegInputCodec.toStdString())
              << " pixel_format=" << (config.ffmpegPixelFormat.isEmpty() ? "(auto)" : config.ffmpegPixelFormat.toStdString())
              << " encoder=" << config.ffmpegEncoder.toStdString() << std::endl;
    std::cerr << "[ObsbotOnvifRtspGateway] arguments: "
              << "--bind=" << config.bindAddress.toStdString()
              << " --public_host=" << config.publicHost.toStdString()
              << " --rtsp_port=" << config.rtspPort
              << " --onvif_port=" << config.onvifPort
              << " --ptz_udp_enabled=" << (config.ptzUdpEnabled ? 1 : 0)
              << " --ptz_udp_port=" << config.ptzUdpPort
              << " --osc_host=" << config.oscHost.toStdString()
              << " --osc_port=" << config.oscPort
              << " --osc_indexed=" << (config.oscIndexedArgs ? 1 : 0)
              << " --osc_enabled=" << (config.oscEnabled ? 1 : 0)
              << " --stream_codec=" << config.streamCodec.toStdString()
              << " --stream_bitrate_kbps=" << config.streamBitrateKbps
              << " --ptz_repeat_ms=" << config.ptzRepeatMs
              << " --ptz_default_timeout_ms=" << config.ptzDefaultTimeoutMs
              << " --ptz_invert_pan=" << (config.ptzInvertPan ? 1 : 0)
              << " --ptz_invert_tilt=" << (config.ptzInvertTilt ? 1 : 0)
              << " --ptz_json_speed_scale=" << config.ptzJsonSpeedScale
              << " --ptz_axis_snap_ratio=" << config.ptzAxisSnapRatio
              << " --camera_video=" << (config.cameraVideo ? 1 : 0)
              << " --camera_device=" << config.cameraDeviceIndex
              << " --camera_device_name=" << config.cameraDeviceName.toStdString()
              << " --camera_max_width=" << config.cameraMaxWidth
              << " --camera_max_height=" << config.cameraMaxHeight
              << " --ffmpeg_fallback=" << (config.ffmpegFallback ? 1 : 0)
              << " --ffmpeg_path=" << config.ffmpegPath.toStdString()
              << " --ffmpeg_device_name=" << config.ffmpegDeviceName.toStdString()
              << " --ffmpeg_width=" << config.ffmpegWidth
              << " --ffmpeg_height=" << config.ffmpegHeight
              << " --ffmpeg_fps=" << config.ffmpegFps
              << " --ffmpeg_input_codec=" << config.ffmpegInputCodec.toStdString()
              << " --ffmpeg_pixel_format=" << config.ffmpegPixelFormat.toStdString()
              << " --ffmpeg_encoder=" << config.ffmpegEncoder.toStdString()
              << " --synthetic_video=" << (config.syntheticVideo ? 1 : 0)
              << " --start_obsbot_center=" << (config.startObsbotCenter ? 1 : 0)
              << " --directshow_ptz=" << (config.directShowPtz ? 1 : 0)
              << " --directshow_pan=" << (config.directShowPan ? 1 : 0)
              << " --directshow_tilt=" << (config.directShowTilt ? 1 : 0)
              << " --directshow_zoom=" << (config.directShowZoom ? 1 : 0)
              << " --directshow_ptz_name=" << config.directShowPtzName.toStdString()
              << " --directshow_tilt_property=" << config.directShowTiltProperty.toStdString()
              << " --directshow_dump_controls=" << (config.directShowDumpControls ? 1 : 0)
              << " --directshow_release_after_command=" << (config.directShowReleaseAfterCommand ? 1 : 0)
              << " --directshow_ptz_speed_scale=" << config.directShowPtzSpeedScale
              << std::endl;

    return app.exec();
}
