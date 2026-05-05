#pragma once
#include <cstdint>
#include <string>
#include <chrono>

struct EncoderConfig {
    int    width        = 0;
    int    height       = 0;
    int    fps          = 30;
    int    bitrateKbps  = 10000;   // default 10 Mbps
    std::string outputPath;        // e.g. "output.mkv"
    std::string codecName = "";    // "" = auto pick H.264; "libx264" for software
    std::string preset   = "medium";

    // For future hardware encoders (NVENC/QSV/AMF)
    int    hwDeviceType  = 0;      // 0 = software; future: 1=NVENC,2=QSV,3=AMF
    bool   useNativeFmt  = false;  // GPU-native format, bypass sws_scale
};

struct EncoderStats {
    int     framesInput  = 0;
    int     framesOutput = 0;
    int64_t totalBytes   = 0;
    double  actualFps    = 0.0;
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    virtual bool Initialize(const EncoderConfig& config) = 0;

    /// Encode one RGBA/BGRA 8-bit frame (top-down).
    /// rgbaData = pointer to first pixel, stride = bytes per row (may include padding).
    virtual bool EncodeFrame(const void* rgbaData, int stride,
                             bool requestKeyframe = false) = 0;

    /// Flush encoder, finalise output file, release resources.
    virtual bool Finalize() = 0;

    virtual EncoderStats GetStats() const = 0;
    virtual bool         IsInitialized() const = 0;
};
