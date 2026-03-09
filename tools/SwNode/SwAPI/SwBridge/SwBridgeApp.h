#pragma once

#include "SwObject.h"
#include "SwString.h"

class SwBridgeHttpServer;

class SwBridgeApp : public SwObject {
public:
    SwBridgeApp(int argc, char** argv, SwObject* parent = nullptr);
    ~SwBridgeApp() override;

private:
    static uint16_t parsePort_(int argc, char** argv);

    SwBridgeHttpServer* server_{nullptr};
};
