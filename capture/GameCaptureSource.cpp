#include "GameCaptureSource.h"
#include "../platform/Logger.h"
#include <format>
#include <cmath>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// Forward declarations
static std::string WtoA(std::wstring_view ws);
static const char* DxgiErrorStr(HRESULT hr);

GameCaptureSource::GameCaptureSource() = default;
GameCaptureSource::~GameCaptureSource() { Shutdown(); }

// ============================================================
// Initialize
// ============================================================
ScrError GameCaptureSource::Initialize(ID3D11Device* device, IDXGIAdapter* adapter,
                                         const Config& cfg) {
    if (m_initialized) return ScrError::AlreadyRunning;
    if (!device || !adapter) return ScrError::InvalidParam;

    m_device = device;
    m_device->GetImmediateContext(&m_context);
    m_config = cfg;
    m_window = cfg.targetWindow;

    // DPI
    m_dpi = GetDpiForWindow(GetDesktopWindow());
    if (m_dpi == 0) m_dpi = 96;

    ScrError err;
    if (cfg.targetWindow) {
        err = InitWindow(cfg.targetWindow);
        LogSourceInfo(L"Window");
    } else {
        err = InitMonitor(cfg.targetMonitor);
        LogSourceInfo(L"Monitor");
    }
    if (err != ScrError::Ok) return err;

    // Fixed output size — must be even for H.264/YUV420P
    m_outputW = cfg.outputWidth  > 0 ? cfg.outputWidth  : m_cropW;
    m_outputH = cfg.outputHeight > 0 ? cfg.outputHeight : m_cropH;
    if (m_outputW <= 0) m_outputW = m_width;
    if (m_outputH <= 0) m_outputH = m_height;
    // Normalize to even
    int rawW = m_outputW, rawH = m_outputH;
    m_outputW &= ~1;
    m_outputH &= ~1;
    if (m_outputW < 2 || m_outputH < 2) return ScrError::InvalidFrameSize;
    if (m_outputW != rawW || m_outputH != rawH)
        Logger::Instance().Info(std::format(
            "GCS: normalized output {}x{} -> {}x{}", rawW, rawH, m_outputW, m_outputH));

    auto oErr = EnsureOutputTexture(m_outputW, m_outputH);
    if (oErr != ScrError::Ok) return oErr;

    m_initialized = true;
    return ScrError::Ok;
}

void GameCaptureSource::Shutdown() {
    ReleaseFrame();
    m_duplication.Reset();
    m_output.Reset();
    m_cropTex.Reset();
    m_outputTex.Reset();
    m_lastFrameTex.Reset();
    m_blackTex.Reset();
    m_frameTex.Reset();
    m_context.Reset();
    m_device.Reset();
    m_initialized = false;
}

// ============================================================
// InitMonitor / InitWindow
// ============================================================
ScrError GameCaptureSource::InitMonitor(int monitorIndex) {
    m_backend = CaptureBackend::MonitorDD;

    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    m_device.As(&dxgiDev);
    dxgiDev->GetAdapter(&adapter);

    ComPtr<IDXGIOutput> output;
    for (UINT i = 0; SUCCEEDED(adapter->EnumOutputs(i, &output)); ++i) {
        if ((int)i == monitorIndex) break;
        output.Reset();
    }
    if (!output) return ScrError::DXGIOutputNotFound;

    DXGI_OUTPUT_DESC desc{};
    output->GetDesc(&desc);
    m_width  = desc.DesktopCoordinates.right  - desc.DesktopCoordinates.left;
    m_height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    m_cropW  = m_width;
    m_cropH  = m_height;

    if (!RecreateDuplication(output.Get()))
        return ScrError::DuplicationFailed;

    return ScrError::Ok;
}

