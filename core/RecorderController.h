#pragma once
#include "RecordingState.h"
#include "RecordingConfig.h"
#include "../platform/D3D11Device.h"
#include "../platform/ErrorCode.h"
#include "../capture/MonitorEnumerator.h"
#include "../capture/GameCaptureSource.h"
#include "../encoder/FFmpegEncoder.h"
#include "../audio/AudioCaptureWasapi.h"
#include "../audio/AudioMixer.h"
#include "../audio/AudioEncoder.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <vector>

class RecorderController {
public:
    RecorderController();
    ~RecorderController();

    RecorderController(const RecorderController&) = delete;
    RecorderController& operator=(const RecorderController&) = delete;

    bool Initialize();
    void Shutdown();

    bool StartRecording(const RecordingConfig& config);
    void StopRecording();
    void PauseRecording();
    void ResumeRecording();

    RecordingState GetState() const { return m_state.load(); }
    RecorderStatus GetStatus() const;
    ScrError      GetLastError() const { return m_lastError; }

    std::vector<MonitorInfo> GetMonitors() const;
    int GetMonitorCount() const { return m_monitorCount; }

    using StatusCallback = std::function<void(const RecorderStatus&)>;
    void SetStatusCallback(StatusCallback cb) { m_statusCb = std::move(cb); }

private:
    struct FrameStats {
        int capturedFrames    = 0;
        int encodedFrames     = 0;
        int droppedFrames     = 0;
        int duplicatedFrames  = 0;
        int blackFrames       = 0;
        int recoveryCount     = 0;
    };

    struct AudioAccum {
        std::vector<float> buf;
        void Append(const float* d, size_t n) { buf.insert(buf.end(), d, d + n); }
        void Consume(size_t n) { if (n >= buf.size()) buf.clear(); else buf.erase(buf.begin(), buf.begin() + n); }
        const float* Data() const { return buf.data(); }
        size_t Size() const { return buf.size(); }
        bool Empty() const { return buf.empty(); }
    };

    void RecordingThread();
    void RecordingThreadImpl();
    void WriteAudioPackets(AudioEncoder& enc, int streamIdx, FFmpegEncoder& mux);

    mutable std::mutex m_statusMtx;
    RecorderStatus     m_status;
    StatusCallback     m_statusCb;

    std::atomic<RecordingState> m_state{RecordingState::Idle};
    std::thread m_recordingThread;
    RecordingConfig m_config;
    ScrError m_lastError = ScrError::Ok;

    D3D11Device m_d3d;
    int  m_monitorCount  = 0;

    // PERF logging
    std::chrono::steady_clock::time_point m_threadStart;
};
