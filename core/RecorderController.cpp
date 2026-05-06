#include "RecorderController.h"
#include "../platform/Logger.h"
#include <format>
#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
}

static std::string WstrToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                        result.data(), size, nullptr, nullptr);
    return result;
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
    try {
        RecordingThreadImpl();
    } catch (const std::exception& e) {
        Logger::Instance().Error(std::format("RT: unhandled exception: {}", e.what()));
        m_lastError = ScrError::Unknown;
        m_state.store(RecordingState::Error);
    } catch (...) {
        Logger::Instance().Error("RT: unhandled unknown exception");
        m_lastError = ScrError::Unknown;
        m_state.store(RecordingState::Error);
    }
}

void RecorderController::RecordingThreadImpl() {
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
    int64_t audioInitMs = 0;
    uint64_t audioSysSamples = 0, audioMicSamples = 0, audioMixedSamples = 0;
    uint64_t audioPacketsWritten = 0, audioBytesWritten = 0;
    uint64_t audioSilenceFrames = 0;
    int64_t lastAudioPts = -1;
    int64_t firstAudioPts = AV_NOPTS_VALUE;
    uint64_t droppedPreStartSys = 0, droppedPreStartMic = 0;

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
    Logger::Instance().Info("RT: audio init begin");
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
    audioInitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_threadStart).count();
    Logger::Instance().Info(std::format("RT: audio init done audioEnabled={} totalMs={}", (int)audioEnabled, audioInitMs));

    // ---------- Init encoder ----------
    encCfg.width       = outW;
    encCfg.height      = outH;
    encCfg.fps         = m_config.fps;
    encCfg.bitrateKbps = m_config.bitrateKbps;
    encCfg.outputPath  = WstrToUtf8(m_config.outputPath);
    encCfg.preset      = "medium";

    if (!encoder.Initialize(encCfg)) { fail(ScrError::EncoderInitFailed, "encoder init"); goto cleanup; }

    if (audioEnabled) {
        audioStreamIdx = encoder.AddAudioStream(audioEnc.CodecCtx());
        if (audioStreamIdx < 0) audioEnabled = false;
    }

    // ---- Write muxer header (must be done before any packet) ----
    if (!encoder.BeginOutput()) {
        fail(ScrError::MuxerInitFailed, "muxer header write failed");
        goto cleanup;
    }

    // ---- Clear pre-start audio backlog ----
    // Audio capture may have accumulated samples during init; discard them
    // so that audio PTS 0 aligns with recording start.
    if (audioEnabled) {
        float discard[9600];
        while (sysAudio && sysAudio->ReadSamples(discard, 9600) > 0)
            droppedPreStartSys += 9600;
        while (micAudio && micAudio->ReadSamples(discard, 9600) > 0)
            droppedPreStartMic += 9600;
        audioSysSamples = 0;
        audioMicSamples = 0;
        audioMixedSamples = 0;
        Logger::Instance().Info(std::format(
            "AUDIO: cleared pre-start backlog sys={} mic={}",
            droppedPreStartSys, droppedPreStartMic));
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
            size_t nSys = 0, nMic = 0;

            // Read system audio (stereo)
            if (sysAudio) {
                nSys = sysAudio->ReadSamples(buf, 9600);
                audioSysSamples += nSys;
            }

            // Read mic audio — if mono, expand to stereo in-place
            if (micAudio) {
                float micBuf[4800];
                size_t nMicRaw = micAudio->ReadSamples(micBuf, 4800);
                audioMicSamples += nMicRaw;
                if (nMicRaw > 0 && micAudio->Channels() == 1) {
                    // Mono → Stereo: duplicate each sample
                    for (size_t i = nMicRaw; i > 0; --i) {
                        buf[nSys + i * 2 - 1] = micBuf[i - 1];
                        buf[nSys + i * 2 - 2] = micBuf[i - 1];
                    }
                    nMic = nMicRaw * 2;
                } else {
                    // Already stereo or unknown
                    memcpy(buf + nSys, micBuf, nMicRaw * sizeof(float));
                    nMic = nMicRaw;
                }
            }

            // Mix into accum (input1=sys buf[0..nSys-1], input2=mic buf[nSys..nSys+nMic-1])
            float mixBuf[9600];
            size_t nMix = AudioMixer::Mix(mixBuf, 9600, buf, nSys, 1.0f, buf + nSys, nMic, 1.0f);
            if (nMix > 0) {
                accum.Append(mixBuf, nMix);
                audioMixedSamples += nMix;
            }

            // Encode AAC frames paced by recording elapsed time.
            // If real samples are insufficient, pad with silence to keep
            // audio track duration aligned with video.
            size_t chunk = (size_t)audioEnc.FrameSize() * 2; // stereo float samples per AAC frame
            auto recElapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - recordingStart).count();
            int64_t targetChSamples = (int64_t)(recElapsed * 48000);
            int64_t submittedChSamples = (int64_t)audioEnc.SubmittedFrames() * audioEnc.FrameSize();
            int64_t maxLeadSamples = 4800; // 100ms lead

            // Keep audio clock in sync: if real samples are insufficient,
            // pad with silence so the audio track duration matches video.
            while (submittedChSamples + (int64_t)audioEnc.FrameSize() <= targetChSamples + maxLeadSamples) {
                if (accum.Size() < chunk) {
                    float silence[2048] = {};
                    accum.Append(silence, chunk);
                    audioSilenceFrames++;
                }
                audioEnc.Encode(accum.Data(), chunk, 0);
                accum.Consume(chunk);
                submittedChSamples += audioEnc.FrameSize();
                // Write all pending packets
                while (auto* pkt = audioEnc.TakePacket()) {
                    // Normalize PTS on original pkt before write
                    if (firstAudioPts == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
                        firstAudioPts = pkt->pts;
                    if (firstAudioPts != AV_NOPTS_VALUE) {
                        if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= firstAudioPts;
                        if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= firstAudioPts;
                    }

                    audioPacketsWritten++;
                    audioBytesWritten += pkt->size;
                    if (pkt->pts >= 0 && pkt->pts > lastAudioPts)
                        lastAudioPts = pkt->pts;

                    // Log first 3 packets
                    if (audioPacketsWritten <= 3) {
                        Logger::Instance().Info(std::format(
                            "RC: audio packet idx={} pts={} dts={} size={} streamIdx={}",
                            audioPacketsWritten, (int64_t)pkt->pts, (int64_t)pkt->dts,
                            pkt->size, audioStreamIdx));
                    }

                    // Write via FFmpegEncoder (internal clone for safety)
                    encoder.WriteExternalPacket(pkt, audioStreamIdx);
                    av_packet_free(&pkt);
                }
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

        // ---- Update UI timer every iteration ----
        {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - recordingStart).count();
            std::lock_guard lock(m_statusMtx);
            m_status.state   = RecordingState::Recording;
            m_status.frames  = (int)fIdx;
            m_status.durationMs = (int64_t)elapsedMs;
            m_status.fileSize = encoder.GetStats().totalBytes;
        }

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
            if (audioEnabled) {
                Logger::Instance().Info(std::format(
                    "AUDIO STATS: sysSamples={} micSamples={} mixedSamples={} submittedFrames={} queuedPkts={} writtenPkts={} bytes={} lastPts={} silence={}",
                    audioSysSamples, audioMicSamples, audioMixedSamples,
                    audioEnc.SubmittedFrames(), audioEnc.QueuedPackets(),
                    audioPacketsWritten, audioBytesWritten,
                    lastAudioPts, audioSilenceFrames));
            }
        }
    }

    // ========= Finalize =========
    if (!failed) Logger::Instance().Info("RT: finalizing...");

    if (audioEnabled) {
        // ---- Tail padding: pad audio to match final recording duration ----
        auto recEndMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - recordingStart).count();
        double recDurSec = recEndMs / 1000.0;
        int64_t finalTargetSamples = (int64_t)(recDurSec * 48000);
        int64_t finalSubmitted = (int64_t)audioEnc.SubmittedFrames() * audioEnc.FrameSize();
        int64_t missing = finalTargetSamples - finalSubmitted;
        int64_t maxPad = (int64_t)(2.0 * 48000); // max 2 seconds of padding

        if (missing > maxPad) {
            Logger::Instance().Warning(std::format(
                "AUDIO: tail pad clamped missingSec={:.3f}", missing / 48000.0));
            missing = maxPad;
        }

        int64_t tailPadFrames = 0;
        while (missing >= (int64_t)audioEnc.FrameSize()) {
            float silence[2048] = {};
            accum.Append(silence, (size_t)audioEnc.FrameSize() * 2);
            audioEnc.Encode(accum.Data(), (size_t)audioEnc.FrameSize() * 2, 0);
            accum.Consume((size_t)audioEnc.FrameSize() * 2);
            missing -= audioEnc.FrameSize();
            tailPadFrames++;
        }

        if (tailPadFrames > 0) {
            Logger::Instance().Info(std::format(
                "AUDIO: tail pad done frames={} sec={:.3f}", tailPadFrames, tailPadFrames * audioEnc.FrameSize() / 48000.0));
        }

        // Flush AAC encoder
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

    if (audioEnabled) {
        auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - recordingStart).count();
        double audioDurationSec = (double)audioEnc.SubmittedFrames() * audioEnc.FrameSize() / 48000.0;
        double videoDurationSec = durationMs / 1000.0;
        Logger::Instance().Info(std::format(
            "AUDIO FINAL: sysSamples={} micSamples={} mixedSamples={} submittedFrames={} queuedPkts={} writtenPkts={} bytes={} lastPts={} silence={} audioSec={:.3f} videoSec={:.3f} leadSec={:.3f}",
            audioSysSamples, audioMicSamples, audioMixedSamples,
            audioEnc.SubmittedFrames(), audioEnc.QueuedPackets(),
            audioPacketsWritten, audioBytesWritten,
            lastAudioPts, audioSilenceFrames,
            audioDurationSec, videoDurationSec, audioDurationSec - videoDurationSec));
    }
    Logger::Instance().Info(std::format(
        "RT: done idx={} cap={} enc={} drop={} dupe={} black={} recv={} audioBytes={} size={}",
        fIdx, fs.capturedFrames, fs.encodedFrames,
        fs.droppedFrames, fs.duplicatedFrames, fs.blackFrames, fs.recoveryCount,
        audioBytesWritten, encoder.GetStats().totalBytes));

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