ScrError GameCaptureSource::InitWindow(HWND hwnd) {
    m_backend = CaptureBackend::WindowRegion;
    m_window  = hwnd;
    m_lastWinCheck = std::chrono::steady_clock::now();

    // Validate window state
    if (!IsWindow(hwnd)) {
        Logger::Instance().Error("GCS: InitWindow IsWindow=false");
        return ScrError::WindowClosed;
    }
    if (IsIconic(hwnd)) {
        Logger::Instance().Warning(std::format("GCS: InitWindow hwnd={} minimized", (int64_t)(intptr_t)hwnd));
        return ScrError::WindowMinimized;
    }
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        Logger::Instance().Warning("GCS: InitWindow window cloaked");
    }

    // Debug log window info
    wchar_t title[128] = {};
    GetWindowTextW(hwnd, title, 128);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    RECT rc{};
    BOOL hasRect = GetWindowRect(hwnd, &rc);
    Logger::Instance().Info(std::format(
        "GCS: InitWindow hwnd={} title='{}' pid={} vis={} icon={} rect=({},{},{},{})",
        (int64_t)(intptr_t)hwnd, WtoA(title), (int)pid,
        (int)IsWindowVisible(hwnd), (int)IsIconic(hwnd),
        (int)rc.left, (int)rc.top, (int)rc.right, (int)rc.bottom));

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    Logger::Instance().Info(std::format("GCS: InitWindow MonitorFromWindow={:p}", (void*)hMon));

    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    m_device.As(&dxgiDev);
    dxgiDev->GetAdapter(&adapter);

    ComPtr<IDXGIOutput> output;
    for (UINT i = 0; SUCCEEDED(adapter->EnumOutputs(i, &output)); ++i) {
        DXGI_OUTPUT_DESC d{};
        output->GetDesc(&d);
        if (d.Monitor == hMon) { m_windowMonitor = (int)i; break; }
        output.Reset();
    }
    if (!output) {
        Logger::Instance().Warning("GCS: InitWindow no output for window's monitor, falling back to cfg monitor");
        return InitMonitor(m_config.targetMonitor);
    }

    if (!RecreateDuplication(output.Get()))
        return ScrError::DuplicationFailed;

    return UpdateCropFromWindow();
}

// ============================================================
// RecreateDuplication
// ============================================================
bool GameCaptureSource::RecreateDuplication(IDXGIOutput* output) {
    HRESULT hr = E_FAIL;

    ComPtr<IDXGIOutput5> out5;
    if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&out5)))) {
        DXGI_FORMAT fmts[] = { DXGI_FORMAT_B8G8R8A8_UNORM };
        hr = out5->DuplicateOutput1(m_device.Get(), 0, 1, fmts, &m_duplication);
    }
    if (!m_duplication) {
        ComPtr<IDXGIOutput1> out1;
        hr = output->QueryInterface(IID_PPV_ARGS(&out1));
        if (FAILED(hr)) return false;
        hr = out1->DuplicateOutput(m_device.Get(), &m_duplication);
    }
    if (FAILED(hr) || !m_duplication) {
        const char* name = DxgiErrorStr(hr);
        if (name[0])
            Logger::Instance().Error(std::format("GCS: DuplicateOutput failed HRESULT=0x{:08X} {}", (UINT)hr, name));
        else
            Logger::Instance().Error(std::format("GCS: DuplicateOutput failed HRESULT=0x{:08X}", (UINT)hr));
        return false;
    }

    m_output = output;
    m_recoveryCount = 0;

    DXGI_OUTPUT_DESC d{};
    output->GetDesc(&d);
    m_width  = d.DesktopCoordinates.right  - d.DesktopCoordinates.left;
    m_height = d.DesktopCoordinates.bottom - d.DesktopCoordinates.top;

    return true;
}

