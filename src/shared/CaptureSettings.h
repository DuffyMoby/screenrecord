#pragma once
#include <d3d11.h>

#include <cstdint>


struct FrameFormat {
    uint32_t width;
    uint32_t height;
    DXGI_FORMAT format;
};

struct FrameRate {
    uint32_t numerator;
    uint32_t denominator;
};

struct CaptureSettings {
    FrameRate frame_rate;
    FrameFormat frame_settings;
    uint8_t monitor_index;
    bool capture_cursor;
};
