#pragma once
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "../platform/ErrorCode.h"

using Microsoft::WRL::ComPtr;

struct DuplicationFrame {
    ID3D11Texture2D*          texture   = nullptr;
    DXGI_OUTDUPL_FRAME_INFO   frameInfo = {};
};

class DesktopDuplicationSource {
public:
    ~DesktopDuplicationSource();

    ScrError Initialize(ID3D11Device* device, IDXGIOutput* output);
    void     Shutdown();

    // timeoutMs = 0 → no wait; INFINITE → wait forever
    ScrError AcquireNextFrame(uint32_t timeoutMs);

    // Valid only between AcquireNextFrame() and ReleaseFrame()
    ID3D11Texture2D*          GetFrameTexture() const { return m_frameTexture.Get(); }
    const DuplicationFrame&   GetFrameInfo()    const { return m_frameInfo; }

    void ReleaseFrame();

    bool IsInitialized() const { return m_initialized; }

    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }

private:
    ComPtr<ID3D11Device>           m_device;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D>        m_frameTexture;
    DuplicationFrame               m_frameInfo{};
    UINT                           m_width  = 0;
    UINT                           m_height = 0;
    bool                           m_initialized = false;
};
