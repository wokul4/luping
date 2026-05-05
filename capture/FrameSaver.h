#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include "../platform/ErrorCode.h"

using Microsoft::WRL::ComPtr;

class FrameSaver {
public:
    ScrError Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    ScrError SaveToPng(ID3D11Texture2D* texture, const std::wstring& filePath);

private:
    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    bool                        m_initialized = false;
};
