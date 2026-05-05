#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

enum class CaptureMode { Monitor, Window };

struct RecordingConfig {
    CaptureMode captureMode    = CaptureMode::Monitor;
    int    sourceMonitor       = 0;          // valid when captureMode == Monitor
    HWND   targetWindow        = nullptr;    // valid when captureMode == Window
    bool   followWindow        = true;
    int    fps                 = 30;
    int    bitrateKbps         = 10000;
    bool   captureSysAudio     = true;
    bool   captureMic          = true;
    std::wstring outputPath   = L"captures/recording.mkv";
};

struct RecorderStatus {
    RecordingState state      = RecordingState::Idle;
    int64_t        durationMs = 0;
    int64_t        fileSize   = 0;
    int            frames     = 0;
};
