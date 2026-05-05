#include "RecorderController.h"
#include "../platform/Logger.h"
#include <format>
#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
}

RecorderController::RecorderController()  = default;
RecorderController::~RecorderController() { Shutdown(); }

bool RecorderController::Initialize() {
    if (m_d3d.Initialize() != ScrError::Ok) {
        Logger::Instance().Error("RC: D3D11 init failed");
        return false;
    }
    MonitorEnumerator monEnum;
    if (monEnum.Enumerate(m_d3d.GetAdapter()) == ScrError::Ok)
        m_monitorCount = (int)monEnum.GetCount();
    Logger::Instance().Info("RC: ready");
    return true;
}

void RecorderController::Shutdown() { StopRecording(); m_d3d.Shutdown(); }

std::vector<MonitorInfo> RecorderController::GetMonitors() const {
    std::vector<MonitorInfo> result;
    MonitorEnumerator monEnum;
    if (monEnum.Enumerate(m_d3d.GetAdapter()) != ScrError::Ok) return result;
    for (size_t i = 0; i < monEnum.GetCount(); ++i)
        result.push_back(monEnum.GetMonitor(i));
    return result;
}

// ============================================================
// State Machine
// ============================================================
bool RecorderController::StartRecording(const RecordingConfig& config) {
    RecordingState expected = RecordingState::Idle;
    if (!m_state.compare_exchange_strong(expected, RecordingState::Starting)) {
        expected = RecordingState::Error;
        if (!m_state.compare_exchange_strong(expected, RecordingState::Starting))
            return false;
    }
    m_lastError = ScrError::Ok;
    m_config = config;
    m_status = RecorderStatus{};
    m_recordingThread = std::thread([this]() { RecordingThread(); });
    return true;
}

void RecorderController::StopRecording() {
    RecordingState s = m_state.load();
    if (s == RecordingState::Idle || s == RecordingState::Error) return;
    m_state.store(RecordingState::Stopping);
    if (m_recordingThread.joinable()) m_recordingThread.join();
    if (m_statusCb) m_statusCb(m_status);
}

void RecorderController::PauseRecording() {
    RecordingState expected = RecordingState::Recording;
    m_state.compare_exchange_strong(expected, RecordingState::Paused);
}

void RecorderController::ResumeRecording() {
    RecordingState expected = RecordingState::Paused;
    m_state.compare_exchange_strong(expected, RecordingState::Recording);
}

RecorderStatus RecorderController::GetStatus() const {
    std::lock_guard lock(m_statusMtx);
    return m_status;
}

