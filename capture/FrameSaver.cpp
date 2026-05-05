#include "FrameSaver.h"
#include "../platform/Logger.h"
#include <wincodec.h>
#include <format>
#include <filesystem>

ScrError FrameSaver::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context) return ScrError::InvalidParam;
    m_device  = device;
    m_context = context;
    m_initialized = true;
    return ScrError::Success;
}

ScrError FrameSaver::SaveToPng(ID3D11Texture2D* texture, const std::wstring& path) {
    if (!m_initialized) return ScrError::NotInitialized;
    if (!texture)       return ScrError::InvalidParam;

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    // --- staging texture for CPU readback ---
    D3D11_TEXTURE2D_DESC stDesc = {
        .Width          = desc.Width,
        .Height         = desc.Height,
        .MipLevels      = 1,
        .ArraySize      = 1,
        .Format         = desc.Format,
        .SampleDesc     = {1, 0},
        .Usage          = D3D11_USAGE_STAGING,
        .BindFlags      = 0,
        .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
        .MiscFlags      = 0,
    };

    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = m_device->CreateTexture2D(&stDesc, nullptr, &staging);
    if (FAILED(hr)) {
        Logger::Instance().Error(std::format("CreateTexture2D (staging) failed: {:08x}", hr));
        return ScrError::TextureCreateFailed;
    }

    m_context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        Logger::Instance().Error(std::format("Map staging texture failed: {:08x}", hr));
        return ScrError::MapFailed;
    }

    // --- WIC encode ---
    ScrError result = ScrError::SaveFrameFailed;

    ComPtr<IWICImagingFactory> wf;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wf))))
        goto cleanup;

    {
        ComPtr<IWICStream>           stream;
        ComPtr<IWICBitmapEncoder>    enc;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2>        props;

        if (FAILED(wf->CreateStream(&stream)))                             goto cleanup;
        if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) goto cleanup;
        if (FAILED(wf->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) goto cleanup;
        if (FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)))      goto cleanup;
        if (FAILED(enc->CreateNewFrame(&frame, &props)))                         goto cleanup;
        if (FAILED(frame->Initialize(props.Get())))                              goto cleanup;
        if (FAILED(frame->SetSize(desc.Width, desc.Height)))                     goto cleanup;

        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&pf)))                                  goto cleanup;

        // WritePixels( lineCount, stride, bufferSize, data )
        if (FAILED(frame->WritePixels(desc.Height, (UINT)mapped.RowPitch,
                                      (UINT)(mapped.RowPitch * desc.Height),
                                      static_cast<BYTE*>(mapped.pData))))       goto cleanup;
        if (FAILED(frame->Commit()))                                             goto cleanup;
        if (FAILED(enc->Commit()))                                              goto cleanup;

        result = ScrError::Success;
    }

cleanup:
    m_context->Unmap(staging.Get(), 0);

    if (result != ScrError::Success) {
        Logger::Instance().Error(std::format("Failed to save PNG: {}", std::filesystem::path(path).string()));
    }
    return result;
}
