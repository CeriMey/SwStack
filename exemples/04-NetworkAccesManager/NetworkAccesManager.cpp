#include "SwCoreApplication.h"
#include "SwNetworkAccessManager.h"

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <functional>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

static void ensureDirectory(const std::string& path) {
#if defined(_WIN32)
    _mkdir(path.c_str()); // ignore errors if exists
#else
    mkdir(path.c_str(), 0755);
#endif
}

static std::string environmentValue(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value || !*value) {
        std::free(value);
        return std::string();
    }

    std::string result(value, length > 0 ? (length - 1) : 0);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (!value || !value[0]) {
        return std::string();
    }
    return std::string(value);
#endif
}

static std::string mapboxTerrainUrlTemplate() {
    const std::string token = environmentValue("MAPBOX_ACCESS_TOKEN");
    if (token.empty()) {
        return std::string();
    }

    return std::string("https://api.mapbox.com/v4/mapbox.terrain-rgb/%1/%2/%3.pngraw?access_token=") + token;
}

#if defined(_WIN32)
static bool downloadWithWinHttp(const std::string& url, const std::string& headersPath, const std::string& bodyPath) {
    std::cout << "[WinHTTP] fallback GET " << url << std::endl;
    std::wstring wurl(url.begin(), url.end());

    URL_COMPONENTS comps{};
    comps.dwStructSize = sizeof(comps);
    std::wstring host(256, L'\0');
    std::wstring path(1024, L'\0');
    comps.lpszHostName = host.empty() ? nullptr : &host[0];
    comps.dwHostNameLength = (DWORD)host.size();
    comps.lpszUrlPath = path.empty() ? nullptr : &path[0];
    comps.dwUrlPathLength = (DWORD)path.size();
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &comps)) {
        return false;
    }
    host.resize(comps.dwHostNameLength);
    path.resize(comps.dwUrlPathLength);
    bool https = (comps.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"SwNetworkAccessManagerFallback/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), comps.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) {
        ok = WinHttpReceiveResponse(hRequest, nullptr);
    }

    std::string headers;
    if (ok) {
        DWORD size = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
        std::wstring rawHeaders;
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 0) {
            rawHeaders.resize(size / sizeof(wchar_t));
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                    rawHeaders.empty() ? nullptr : &rawHeaders[0], &size, WINHTTP_NO_HEADER_INDEX)) {
                headers = SwString::fromWCharArray(rawHeaders.c_str()).toStdString();
            }
        }
    }

    std::ofstream bodyOut(bodyPath, std::ios::binary | std::ios::trunc);
    if (!bodyOut || !ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    size_t totalBytes = 0;
    for (;;) {
        DWORD downloaded = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &downloaded) || downloaded == 0) {
            break;
        }
        std::vector<char> buffer(downloaded);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), downloaded, &read) || read == 0) {
            break;
        }
        bodyOut.write(buffer.data(), (std::streamsize)read);
        totalBytes += read;
    }

    bodyOut.close();
    if (!headers.empty()) {
        std::ofstream hdrOut(headersPath, std::ios::trunc);
        if (hdrOut) {
            hdrOut << headers;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    std::cout << "[WinHTTP] saved to " << bodyPath << " (" << totalBytes << " bytes)" << std::endl;
    return true;
}
#endif

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    SwNetworkAccessManager nam;
    nam.setRawHeader("User-Agent", "SwNetworkAccessManager/1.0 (example4)");
    nam.setRawHeader("Accept", "image/png,image/*;q=0.8");

    // Liste des fournisseurs et URL modèles (z/x/y à 1/1/1 pour la première tuile)
    std::vector<std::pair<std::string, std::string>> sources = {
        {"voyage", "https://cartodb-basemaps-a.global.ssl.fastly.net/rastertiles/voyager/{z}/{x}/{y}.png"},
        {"satellite", "https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"},
        {"worldstreet", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}"},
        {"worldtopo", "https://services.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}"},
        {"worldterrain", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Terrain_Base/MapServer/tile/{z}/{y}/{x}"},
        {"oceanworld", "https://server.arcgisonline.com/ArcGIS/rest/services/Ocean/World_Ocean_Base/MapServer/tile/{z}/{y}/{x}"},
        {"physicalworld", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Physical_Map/MapServer/tile/{z}/{y}/{x}"},
        {"osm", "https://tile.openstreetmap.org/{z}/{x}/{y}.png"}
    };

    const std::string mapboxTerrain = mapboxTerrainUrlTemplate();
    if (!mapboxTerrain.empty()) {
        sources.insert(sources.begin(), std::make_pair(std::string("elevation"), mapboxTerrain));
    } else {
        std::cout << "MAPBOX_ACCESS_TOKEN not set, skipping elevation source." << std::endl;
    }

    const int z = 1, x = 1, y = 1;
    const std::string outDir = "example4_tiles";
    ensureDirectory(outDir);

    auto buildUrl = [&](const std::string& tmpl) -> SwString {
        std::string url = tmpl;
        auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.length(), to);
                pos += to.length();
            }
        };
        replaceAll(url, "{z}", std::to_string(z));
        replaceAll(url, "{x}", std::to_string(x));
        replaceAll(url, "{y}", std::to_string(y));
        replaceAll(url, "%1", std::to_string(z));
        replaceAll(url, "%2", std::to_string(x));
        replaceAll(url, "%3", std::to_string(y));
        return SwString(url.c_str());
    };

    size_t index = 0;
    std::atomic<size_t> activeIndex{static_cast<size_t>(-1)};

    std::function<void(size_t)> startDownload;

    startDownload = [&](size_t idx) {
        SwString url = buildUrl(sources[idx].second);
        std::cout << "Téléchargement " << (idx + 1) << "/" << sources.size() << ": "
                  << sources[idx].first << " -> " << url.toStdString() << std::endl;
        activeIndex.store(idx);
        nam.get(url);
        std::thread([&, idx]() {
            std::this_thread::sleep_for(std::chrono::seconds(15));
            if (activeIndex.load() == idx) {
                std::cerr << "Timeout pour " << sources[idx].first << ", on passe au suivant." << std::endl;
                nam.abort();
                ++index;
                if (index < sources.size()) {
                    startDownload(index);
                } else {
                    SwCoreApplication::instance()->quit();
                }
            }
        }).detach();
    };

    SwObject::connect(&nam, SIGNAL(finished), [&](const SwByteArray& result) {
        const auto& source = sources[index];
        const std::string baseName = source.first;
        const std::string headersPath = outDir + "/" + baseName + "_headers.txt";
        const std::string bodyPath = outDir + "/" + baseName + ".png";

        SwString headers = nam.responseHeaders();
        SwByteArray bodyBytes = result;

        if (headers.isEmpty()) {
            int sep = result.indexOf("\r\n\r\n");
            if (sep >= 0) {
                headers = SwString(result.left(sep));
                SwByteArray remainder = result.mid(sep + 4);
                if (!remainder.isEmpty()) {
                    bodyBytes = remainder;
                }
            }
        }

        {
            std::ofstream headersOut(headersPath, std::ios::trunc);
            if (headersOut) {
                headersOut << headers.toStdString();
                std::cout << "En-têtes sauvegardés dans " << headersPath << std::endl;
            } else {
                std::cerr << "Impossible d'écrire " << headersPath << std::endl;
            }
        }

        {
            std::ofstream bodyOut(bodyPath, std::ios::binary | std::ios::trunc);
            if (bodyOut) {
                if (!bodyBytes.isEmpty()) {
                    bodyOut.write(bodyBytes.constData(), static_cast<std::streamsize>(bodyBytes.size()));
                }
                std::cout << "Tuile enregistrée dans " << bodyPath
                          << " (" << bodyBytes.size() << " octets)" << std::endl;
            } else {
                std::cerr << "Impossible d'écrire " << bodyPath << std::endl;
            }
        }

        activeIndex.store(static_cast<size_t>(-1));
        ++index;
        if (index < sources.size()) {
            startDownload(index);
        } else {
            SwCoreApplication::instance()->quit();
        }
    });

    SwObject::connect(&nam, SIGNAL(errorOccurred), [&](int err) {
        const auto& source = sources[index];
        const std::string baseName = source.first;
        const std::string headersPath = outDir + "/" + baseName + "_headers.txt";
        const std::string bodyPath = outDir + "/" + baseName + ".png";

        std::cerr << "Erreur réseau (" << source.first << "): " << err << std::endl;
#if defined(_WIN32)

#endif

        activeIndex.store(static_cast<size_t>(-1));
        ++index;
        if (index < sources.size()) {
            startDownload(index);
        } else {
            SwCoreApplication::instance()->quit();
        }
    });

    if (!sources.empty()) {
        startDownload(0);
    }

    return app.exec();
}
