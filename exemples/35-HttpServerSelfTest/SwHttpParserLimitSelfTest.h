#pragma once

#include "SwDebug.h"
#include "http/SwHttpParser.h"

#include <string>

static bool swHttpParserExpectLimitError_(const SwString& name,
                                          const SwHttpLimits& limits,
                                          const SwByteArray& input,
                                          int expectedStatus) {
    SwHttpParser parser;
    parser.setLimits(limits);

    SwList<SwHttpRequest> requests;
    const SwHttpParser::FeedStatus status = parser.feed(input, requests);
    if (status != SwHttpParser::FeedStatus::Error ||
        parser.errorStatus() != expectedStatus ||
        !requests.isEmpty()) {
        swError() << "[SwHttpParserLimitSelfTest] FAIL " << name
                  << " feedStatus=" << static_cast<int>(status)
                  << " parserStatus=" << parser.errorStatus()
                  << " message=" << parser.errorMessage()
                  << " parsedRequests=" << static_cast<int>(requests.size());
        return false;
    }
    return true;
}

static bool runSwHttpParserLimitSelfTest() {
    SwHttpLimits headerLimits;
    headerLimits.maxHeaderBytes = 48;
    SwByteArray partialHeader("GET / HTTP/1.1\r\nHost: ");
    partialHeader.append(std::string(64, 'a'));
    if (!swHttpParserExpectLimitError_("partial header line", headerLimits, partialHeader, 431)) {
        return false;
    }

    SwHttpLimits chunkLimits;
    chunkLimits.maxRequestLineBytes = 32;
    chunkLimits.maxHeaderBytes = 1024;
    SwByteArray partialChunkSize("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n");
    partialChunkSize.append(std::string(40, 'f'));
    if (!swHttpParserExpectLimitError_("partial chunk size line", chunkLimits, partialChunkSize, 400)) {
        return false;
    }

    SwHttpLimits trailerLimits;
    trailerLimits.maxHeaderBytes = 80;
    SwByteArray partialTrailer("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-Trailer: ");
    partialTrailer.append(std::string(80, 'b'));
    if (!swHttpParserExpectLimitError_("partial trailer line", trailerLimits, partialTrailer, 431)) {
        return false;
    }

    swDebug() << "[SwHttpParserLimitSelfTest] PASS";
    return true;
}
