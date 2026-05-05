#pragma once
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <chrono>
#include <vector>
#include "../platform/ErrorCode.h"

using Microsoft::WRL::ComPtr;

enum class CaptureBackend {
    MonitorDD,
    WindowRegion,
};

class GameCaptureSource final {
public:
    GameCaptureSource();
    ~GameCaptureSource();

    GameCaptureSource(const GameCaptureSource&) = delete;
    GameCaptureSource& operator=(const GameCaptureSource&) = delete;

    struct Config {
        HWND  targetWindow  = nullptr;
        int   targetMonitor = 0;
        bool  followWindow  = true;

        // Fixed output size (0 = auto from initial capture)
        int   outputWidth   = 0;
        int   outputHeight  = 0;
    };

    ScrError Initialize(ID3D11Device* device, IDXGIAdapter* adapter, const Config& cfg);
    void     Shutdown();

    ScrError AcquireNextFrame(uint32_t timeoutMs);
    ID3D11Texture2D* GetFrameTexture() const { return m_outputTex ? m_outputTex.Get() : m_frameTex.Get(); }
    void ReleaseFrame();

    bool IsInitialized() const { return m_initialized; }
    int  Width()  const { return m_outputW; }
    int  Height() const { return m_outputH; }
    CaptureBackend ActiveBackend() const { return m_backend; }

    // Stats (resets each AcquireNextFrame cycle)
    bool  WasWindowMinimized() const { return m_lastFrameWasMinimized; }
    bool  WasBlackFrameSent()  const { return m_lastFrameWasBlack; }
    int   RecoveryCount()      const { return m_recoveryCount; }

private:
    enum class WinState { OK, Moved, CrossMonitor, Minimized, Closed, Invisible };

    ScrError InitMonitor(int monitorIndex);
    ScrError InitWindow(HWND hwnd);
    void      LogSourceInfo(const wchar_t* label);

    WinState  CheckWindowState();
    ScrError UpdateCropFromWindow();
    ScrError ReinitForWindow(HWND hwnd);
    bool      RecreateDuplication(IDXGIOutput* output);

    // Fixed-frame composition
    ScrError EnsureOutputTexture(int w, int h);
    ScrError ComposeToFixedCanvas();
    ScrError FillBlackFrame();
    void      CopyLastFrameForRepeat();

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<IDXGIOutput>            m_output;
    ComPtr<ID3D11Texture2D>        m_frameTex;
    ComPtr<ID3D11Texture2D>        m_cropTex;
    ComPtr<ID3D11Texture2D>        m_outputTex;      // fixed-size output canvas
    ComPtr<ID3D11Texture2D>        m_lastFrameTex;    // last good frame for repeat
    ComPtr<ID3D11Texture2D>        m_blackTex;        // persistent black fill
    bool                           m_hasLastFrame = false;

    Config         m_config;
    CaptureBackend m_backend       = CaptureBackend::MonitorDD;
    bool           m_initialized   = false;
    int            m_width         = 0;   // source (monitor/DDA) dimensions
    int            m_height        = 0;
    int            m_outputW       = 0;   // fixed output canvas
    int            m_outputH       = 0;
    int            m_recoveryCount = 0;

    // Window tracking
    HWND   m_window         = nullptr;
    int    m_windowMonitor  = 0;
    RECT   m_windowRect     = {};
    POINT  m_cropOffset     = {};
    int    m_cropW          = 0;
    int    m_cropH          = 0;

    // Minimized handling
    bool   m_lastFrameWasMinimized = false;
    bool   m_lastFrameWasBlack     = false;
    bool   m_windowWasMinimized    = false;
    bool   m_lastIconicState       = false;

    // DDA frame tracking
    bool   m_frameAcquired         = false;

    std::chrono::steady_clock::time_point m_lastWinCheck;
    static constexpr auto kWinCheckInterval = std::chrono::milliseconds(250);

    // DPI
    UINT m_dpi = 96;
};