// ============================================================
// AcquireNextFrame
// ============================================================
ScrError GameCaptureSource::AcquireNextFrame(uint32_t timeoutMs) {
    if (!m_initialized) return ScrError::NotInitialized;
    ReleaseFrame();

    // ---- Window state check (throttled) ----
    if (m_window && m_backend == CaptureBackend::WindowRegion) {
        auto now = std::chrono::steady_clock::now();
        if (now - m_lastWinCheck >= kWinCheckInterval) {
            m_lastWinCheck = now;
            switch (CheckWindowState()) {
            case WinState::Closed:
                Logger::Instance().Info("GCS: window closed");
                return ScrError::WindowClosed;
            case WinState::Minimized:
                m_lastFrameWasMinimized = true;
                CopyLastFrameForRepeat();
                return ScrError::WindowMinimized;
            case WinState::CrossMonitor:
                Logger::Instance().Info("GCS: cross-monitor, re-arming");
                if (ReinitForWindow(m_window) != ScrError::Ok) {
                    Logger::Instance().Error("GCS: cross-monitor re-init failed");
                    return ScrError::WindowClosed;
                }
                break;
            case WinState::Moved:
                Logger::Instance().Info("GCS: window moved");
                UpdateCropFromWindow();
                break;
            case WinState::Invisible:
                CopyLastFrameForRepeat();
                return ScrError::WindowMinimized;
            case WinState::OK:
                break;
            }
        }
    }

    // ---- DDA acquire ----
    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO info{};
    HRESULT hr = m_duplication->AcquireNextFrame(timeoutMs, &info, &resource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        CopyLastFrameForRepeat();
        return ScrError::FrameAcquireTimeout;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        Logger::Instance().Warning(std::format(
            "GCS: ACCESS_LOST (#{})", m_recoveryCount + 1));
        m_duplication.Reset();
        // Retry with backoff
        for (int attempt = 0; attempt < 5; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 + attempt * 50));
            if (RecreateDuplication(m_output.Get())) {
                Logger::Instance().Info(std::format(
                    "GCS: DDA recovered after attempt {}", attempt + 1));
                m_recoveryCount = attempt + 1;
                CopyLastFrameForRepeat();
                return ScrError::DuplicationAccessLost;
            }
        }
        Logger::Instance().Error("GCS: DDA recovery failed after 5 attempts");
        return ScrError::DuplicationRecreateFailed;
    }
    if (FAILED(hr)) {
        return ScrError::FrameAcquireFailed;
    }

    m_recoveryCount = 0;
    m_lastFrameWasMinimized = false;
    m_lastFrameWasBlack = false;

    hr = resource.As(&m_frameTex);
    if (FAILED(hr)) {
        m_duplication->ReleaseFrame();
        return ScrError::FrameAcquireFailed;
    }

    m_frameAcquired = true;

    // ---- Crop to window region ----
    if (m_backend == CaptureBackend::WindowRegion && m_cropTex) {
        D3D11_BOX box;
        box.left   = (UINT)(std::max)(0L, m_cropOffset.x);
        box.top    = (UINT)(std::max)(0L, m_cropOffset.y);
        box.right  = (UINT)(std::min)(m_cropOffset.x + (LONG)m_cropW, (LONG)m_width);
        box.bottom = (UINT)(std::min)(m_cropOffset.y + (LONG)m_cropH, (LONG)m_height);
        box.front  = 0;
        box.back   = 1;

        if (box.right <= box.left || box.bottom <= box.top) {
            m_duplication->ReleaseFrame();
            m_frameTex.Reset();
            return ScrError::WindowCropOutOfBounds;
        }

        m_context->CopySubresourceRegion(m_cropTex.Get(), 0, 0, 0, 0,
                                          m_frameTex.Get(), 0, &box);
        m_frameTex = m_cropTex;
    }

    // ---- Compose to fixed-size output canvas ----
    auto compErr = ComposeToFixedCanvas();
    if (compErr != ScrError::Ok) return compErr;

    // Save as last frame for minimize recovery
    if (m_outputTex) {
        if (!m_lastFrameTex) {
            D3D11_TEXTURE2D_DESC d{};
            m_outputTex->GetDesc(&d);
            m_device->CreateTexture2D(&d, nullptr, &m_lastFrameTex);
        }
        if (m_lastFrameTex)
            m_context->CopyResource(m_lastFrameTex.Get(), m_outputTex.Get());
        m_hasLastFrame = true;
    }

    return ScrError::Ok;
}

void GameCaptureSource::ReleaseFrame() {
    m_frameTex.Reset();
    if (m_duplication && m_frameAcquired) {
        m_duplication->ReleaseFrame();
        m_frameAcquired = false;
    }
}