// ============================================================
// RecordingThread
// ============================================================
void RecorderController::RecordingThread() {
    Logger::Instance().Info("RT: thread started");
    m_threadStart = std::chrono::steady_clock::now();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_lastError = ScrError::Ok;

    // Declare all variables up here to avoid goto-skip issues
    GameCaptureSource capture;
    GameCaptureSource::Config capCfg;
    int outW = 0, outH = 0;
    AudioCaptureWasapi* sysAudio = nullptr;
    AudioCaptureWasapi* micAudio = nullptr;
    AudioEncoder audioEnc;
    int  audioStreamIdx = -1;
    bool audioEnabled = false;
    FFmpegEncoder encoder;
    EncoderConfig encCfg;
    ComPtr<ID3D11Texture2D> stagingTex;
    bool stagingOk = false;
    AudioAccum accum;
    FrameStats fs;
    bool failed = false;
    ErrorCode capErr;
    auto recordingStart = std::chrono::steady_clock::now();
    auto frameInterval = std::chrono::microseconds(1000000 / m_config.fps);
    auto nextFrameTime = recordingStart;
    uint64_t fIdx = 0;
    bool hasLastFrame = false;
    double lastStatsSec = 0.0;

    auto fail = [&](ErrorCode code, const char* msg) {
        Logger::Instance().Error(std::format("RT: {} [{}]", msg, ScrErrorToString(code)));
        m_lastError = code;
        failed = true;
    };

    // ---------- Init capture ----------
    // Compute target — ensure mode/hwnd consistency
    HWND targetHwnd  = nullptr;
    int  targetMon   = m_config.sourceMonitor;
    if (m_config.captureMode == CaptureMode::Window && m_config.targetWindow) {
        targetHwnd = m_config.targetWindow;
    }
    capCfg.targetWindow  = targetHwnd;
    capCfg.targetMonitor = targetMon;
    capCfg.followWindow  = m_config.followWindow;

    Logger::Instance().Info(std::format(
        "RT: capCfg mode={} hwnd={:p} mon={}",
        targetHwnd ? "Window" : "Monitor",
        (void*)targetHwnd, targetMon));

    capErr = capture.Initialize(m_d3d.GetDevice(), m_d3d.GetAdapter(), capCfg);
    if (capErr != ScrError::Ok) { fail(capErr, "capture init failed"); goto cleanup; }

    outW = capture.Width();
    outH = capture.Height();
    Logger::Instance().Info(std::format("RT: capture {}x{} mode={}",
        outW, outH,
        m_config.captureMode == CaptureMode::Window ? "window" : "monitor"));

    // ---------- Init audio ----------
    if (m_config.captureSysAudio) {
        sysAudio = new AudioCaptureWasapi(AudioCaptureWasapi::System);
        if (sysAudio->Start() != ScrError::Ok) { delete sysAudio; sysAudio = nullptr; }
    }
    if (m_config.captureMic) {
        micAudio = new AudioCaptureWasapi(AudioCaptureWasapi::Microphone);
        if (micAudio->Start() != ScrError::Ok) { delete micAudio; micAudio = nullptr; }
    }
    audioEnabled = (sysAudio || micAudio);
    if (audioEnabled) {
        int sr = sysAudio ? sysAudio->SampleRate() : 48000;
        int ch = sysAudio ? sysAudio->Channels()   : 2;
        if (!audioEnc.Initialize(sr, ch, 128000)) audioEnabled = false;
    }

    // ---------- Init encoder ----------
    encCfg.width       = outW;
    encCfg.height      = outH;
    encCfg.fps         = m_config.fps;
    encCfg.bitrateKbps = m_config.bitrateKbps;
    encCfg.outputPath  = std::filesystem::path(m_config.outputPath).string();
    encCfg.preset      = "medium";

    if (!encoder.Initialize(encCfg)) { fail(ScrError::EncoderInitFailed, "encoder init"); goto cleanup; }

    if (audioEnabled) {
        audioStreamIdx = encoder.AddAudioStream(audioEnc.CodecCtx());
        if (audioStreamIdx < 0) audioEnabled = false;
    }

    // ---------- Main loop: fixed-output FPS pacing ----------
    // Pacing starts NOW — after all init is done. Init delay is not counted.
    recordingStart = std::chrono::steady_clock::now();
    nextFrameTime = recordingStart;
    fIdx = 0;
    fs = FrameStats{};

    Logger::Instance().Info(std::format(
        "RT: started fps={} br={}kbps audio={} out={}x{} interval={}ms",
        m_config.fps, m_config.bitrateKbps, audioEnabled, outW, outH,
        (int)std::chrono::duration_cast<std::chrono::milliseconds>(frameInterval).count()));

    // Atomically transition from Starting → Recording.
    // If StopRecording() was called during init, the state is Stopping; skip loop.
    {
        RecordingState expected = RecordingState::Starting;
        if (!m_state.compare_exchange_strong(expected, RecordingState::Recording)) {
            Logger::Instance().Info("RT: stopped during init, skipping loop");
        }
    }

    while (m_state.load() == RecordingState::Recording ||
           m_state.load() == RecordingState::Paused) {

        if (m_state.load() == RecordingState::Paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // ---- Fixed-output frame slot ----
        // Each slot produces exactly one encoded frame for the output.
        nextFrameTime = recordingStart + fIdx * frameInterval;

        // Wait until next frame time (poll stop/pause every 10ms)
        while (std::chrono::steady_clock::now() < nextFrameTime &&
               (m_state.load() == RecordingState::Recording ||
                m_state.load() == RecordingState::Paused)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (m_state.load() != RecordingState::Recording &&
            m_state.load() != RecordingState::Paused) break;

        // Pacing drift: only warn / reset if severely behind (≥1s)
        auto now = std::chrono::steady_clock::now();
        auto driftMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - nextFrameTime).count();
        if (driftMs >= 1000) {
            Logger::Instance().Warning(std::format(
                "RT: pacing drift {}ms, resetting frame index", driftMs));
            fIdx = static_cast<uint64_t>((now - recordingStart) / frameInterval);
            ++fs.droppedFrames;
            nextFrameTime = recordingStart + fIdx * frameInterval;
        }

        // ---- Audio (not paced — real-time capture) ----
        if (audioEnabled) {
            float buf[9600];
            size_t nSys = sysAudio ? sysAudio->ReadSamples(buf, 9600) : 0;
            size_t nMic = micAudio ? micAudio->ReadSamples(buf + nSys, 9600 - nSys) : 0;
            float mix[9600];
            size_t nMix = AudioMixer::Mix(mix, 9600, buf, nSys, 1.0f, buf + nSys, nMic, 1.0f);
            if (nMix > 0) accum.Append(mix, nMix);
            size_t chunk = (size_t)audioEnc.FrameSize() * 2;
            while (accum.Size() >= chunk) {
                audioEnc.Encode(accum.Data(), chunk, 0);
                accum.Consume(chunk);
                WriteAudioPackets(audioEnc, audioStreamIdx, encoder);
            }
        }

        // ---- Video: acquire with short timeout (1ms) ----
        // Short timeout prevents DDA from blocking the fixed-output pacing.
        // When DDA has no new frame, GCS copies last frame (or black) into m_outputTex.
        ScrError err = capture.AcquireNextFrame(1);

        bool hasFrameToEncode = false;

        if (err == ScrError::Ok) {
            ++fs.capturedFrames;
            hasFrameToEncode = true;
            hasLastFrame = true;
        } else if (err == ScrError::FrameAcquireTimeout) {
            if (hasLastFrame)
                ++fs.duplicatedFrames;
            else
                ++fs.blackFrames;
            hasFrameToEncode = true;
        } else if (err == ScrError::WindowMinimized) {
            if (hasLastFrame)
                ++fs.duplicatedFrames;
            else
                ++fs.blackFrames;
            hasFrameToEncode = true;
        } else if (err == ScrError::DuplicationAccessLost) {
            ++fs.recoveryCount;
            Logger::Instance().Info(std::format(
                "RT: DDA recovered (#{})", fs.recoveryCount));
            if (hasLastFrame)
                ++fs.duplicatedFrames;
            else
                ++fs.blackFrames;
            hasFrameToEncode = true;
        } else if (err == ScrError::DuplicationRecreateFailed) {
            fail(err, "DDA recovery failed after 5 attempts");
            break;
        } else if (err == ScrError::WindowClosed) {
            Logger::Instance().Info("RT: window closed, stopping");
            break;
        } else {
            fail(err, "capture error");
            break;
        }

        // ---- Encode video frame (every frame slot must produce output) ----
        if (hasFrameToEncode) {
            ID3D11Texture2D* frameTex = capture.GetFrameTexture();
            if (!stagingOk && frameTex) {
                D3D11_TEXTURE2D_DESC desc{};
                frameTex->GetDesc(&desc);
                D3D11_TEXTURE2D_DESC stDesc = {
                    .Width = desc.Width, .Height = desc.Height,
                    .MipLevels = 1, .ArraySize = 1,
                    .Format = desc.Format, .SampleDesc = {1, 0},
                    .Usage = D3D11_USAGE_STAGING, .BindFlags = 0,
                    .CPUAccessFlags = D3D11_CPU_ACCESS_READ, .MiscFlags = 0,
                };
                stagingOk = SUCCEEDED(m_d3d.GetDevice()->CreateTexture2D(&stDesc, nullptr, &stagingTex));
                if (!stagingOk) { fail(ScrError::TextureCreateFailed, "staging tex"); break; }
            }
            if (stagingOk && frameTex) {
                ID3D11DeviceContext* ctx = m_d3d.GetContext();
                ctx->CopyResource(stagingTex.Get(), frameTex);
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                    encoder.EncodeFrame(mapped.pData, (int)mapped.RowPitch);
                    ++fs.encodedFrames;
                    ctx->Unmap(stagingTex.Get(), 0);
                }
            }
            capture.ReleaseFrame();  // safe for all paths (guarded by m_frameAcquired)
        } else {
            ++fs.droppedFrames;
        }

        ++fIdx;

        // ---- Periodic STATS (every ~5s of wall-clock) ----
        double elapsedSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - recordingStart).count();
        if (elapsedSec >= 5.0 && elapsedSec - lastStatsSec >= 4.0) {
            lastStatsSec = elapsedSec;
            int expectedFrames = (int)(elapsedSec * m_config.fps + 0.5);
            auto s = encoder.GetStats();
            Logger::Instance().Info(std::format(
                "STATS: elapsed={:.1f} expected={} idx={} cap={} enc={} dupe={} black={} drop={} size={:.1f}MB",
                elapsedSec, expectedFrames, fIdx,
                fs.capturedFrames, fs.encodedFrames,
                fs.duplicatedFrames, fs.blackFrames, fs.droppedFrames,
                s.totalBytes / (1024.0 * 1024.0)));
            if (fs.encodedFrames < expectedFrames - 5) {
                Logger::Instance().Warning(std::format(
                    "RT: output fps drift expected={} enc={}", expectedFrames, fs.encodedFrames));
            }
            // Update status for UI
            {
                std::lock_guard lock(m_statusMtx);
                m_status.state   = RecordingState::Recording;
                m_status.frames  = (int)fIdx;
                m_status.durationMs = (int64_t)(elapsedSec * 1000);
                m_status.fileSize = s.totalBytes;
            }
        }
    }

    // ========= Finalize =========
    if (!failed) Logger::Instance().Info("RT: finalizing...");

    if (audioEnabled) {
        if (!accum.Empty()) audioEnc.Encode(accum.Data(), accum.Size(), 0);
        audioEnc.Flush();
        WriteAudioPackets(audioEnc, audioStreamIdx, encoder);
    }
    encoder.Finalize();

    {
        std::lock_guard lock(m_statusMtx);
        auto s = encoder.GetStats();
        m_status.state   = RecordingState::Idle;
        m_status.frames  = s.framesOutput;
        m_status.fileSize = s.totalBytes;
        m_status.durationMs = (int64_t)(fIdx * 1000LL / m_config.fps);
    }

    Logger::Instance().Info(std::format(
        "RT: done idx={} cap={} enc={} drop={} dupe={} black={} recv={} size={}",
        fIdx, fs.capturedFrames, fs.encodedFrames,
        fs.droppedFrames, fs.duplicatedFrames, fs.blackFrames, fs.recoveryCount,
        encoder.GetStats().totalBytes));

cleanup:
    capture.Shutdown();
    if (sysAudio) { sysAudio->Stop(); delete sysAudio; }
    if (micAudio) { micAudio->Stop(); delete micAudio; }
    if (SUCCEEDED(hr)) CoUninitialize();

    m_state.store(failed ? RecordingState::Error : RecordingState::Idle);

    if (m_statusCb) {
        std::lock_guard lock(m_statusMtx);
        m_statusCb(m_status);
    }
    Logger::Instance().Info("RT: thread finished");
}

// ============================================================
void RecorderController::WriteAudioPackets(AudioEncoder& enc, int streamIdx, FFmpegEncoder& mux) {
    while (auto* pkt = enc.TakePacket()) {
        mux.WriteExternalPacket(pkt, streamIdx);
        av_packet_free(&pkt);
    }
}
