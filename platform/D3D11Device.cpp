#include "D3D11Device.h"
#include "Logger.h"
#include <format>
#include <filesystem>

ScrError D3D11Device::Initialize() {
    if (m_initialized) return ScrError::AlreadyInitialized;

    // 1. Create DXGI factory
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        Logger::Instance().Error(std::format("CreateDXGIFactory1 failed: {:08x}", hr));
        return ScrError::DeviceCreateFailed;
    }

    // 2. Find the first hardware adapter that has display outputs
    ComPtr<IDXGIAdapter> selectedAdapter;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter> adapter;
        hr = factory->EnumAdapters(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) continue;

        DXGI_ADAPTER_DESC aDesc{};
        adapter->GetDesc(&aDesc);

        // Skip software adapters
        if (aDesc.VendorId == 0x1414 && aDesc.DeviceId == 0x8c) // Microsoft Basic Render Driver
            continue;

        // Check if this adapter has any outputs
        ComPtr<IDXGIOutput> output;
        if (SUCCEEDED(adapter->EnumOutputs(0, &output))) {
            selectedAdapter = adapter;
            Logger::Instance().Info(std::format("Selected adapter: {} (VRAM {} MiB)",
                std::filesystem::path(aDesc.Description).string(),
                aDesc.DedicatedVideoMemory / (1024 * 1024)));
            break;
        }
    }

    if (!selectedAdapter) {
        // Fallback: take the first adapter even without outputs
        hr = factory->EnumAdapters(0, &selectedAdapter);
        if (FAILED(hr)) {
            Logger::Instance().Error("No adapter found");
            return ScrError::AdapterNotFound;
        }
    }

    // 3. Create D3D11 device on the selected adapter
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    ComPtr<ID3D11Device>        device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL           selectedLevel = D3D_FEATURE_LEVEL_11_0;

    hr = D3D11CreateDevice(
        selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        flags, levels, (UINT)std::size(levels),
        D3D11_SDK_VERSION, &device, &selectedLevel, &context);

    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            flags, levels, (UINT)std::size(levels),
            D3D11_SDK_VERSION, &device, &selectedLevel, &context);
    }

    if (FAILED(hr)) {
        Logger::Instance().Error(std::format("D3D11CreateDevice failed: {:08x}", hr));
        return ScrError::DeviceCreateFailed;
    }

    // 4. Upgrade factory to IDXGIFactory base for public interface
    ComPtr<IDXGIFactory> baseFactory;
    factory.As(&baseFactory);

    m_device       = std::move(device);
    m_context      = std::move(context);
    m_adapter      = std::move(selectedAdapter);
    m_factory      = std::move(baseFactory);
    m_featureLevel = selectedLevel;
    m_initialized  = true;

    Logger::Instance().Info(std::format("D3D11 initialized (FL {}.{})",
        (selectedLevel >> 12) & 0xF, (selectedLevel >> 8) & 0xF));
    return ScrError::Success;
}

void D3D11Device::Shutdown() {
    if (!m_initialized) return;

    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }
    m_context.Reset();
    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();
    m_initialized = false;
    Logger::Instance().Info("D3D11 shut down");
}
