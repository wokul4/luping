#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiosessiontypes.h>
#include <vector>
#include <mutex>
#include <wrl/client.h>
#include "../platform/ErrorCode.h"

using Microsoft::WRL::ComPtr;

/// WASAPI capture for either system audio loopback or microphone.
/// Runs its own capture thread; data is read via ReadSamples().
class AudioCaptureWasapi {
public:
    enum Type { System, Microphone };

    AudioCaptureWasapi(Type type);
    ~AudioCaptureWasapi();

    AudioCaptureWasapi(const AudioCaptureWasapi&) = delete;
    AudioCaptureWasapi& operator=(const AudioCaptureWasapi&) = delete;

    /// Open device, create capture thread, start streaming.
    ScrError Start();

    /// Signal thread to stop and wait for it.
    void Stop();

    bool IsRunning() const { return m_running; }

    /// Copy up to @p maxSamples interleaved float samples into @p buffer.
    size_t ReadSamples(float* buffer, size_t maxSamples);

    int SampleRate() const { return m_sampleRate; }
    int Channels()   const { return m_channels;   }

private:
    enum { kRingCapacity = 48000 * 2 * 2 * 4 }; // ~4 s of stereo float

    // All WASAPI work happens on the capture thread (incl. COM init)
    static DWORD WINAPI ThreadProc(LPVOID param);
    void RunCapture();
    void DrainCaptured();

    // Thread & sync
    HANDLE m_shutdownEvent = nullptr;
    HANDLE m_captureEvent  = nullptr;
    HANDLE m_readyEvent    = nullptr;
    HANDLE m_thread        = nullptr;
    bool   m_running       = false;

    // Ring buffer (thread-safe)
    std::vector<float> m_ring;
    size_t             m_wp = 0;
    size_t             m_rp = 0;
    mutable std::mutex m_mtx;

    // WASAPI objects (owned by capture thread)
    ComPtr<IMMDevice>          m_device;
    ComPtr<IAudioClient>       m_client;
    ComPtr<IAudioCaptureClient> m_captureClient;
    WAVEFORMATEX*              m_wfex  = nullptr;
    Type m_type        = System;
    int  m_sampleRate  = 48000;
    int  m_channels    = 2;
    UINT m_frameSize   = 4;

    // Device role
    EDataFlow DataFlow() const {
        return m_type == System ? eRender : eCapture;
    }
};
