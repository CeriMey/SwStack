#pragma once

#include "SwByteArray.h"
#include "SwList.h"
#include "SwMutex.h"
#include "SwRemoteObject.h"
#include "SwTcpServer.h"

class SwMediaFoundationVideoSource;
class SwHttpMjpegSource;
class SwTcpSocket;
class SwTimer;
class SwVideoPacket;

class PnServerManagerNode : public SwRemoteObject {
public:
    PnServerManagerNode(const SwString& sysName,
                        const SwString& nameSpace,
                        const SwString& objectName,
                        SwObject* parent = nullptr);
    ~PnServerManagerNode() override;

private:
    void sanitizeConfig_();
    void restartVideoSource_();
    void startWebcamSource_(int webcamIndex);
    void startHttpMjpegSource_(const SwString& url);
    void restartHttpServer_();
    void onVideoPacket_(const SwVideoPacket& packet);

    void onNewTcpConnection_();
    void onClientReadyRead_(SwTcpSocket* socket);
    void handleHttpRequest_(SwTcpSocket* socket, const SwString& method, const SwString& path);
    void beginStream_(SwTcpSocket* socket);
    void pumpStream_(SwTcpSocket* socket);

    void writeTextResponse_(SwTcpSocket* socket, int status, const SwString& reason, const SwString& body);
    void writeBinaryResponse_(SwTcpSocket* socket,
                              int status,
                              const SwString& reason,
                              const SwString& contentType,
                              const SwByteArray& body);
    void closeClient_(SwTcpSocket* socket);

    static int clampInt_(int value, int minValue, int maxValue);
    static SwString statusReason_(int status);
    static SwString extractRequestMethod_(const SwByteArray& request);
    static SwString extractRequestPath_(const SwByteArray& request);
    static bool isDynamicSourcePath_(const SwString& path);
    static bool isWebcamUrl_(const SwString& url);
    static int parseWebcamIndexFromUrl_(const SwString& url, int fallbackIndex);
    bool encodeBgraToJpeg_(const SwByteArray& bgra, int width, int height, SwByteArray& outJpeg) const;

    struct StreamClientState {
        SwTcpSocket* socket{nullptr};
        SwByteArray requestBuffer;
        bool requestHandled{false};
        SwTimer* pumpTimer{nullptr};
        int lastSentSeq{0};
    };

    StreamClientState* findClient_(SwTcpSocket* socket);
    void removeClient_(SwTcpSocket* socket, bool deleteSocket);

    int httpPort_{8090};
    int deviceIndex_{0};
    int streamFps_{20};
    int jpegQuality_{80};
    SwString videoUrl_{SwString("webcam://0")};

    SwTcpServer* tcpServer_{nullptr};
    bool httpListening_{false};
    SwMediaFoundationVideoSource* webcamSource_{nullptr};
    SwHttpMjpegSource* mjpegSource_{nullptr};
    bool cameraReady_{false};
    SwList<StreamClientState*> clients_;

    mutable SwMutex frameMutex_;
    int latestSeq_{0};
    int latestWidth_{0};
    int latestHeight_{0};
    SwByteArray latestJpeg_;
};
