#pragma once
#include <vector>
#include <string>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "../platform/ErrorCode.h"

using Microsoft::WRL::ComPtr;

struct MonitorInfo {
    std::wstring    name;
    int             index  = 0;
    int             width  = 0;
    int             height = 0;
    bool            isPrimary = false;

    // Internal DXGI details (not used by UI)
    DXGI_OUTPUT_DESC desc     = {};
    DXGI_MODE_DESC  preferredMode = {};
};

class MonitorEnumerator {
public:
    ScrError Enumerate(IDXGIAdapter* adapter);

    size_t           GetCount()             const { return m_monitors.size(); }
    const MonitorInfo& GetMonitor(size_t i) const;
    IDXGIOutput*     GetOutput(size_t i)    const;
    size_t           GetPrimaryMonitorIndex() const;

private:
    struct Entry {
        MonitorInfo        info;
        ComPtr<IDXGIOutput> output;
    };
    std::vector<Entry> m_monitors;
};