// ============================================================
// ComposeToFixedCanvas — copy cropped frame onto fixed-size black canvas
// ============================================================
ScrError GameCaptureSource::EnsureOutputTexture(int w, int h) {
    if (m_outputTex) {
        D3D11_TEXTURE2D_DESC d{};
        m_outputTex->GetDesc(&d);
        if ((int)d.Width == w && (int)d.Height == h)
            return ScrError::Ok;
        m_outputTex.Reset();
        m_lastFrameTex.Reset();
        m_blackTex.Reset();
        m_hasLastFrame = false;
    }
    D3D11_TEXTURE2D_DESC desc = {
        .Width = (UINT)w, .Height = (UINT)h,
        .MipLevels = 1, .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc = {1, 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = 0,
        .CPUAccessFlags = 0,
        .MiscFlags = 0,
    };
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_outputTex)))
        return ScrError::TextureCopyFailed;
    return ScrError::Ok;
}

ScrError GameCaptureSource::ComposeToFixedCanvas() {
    if (!m_outputTex) return ScrError::Unknown;
    if (!m_frameTex)  return ScrError::InvalidParam;

    // Fill canvas with black, then copy frame region on top
    FillBlackFrame();

    D3D11_TEXTURE2D_DESC frameDesc{};
    m_frameTex->GetDesc(&frameDesc);
    D3D11_BOX box = { 0, 0, 0, (UINT)frameDesc.Width, (UINT)frameDesc.Height, 1 };
    m_context->CopySubresourceRegion(m_outputTex.Get(), 0, 0, 0, 0,
                                      m_frameTex.Get(), 0, &box);
    return ScrError::Ok;
}

ScrError GameCaptureSource::FillBlackFrame() {
    if (!m_outputTex) return ScrError::Unknown;
    D3D11_TEXTURE2D_DESC d{};
    m_outputTex->GetDesc(&d);

    // Lazily create persistent black staging texture
    if (!m_blackTex) {
        D3D11_TEXTURE2D_DESC sd = d;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        sd.BindFlags = 0;
        if (FAILED(m_device->CreateTexture2D(&sd, nullptr, &m_blackTex)))
            return ScrError::TextureCopyFailed;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_blackTex.Get(), 0, D3D11_MAP_WRITE, 0, &mapped))) {
            // Fill with zero (black) — only once
            size_t rowSize = (size_t)d.Width * 4; // BGRA 32bpp
            for (UINT y = 0; y < d.Height; ++y)
                std::memset((char*)mapped.pData + mapped.RowPitch * y, 0, rowSize);
            m_context->Unmap(m_blackTex.Get(), 0);
        }
    }
    // Copy persisted black onto output canvas
    m_context->CopyResource(m_outputTex.Get(), m_blackTex.Get());
    return ScrError::Ok;
}

void GameCaptureSource::CopyLastFrameForRepeat() {
    if (m_hasLastFrame && m_lastFrameTex && m_outputTex) {
        m_context->CopyResource(m_outputTex.Get(), m_lastFrameTex.Get());
        m_lastFrameWasBlack = false;
    } else {
        FillBlackFrame();
        m_lastFrameWasBlack = true;
    }
}

