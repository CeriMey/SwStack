#pragma once
#include "SwString.h"
#include "SwMap.h"
#include "SwList.h"

struct HttpRequest {
    SwString method;
    SwString path;
    SwString protocol;
    SwMap<SwString, SwString> headers;
    SwString body;
    bool keepAlive = true;
};

struct HttpResponse {
    int status = 200;
    SwString reason = SwString("OK");
    SwString contentType = SwString("text/plain");
    SwString body;
    SwMap<SwString, SwString> headers;
};
