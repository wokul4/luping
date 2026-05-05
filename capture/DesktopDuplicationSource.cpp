#include "DesktopDuplicationSource.h"
#include "../platform/Logger.h"
#include <format>

DesktopDuplicationSource::~DesktopDuplicationSource() {
    Shutdown();
}

ScrError DesktopDuplicationSource::Initialize(ID3D11Device* device, IDXGIOutput* output) {
    if (!device || !output)     return ScrError::InvalidParam;
    if (m_initialized)          return ScrError::AlreadyInitialized;

    m_device = device;

    DXGI_OUTPUT_DESC oDesc{};
    output->GetDesc(&oDesc);
    m_width  = oDesc.DesktopCoordinates.right - oDesc.DesktopCoordinates.left;
    m_height = oDesc.DesktopCoordinates.bottom - oDesc.DesktopCoordinates.top;

    HRESULT hr = E_FAIL;

    // Prefer IDXGIOutput5::DuplicateOutput1 (explicit format → less CPU overhead)
    ComPtr<IDXGIOutput5> out5;
    if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&out5)))) {
        DXGI_FORMAT formats[] = { DXGI_FORMAT_B8G8R8A8_UNORM };
        hr = out5->DuplicateOutput1(
            device, 0, (UINT)std::size(formats), formats, &m_duplication);
        if (SUCCEEDED(hr))
            Logger::Instance().Info("Desktop Duplication: IDXGIOutput5 path");
        else
            Logger::Instance().Info("IDXGIOutput5 path failed, falling back to IDXGIOutput1");
    }

    // Fallback to IDXGIOutput1::DuplicateOutput
    if (FAILED(hr)) {
        ComPtr<IDXGIOutput1> out1;
        hr = output->QueryInterface(IID_PPV_ARGS(&out1));
        if (SUCCEEDED(hr)) {
            hr = out1->DuplicateOutput(device, &m_duplication);
            if (SUCCEEDED(hr))
                Logger::Instance().Info("Desktop Duplication: IDXGIOutput1 path");
        } else {
            Logger::Instance().Error("IDXGIOutput1 not available");
            return ScrError::DuplicationFailed;
        }
    }

    if (FAILED(hr)) {
        Logger::Instance().Error(std::format("DuplicateOutput failed: HRESULT={:08x}", hr));
        return ScrError::DuplicationFailed;
    }

    m_initialized = true;
    Logger::Instance().Info(std::format("Duplication started: {}x{}", m_width, m_height));
    return ScrError::Success;
}

ScrError DesktopDuplicationSource::AcquireNextFrame(uint32_t timeoutMs) {
    if (!m_initialized) return ScrError::NotInitialized;

    ReleaseFrame();

    ComPtr<IDXGIResource>     resource;
    DXGI_OUTDUPL_FRAME_INFO   frameInfo{};

    HRESULT hr = m_duplication->AcquireNextFrame(timeoutMs, &frameInfo, &resource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return ScrError::FrameAcquireTimeout;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        Logger::Instance().Error("Duplication access lost (desktop switch / GPU reset)");
        return ScrError::DuplicationFailed;
    }
    if (FAILED(hr)) {
        Logger::Instance().Error(std::format("AcquireNextFrame failed: HRESULT={:08x}", hr));
        return ScrError::FrameAcquireFailed;
    }

    hr = resource.As(&m_frameTexture);
    if (FAILED(hr)) {
        m_duplication->ReleaseFrame();
        Logger::Instance().Error("Failed to QI resource → ID3D11Texture2D");
        return ScrError::FrameAcquireFailed;
    }

    m_frameInfo.texture   = m_frameTexture.Get();
    m_frameInfo.frameInfo = frameInfo;
    return ScrError::Success;
}

void DesktopDuplicationSource::ReleaseFrame() {
    if (m_frameTexture) {
        m_frameTexture.Reset();
    }
    if (m_initialized && m_duplication) {
        m_duplication->ReleaseFrame();
    }
    m_frameInfo = {};
}

void DesktopDuplicationSource::Shutdown() {
    ReleaseFrame();
    m_duplication.Reset();
    m_device.Reset();
    m_initialized = false;
}