// ============================================================
// Window state tracking
// ============================================================
GameCaptureSource::WinState GameCaptureSource::CheckWindowState() {
    if (!IsWindow(m_window)) return WinState::Closed;

    bool nowIconic = IsIconic(m_window) != FALSE;
    if (nowIconic != m_lastIconicState) {
        if (nowIconic)
            Logger::Instance().Info(std::format(
                "GCS: window minimized hwnd={}", (int64_t)(intptr_t)m_window));
        else
            Logger::Instance().Info(std::format(
                "GCS: window restored hwnd={}", (int64_t)(intptr_t)m_window));
        m_lastIconicState = nowIconic;
    }
    if (nowIconic) return WinState::Minimized;

    // Check visibility/cloaked
    if (!IsWindowVisible(m_window)) return WinState::Invisible;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(m_window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))))
        if (cloaked) return WinState::Invisible;

    m_windowWasMinimized = false;

    // Get DWM bounds for movement detection (same coordinate system as UpdateCropFromWindow)
    RECT dwmBounds{};
    bool dwmOk = SUCCEEDED(DwmGetWindowAttribute(m_window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                                  &dwmBounds, sizeof(dwmBounds)))
                 && dwmBounds.right > dwmBounds.left && dwmBounds.bottom > dwmBounds.top;
    if (!dwmOk) {
        // Fallback to client rect in screen coords
        RECT rc{};
        if (!GetClientRect(m_window, &rc) || rc.right == 0 || rc.bottom == 0)
            return WinState::Minimized;
        MapWindowPoints(m_window, nullptr, (POINT*)&rc, 2);
        dwmBounds = rc;
    }

    // Cross-monitor check
    HMONITOR hMon = MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    m_device.As(&dxgiDev);
    dxgiDev->GetAdapter(&adapter);

    int newMonIdx = m_windowMonitor;
    ComPtr<IDXGIOutput> monOutput;
    for (UINT i = 0; SUCCEEDED(adapter->EnumOutputs(i, &monOutput)); ++i) {
        DXGI_OUTPUT_DESC d{};
        monOutput->GetDesc(&d);
        if (d.Monitor == hMon) { newMonIdx = (int)i; break; }
        monOutput.Reset();
    }
    if (newMonIdx != m_windowMonitor) return WinState::CrossMonitor;

    // Tolerance of 4 px to avoid spurious "moved" from DPI rounding (same coords as m_windowRect)
    if (std::abs(dwmBounds.left - m_windowRect.left) >= 4 ||
        std::abs(dwmBounds.top - m_windowRect.top) >= 4)
        return WinState::Moved;

    return WinState::OK;
}

// ============================================================
// UpdateCropFromWindow
// ============================================================
ScrError GameCaptureSource::UpdateCropFromWindow() {
    if (!m_window) {
        Logger::Instance().Error("GCS: UpdateCrop m_window=null");
        return ScrError::InvalidParam;
    }
    if (!IsWindow(m_window)) {
        Logger::Instance().Error("GCS: UpdateCrop IsWindow=false");
        return ScrError::WindowClosed;
    }

    // Prefer DWMWA_EXTENDED_FRAME_BOUNDS (physical pixels, Per-Monitor DPI aware)
    RECT bounds{};
    HRESULT dwmHr = DwmGetWindowAttribute(m_window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                           &bounds, sizeof(bounds));
    bool dwmOk = SUCCEEDED(dwmHr) && bounds.right > bounds.left && bounds.bottom > bounds.top;

    RECT cr{};
    BOOL crOk = FALSE;
    if (!dwmOk) {
        crOk = GetClientRect(m_window, &cr);
        if (crOk) MapWindowPoints(m_window, nullptr, (POINT*)&cr, 2);
    }

    RECT rc = dwmOk ? bounds : cr;
    bool rcValid = rc.right > rc.left && rc.bottom > rc.top;

    Logger::Instance().Info(std::format(
        "GCS: bounds dwm({:08x})=({},{},{},{}) cl={} cl=({},{},{},{}) final=({},{},{},{})",
        (UINT)dwmHr,
        bounds.left, bounds.top, bounds.right, bounds.bottom,
        (int)crOk,
        cr.left, cr.top, cr.right, cr.bottom,
        rc.left, rc.top, rc.right, rc.bottom));

    if (!rcValid) {
        Logger::Instance().Error("GCS: bounds empty");
        return ScrError::WindowBoundsInvalid;
    }

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 64 || h < 64) {
        Logger::Instance().Error(std::format("GCS: window too small {}x{}", w, h));
        return ScrError::WindowTooSmall;
    }

    m_windowRect = rc;
    // Normalize crop to even dimensions (H.264/YUV420P requirement)
    int rawW = w, rawH = h;
    w &= ~1;  // floor to even
    h &= ~1;
    m_cropW = w;
    m_cropH = h;
    if (w < 2 || h < 2) {
        Logger::Instance().Error(std::format("GCS: crop too small after even-normalize {}x{} -> {}x{}", rawW, rawH, w, h));
        return ScrError::WindowTooSmall;
    }
    if (w != rawW || h != rawH)
        Logger::Instance().Info(std::format("GCS: crop raw={}x{} normalized={}x{}", rawW, rawH, w, h));

    HMONITOR hMon = MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMon, &mi);

    m_cropOffset.x = rc.left - mi.rcMonitor.left;
    m_cropOffset.y = rc.top  - mi.rcMonitor.top;

    // Outside monitor check
    int monW = (int)(mi.rcMonitor.right - mi.rcMonitor.left);
    int monH = (int)(mi.rcMonitor.bottom - mi.rcMonitor.top);
    if (m_cropOffset.x + w <= 0 || m_cropOffset.y + h <= 0 ||
        m_cropOffset.x >= monW || m_cropOffset.y >= monH) {
        Logger::Instance().Warning("GCS: window outside monitor");
        return ScrError::WindowOutsideMonitor;
    }

    // Create crop texture
    D3D11_TEXTURE2D_DESC d = {
        .Width = (UINT)w, .Height = (UINT)h,
        .MipLevels = 1, .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc = {1, 0},
        .Usage = D3D11_USAGE_DEFAULT,
    };
    m_cropTex.Reset();
    if (FAILED(m_device->CreateTexture2D(&d, nullptr, &m_cropTex))) {
        Logger::Instance().Error("GCS: crop tex create failed");
        return ScrError::TextureCopyFailed;
    }

    m_dpi = GetDpiForWindow(m_window);
    Logger::Instance().Info(std::format(
        "GCS: crop {}x{} @ ({},{}) mon={} DPI={}",
        m_cropW, m_cropH, m_cropOffset.x, m_cropOffset.y,
        m_windowMonitor, m_dpi));

    return ScrError::Ok;
}

