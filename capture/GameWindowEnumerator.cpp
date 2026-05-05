#include "GameWindowEnumerator.h"
#include <dwmapi.h>
#include <tlhelp32.h>
#include <filesystem>

#pragma comment(lib, "dwmapi.lib")

GameWindowEnumerator::GameWindowEnumerator() = default;

const std::vector<CaptureWindowInfo>& GameWindowEnumerator::Enumerate() {
    if (m_cached) return m_windows;
    m_windows.clear();
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(this));
    m_cached = true;
    return m_windows;
}

int GameWindowEnumerator::FindByHwnd(HWND hwnd) const {
    for (size_t i = 0; i < m_windows.size(); ++i)
        if (m_windows[i].hwnd == hwnd) return (int)i;
    return -1;
}

// ---- Filter ----------------------------------------------------------
bool GameWindowEnumerator::IsCandidate(HWND hwnd) const {
    if (!IsWindowVisible(hwnd)) return false;

    int titleLen = GetWindowTextLengthW(hwnd);
    if (titleLen == 0) return false;

    // Skip cloaked UWP windows
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED,
                                         &cloaked, sizeof(cloaked))))
        if (cloaked) return false;

    // Skip minimized windows (they have no valid capture rect)
    if (IsIconic(hwnd)) return false;

    // Skip shell / desktop
    if (hwnd == GetShellWindow() || hwnd == GetDesktopWindow())
        return false;

    // Skip tool windows
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return false;

    // Must have non-zero client area
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return false;
    if (rc.right < 80 || rc.bottom < 80) return false; // too small

    return true;
}

// ---- Enum callback ---------------------------------------------------
BOOL CALLBACK GameWindowEnumerator::EnumProc(HWND hwnd, LPARAM lparam) {
    auto* self = reinterpret_cast<GameWindowEnumerator*>(lparam);
    if (!self->IsCandidate(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    // Executable name
    std::wstring exeName;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t path[MAX_PATH]{};
        DWORD sz = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, path, &sz))
            exeName = std::filesystem::path(path).filename().wstring();
        CloseHandle(hProcess);
    }
    if (exeName.empty()) return TRUE;

    // Window title
    int titleLen = GetWindowTextLengthW(hwnd);
    std::wstring title;
    if (titleLen > 0) {
        title.resize(titleLen);
        GetWindowTextW(hwnd, &title[0], titleLen + 1);
    }

    // Client rect
    RECT rc{};
    GetClientRect(hwnd, &rc);

    // Monitor index
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    int monIdx = 0;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    for (int m = 0;; ++m) {
        if (!EnumDisplayMonitors(nullptr, nullptr,
                [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
                    *reinterpret_cast<HMONITOR*>(lp) = h; return FALSE;
                }, reinterpret_cast<LPARAM>(&hMon))) {
            // Can't easily enumerate monitors here without a stateful approach
            break;
        }
    }
    // Simpler approach: use MonitorFromPoint
    POINT pt = { rc.left, rc.top };
    HMONITOR hMon2 = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (hMon2 != hMon) hMon = hMon2;

    CaptureWindowInfo info;
    info.hwnd         = hwnd;
    info.processId    = pid;
    info.title        = title;
    info.executable   = exeName;
    info.width        = rc.right - rc.left;
    info.height       = rc.bottom - rc.top;
    info.isCloaked    = false;
    info.isMinimized  = IsIconic(hwnd) != FALSE;
    info.isToolWindow = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;

    self->m_windows.push_back(std::move(info));
    return TRUE;
}
