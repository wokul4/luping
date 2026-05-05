#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct CaptureWindowInfo {
    HWND            hwnd        = nullptr;
    DWORD           processId   = 0;
    std::wstring    title;
    std::wstring    executable;
    int             width       = 0;
    int             height      = 0;
    int             monitorIndex = 0;
    bool            isCloaked   = false;
    bool            isMinimized = false;
    bool            isToolWindow = false;
};

class GameWindowEnumerator {
public:
    GameWindowEnumerator();

    /// Enumerate all visible application windows.
    /// Call repeatedly to refresh.
    const std::vector<CaptureWindowInfo>& Enumerate();
    void Refresh() { m_cached = false; }

    size_t GetCount() const { return m_windows.size(); }
    const CaptureWindowInfo& Get(size_t i) const { return m_windows.at(i); }

    /// Find window info by HWND.
    int FindByHwnd(HWND hwnd) const;

private:
    static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam);
    bool IsCandidate(HWND hwnd) const;

    std::vector<CaptureWindowInfo> m_windows;
    bool m_cached = false;
};
