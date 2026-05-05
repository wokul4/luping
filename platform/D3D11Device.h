#pragma once
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "ErrorCode.h"

using Microsoft::WRL::ComPtr;

class D3D11Device {
public:
    ScrError  Initialize();
    void      Shutdown();

    ID3D11Device*        GetDevice()        const { return m_device.Get();  }
    ID3D11DeviceContext* GetContext()       const { return m_context.Get(); }
    IDXGIAdapter*        GetAdapter()       const { return m_adapter.Get(); }
    IDXGIFactory*        GetDXGIFactory()   const { return m_factory.Get(); }

    D3D_FEATURE_LEVEL    FeatureLevel()     const { return m_featureLevel; }
    bool                 IsInitialized()    const { return m_initialized;  }

private:
    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIAdapter>        m_adapter;
    ComPtr<IDXGIFactory>        m_factory;
    D3D_FEATURE_LEVEL           m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool                        m_initialized  = false;
};