// ============================================================
// ReinitForWindow
// ============================================================
ScrError GameCaptureSource::ReinitForWindow(HWND hwnd) {
    m_duplication.Reset();
    m_cropTex.Reset();
    m_frameTex.Reset();
    return InitWindow(hwnd);
}

// ============================================================
// LogSourceInfo
// ============================================================
static const char* DxgiErrorStr(HRESULT hr) {
    switch (hr) {
    case DXGI_ERROR_ACCESS_LOST:             return "DXGI_ERROR_ACCESS_LOST";
    case DXGI_ERROR_UNSUPPORTED:             return "DXGI_ERROR_UNSUPPORTED";
    case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: return "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";
    case DXGI_ERROR_DEVICE_REMOVED:          return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_INVALID_CALL:            return "DXGI_ERROR_INVALID_CALL";
    case DXGI_ERROR_WAIT_TIMEOUT:            return "DXGI_ERROR_WAIT_TIMEOUT";
    case DXGI_ERROR_SESSION_DISCONNECTED:    return "DXGI_ERROR_SESSION_DISCONNECTED";
    case DXGI_ERROR_ACCESS_DENIED:           return "DXGI_ERROR_ACCESS_DENIED";
    case DXGI_ERROR_NOT_FOUND:               return "DXGI_ERROR_NOT_FOUND";
    default: return "";
    }
}

static std::string WtoA(std::wstring_view ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), s.data(), len, nullptr, nullptr);
    return s;
}

void GameCaptureSource::LogSourceInfo(const wchar_t* label) {
    if (m_config.targetWindow) {
        wchar_t title[256] = {};
        DWORD pid = 0;
        GetWindowTextW(m_config.targetWindow, title, 256);
        GetWindowThreadProcessId(m_config.targetWindow, &pid);
        Logger::Instance().Info(std::format(
            "GCS: src={} [{}] HWND={:p} PID={} {}x{} DPI={}",
            WtoA(label), WtoA(title), (void*)m_config.targetWindow,
            pid, m_width, m_height, m_dpi));
    } else {
        Logger::Instance().Info(std::format(
            "GCS: src={} #{} ({}x{}) DPI={}",
            WtoA(label), m_config.targetMonitor, m_width, m_height, m_dpi));
    }
}
