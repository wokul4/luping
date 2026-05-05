#include "MonitorEnumerator.h"
#include "../platform/Logger.h"
#include <format>
#include <filesystem>

ScrError MonitorEnumerator::Enumerate(IDXGIAdapter* adapter) {
    if (!adapter) return ScrError::InvalidParam;
    m_monitors.clear();

    DXGI_ADAPTER_DESC aDesc{};
    adapter->GetDesc(&aDesc);
    Logger::Instance().Info(std::format("Adapter: {} (VRAM {} MiB)",
        std::filesystem::path(aDesc.Description).string(),
        aDesc.DedicatedVideoMemory / (1024 * 1024)));

    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIOutput> output;
        HRESULT hr = adapter->EnumOutputs(i, &output);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) continue;

        Entry entry;
        entry.output = std::move(output);

        DXGI_OUTPUT_DESC oDesc{};
        entry.output->GetDesc(&oDesc);
        entry.info.name      = oDesc.DeviceName;
        entry.info.desc      = oDesc;
        entry.info.index     = (int)i;
        entry.info.width     = oDesc.DesktopCoordinates.right  - oDesc.DesktopCoordinates.left;
        entry.info.height    = oDesc.DesktopCoordinates.bottom - oDesc.DesktopCoordinates.top;
        entry.info.isPrimary = (oDesc.DesktopCoordinates.left == 0 &&
                                oDesc.DesktopCoordinates.top  == 0);

        // Query current display mode
        ComPtr<IDXGIOutput1> out1;
        if (SUCCEEDED(entry.output.As(&out1))) {
            DXGI_MODE_DESC match = {};
            UINT count = 0;
            out1->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, nullptr);
            if (count > 0) {
                std::vector<DXGI_MODE_DESC> modes(count);
                out1->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, modes.data());

                DEVMODEW dm{};
                dm.dmSize = sizeof(dm);
                if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
                    for (auto& m : modes) {
                        if (m.Width == dm.dmPelsWidth && m.Height == dm.dmPelsHeight) {
                            match = m;
                            break;
                        }
                    }
                }
                if (match.Width == 0 && !modes.empty())
                    match = modes[0];
            }
            entry.info.preferredMode = match;
        }

        Logger::Instance().Info(std::format(
            "  Output {}: {}x{} @ ({},{})  [primary={}]", i,
            entry.info.width, entry.info.height,
            oDesc.DesktopCoordinates.left,
            oDesc.DesktopCoordinates.top,
            entry.info.isPrimary));

        m_monitors.push_back(std::move(entry));
    }

    if (m_monitors.empty()) {
        Logger::Instance().Warning("No outputs found on adapter");
        return ScrError::OutputNotFound;
    }
    return ScrError::Success;
}

const MonitorInfo& MonitorEnumerator::GetMonitor(size_t i) const {
    return m_monitors.at(i).info;
}

IDXGIOutput* MonitorEnumerator::GetOutput(size_t i) const {
    return m_monitors.at(i).output.Get();
}

size_t MonitorEnumerator::GetPrimaryMonitorIndex() const {
    for (size_t i = 0; i < m_monitors.size(); ++i) {
        if (m_monitors[i].info.isPrimary)
            return i;
    }
    return 0;
}
