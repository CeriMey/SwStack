#pragma once

#include <cstdint>

struct IpcRingBufferFrameMeta {
    static constexpr const char* kTypeName = "sw::ipc::IpcRingBufferFrameMetaV1";

    uint32_t width{0};
    uint32_t height{0};
    uint32_t pixelFormat{0}; // SwVideoPixelFormat
    int32_t stride{0};
    int64_t pts{0};
};

