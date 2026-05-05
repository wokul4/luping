#include "AudioCaptureWasapi.h"
#include "../platform/Logger.h"
#include <format>
#include <cstring>
#include <functiondiscoverykeys.h>

// ============================================================
// Ctor / Dtor
// ============================================================
AudioCaptureWasapi::AudioCaptureWasapi(Type type)
    : m_type(type)
    , m_ring(kRingCapacity, 0) {}

AudioCaptureWasapi::~AudioCaptureWasapi() { Stop(); }

// ============================================================
// Start / Stop
// ============================================================
ScrError AudioCaptureWasapi::Start() {
    if (m_running) return ScrError::AlreadyInitialized;

    m_shutdownEvent = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
    m_captureEvent  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_shutdownEvent || !m_captureEvent) {
        Logger::Instance().Error("AudioCapture: CreateEvent failed");
        return ScrError::Unknown;
    }

    m_thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!m_thread) {
        Logger::Instance().Error("AudioCapture: CreateThread failed");
        return ScrError::Unknown;
    }

    // Wait for thread to initialise (or fail)
    WaitForSingleObject(m_thread, 3000);

    if (!m_running) {
        Logger::Instance().Warning(std::format(
            "AudioCapture({}) unavailable", m_type == System ? "System" : "Mic"));
        return ScrError::Unknown;
    }
    Logger::Instance().Info(std::format(
        "AudioCapture({}) started: {} Hz, {} ch",
        m_type == System ? "Sys" : "Mic", m_sampleRate, m_channels));
    return ScrError::Success;
}

void AudioCaptureWasapi::Stop() {
    if (m_thread && m_running) SetEvent(m_shutdownEvent);
    if (m_thread) {
        WaitForSingleObject(m_thread, 3000);
        CloseHandle(m_thread); m_thread = nullptr;
    }
    if (m_shutdownEvent) { CloseHandle(m_shutdownEvent); m_shutdownEvent = nullptr; }
    if (m_captureEvent)  { CloseHandle(m_captureEvent);  m_captureEvent  = nullptr; }
    m_running = false;
}

// ============================================================
// ReadSamples (main thread)
// ============================================================
size_t AudioCaptureWasapi::ReadSamples(float* buffer, size_t maxSamples) {
    std::lock_guard lock(m_mtx);
    size_t avail = (m_wp >= m_rp) ? m_wp - m_rp : kRingCapacity - m_rp;
    size_t n = (std::min)(avail, maxSamples);
    if (n == 0) return 0;
    if (m_rp + n <= kRingCapacity) {
        memcpy(buffer, &m_ring[m_rp], n * sizeof(float));
    } else {
        size_t first = kRingCapacity - m_rp;
        memcpy(buffer,       &m_ring[m_rp],  first * sizeof(float));
        memcpy(buffer + first, &m_ring[0],  (n - first) * sizeof(float));
    }
    m_rp = (m_rp + n) % kRingCapacity;
    return n;
}

// ============================================================
// Thread
// ============================================================
DWORD WINAPI AudioCaptureWasapi::ThreadProc(LPVOID param) {
    auto* self = static_cast<AudioCaptureWasapi*>(param);
    self->RunCapture();
    return 0;
}

// ============================================================
// RunCapture  –  all WASAPI work on its own thread
// ============================================================
void AudioCaptureWasapi::RunCapture() {
    // ---- COM ----
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) { Logger::Instance().Error("AudioCapture COM init failed"); return; }

    DWORD flags = 0;  // declare BEFORE any goto done

    // ---- device enumerator ----
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))))
    { Logger::Instance().Error("AudioCapture: no MMDeviceEnumerator"); goto done; }

    hr = enumerator->GetDefaultAudioEndpoint(DataFlow(), eConsole, &m_device);
    if (FAILED(hr)) {
        if (m_type == Microphone)
            Logger::Instance().Info("AudioCapture(Mic): no default mic – disabled");
        else
            Logger::Instance().Error("AudioCapture(Sys): no render device");
        goto done;
    }

    // ---- IAudioClient ----
    hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER,
                            nullptr, (void**)&m_client);
    if (FAILED(hr)) { Logger::Instance().Error("AudioCapture: Activate failed"); goto done; }

    hr = m_client->GetMixFormat(&m_wfex);
    if (FAILED(hr)) { Logger::Instance().Error("AudioCapture: GetMixFormat failed"); goto done; }
    m_sampleRate = (int)m_wfex->nSamplesPerSec;
    m_channels   = (int)m_wfex->nChannels;

    flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (m_type == System) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

    hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                              100000, 0, m_wfex, nullptr);  // 10 ms buffer
    if (FAILED(hr)) { Logger::Instance().Error("AudioCapture: Initialize failed"); goto done; }

    hr = m_client->GetService(IID_PPV_ARGS(&m_captureClient));
    if (FAILED(hr)) { Logger::Instance().Error("AudioCapture: GetService failed"); goto done; }

    hr = m_client->SetEventHandle(m_captureEvent);
    if (FAILED(hr)) { Logger::Instance().Error("AudioCapture: SetEventHandle failed"); goto done; }

    hr = m_client->Start();
    if (FAILED(hr)) { Logger::Instance().Error("AudioCapture: Start failed"); goto done; }

    // ---- running ----
    m_running = true;

    {
        HANDLE waitHandles[2] = { m_shutdownEvent, m_captureEvent };
        bool done = false;
        while (!done) {
            switch (WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE)) {
            case WAIT_OBJECT_0:     done = true; break;
            case WAIT_OBJECT_0 + 1: DrainCaptured(); break;
            default:                done = true; break;
            }
        }
    }

    m_client->Stop();

done:
    CoUninitialize();
}

// ============================================================
// DrainCaptured  –  read WASAPI buffer into ring
// ============================================================
void AudioCaptureWasapi::DrainCaptured() {
    if (!m_captureClient) return;

    BYTE*  data   = nullptr;
    UINT32 frames = 0;
    DWORD  flags  = 0;

    while (m_captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr)
           != AUDCLNT_S_BUFFER_EMPTY) {
        if (frames == 0) { m_captureClient->ReleaseBuffer(0); continue; }

        size_t numSamples = frames * (size_t)m_channels;
        const float* src = reinterpret_cast<const float*>(data);
        const float* end = src + numSamples;

        // -- copy into ring --
        {
            std::lock_guard lock(m_mtx);
            while (src < end) {
                m_ring[m_wp] = *src++;
                m_wp = (m_wp + 1) % kRingCapacity;
                if (m_wp == m_rp)  // overrun – drop oldest sample
                    m_rp = (m_rp + 1) % kRingCapacity;
            }
        }

        m_captureClient->ReleaseBuffer(frames);
    }
}
