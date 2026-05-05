#include "AppWindow.h"
#include "../platform/Logger.h"
#include <format>
#include <filesystem>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

extern const char* kAppVersion;

AppWindow::AppWindow()  = default;
AppWindow::~AppWindow() = default;

// ============================================================
// Get executable directory (for exe-relative paths)
// ============================================================
static std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

// ============================================================
// Create
// ============================================================
bool AppWindow::Create(HINSTANCE hInstance, const AppSettings& settings, const std::filesystem::path& exeDir) {
    // Per-monitor DPI awareness (Win10 1703+)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    m_settings = settings;
    m_exeDir = exeDir;
    m_hInst = hInstance;

    WNDCLASS wc = {};
    wc.lpfnWndProc   = WndProcStatic;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ScreenRecorderWindow";

    if (!RegisterClass(&wc)) return false;

    // Title with version
    std::wstring title = L"ScreenRecorder " + std::wstring(kAppVersion, kAppVersion + strlen(kAppVersion));

    m_hwnd = CreateWindowExW(
        0, L"ScreenRecorderWindow", title.c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 420,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    CreateControls(m_hwnd);

    // Apply settings to UI controls
    ApplySettingsToUI();
    PopulateSourceList();

    // Output path from settings
    auto capturesDir = m_settings.savedOutputDir.empty()
        ? (m_exeDir / m_settings.outputDir)
        : std::filesystem::path(m_settings.savedOutputDir);
    std::filesystem::create_directories(capturesDir);

    // Default output path from settings
    wchar_t timeStr[64];
    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf_s(timeStr, L"ScreenRecorder_%04d%02d%02d_%02d%02d%02d.mkv",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    auto outPath = capturesDir / timeStr;
    SetWindowTextW(m_hOutputPath, outPath.wstring().c_str());
    m_config.outputPath = outPath.wstring();

    // Hotkeys
    m_hotkeys.Register(m_hwnd, HotkeyManager::ID_START_STOP,
                       HotkeyManager::MOD_CTRL_ALT, 'R');
    m_hotkeys.Register(m_hwnd, HotkeyManager::ID_PAUSE_RESUME,
                       HotkeyManager::MOD_CTRL_ALT, 'P');

    // Controller
    m_controller.SetStatusCallback([this](const RecorderStatus&) {
        PostMessageW(m_hwnd, WM_TIMER, ID_STATUS_TIMER, 0);
    });

    if (!m_controller.Initialize()) {
        MessageBoxW(m_hwnd, L"D3D11 init failed", L"Error", MB_ICONERROR);
        return false;
    }

    SetTimer(m_hwnd, ID_STATUS_TIMER, 500, nullptr);
    return true;
}

void AppWindow::Show(int cmdShow) { ShowWindow(m_hwnd, cmdShow); }

int AppWindow::Run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

// ============================================================
// WndProc
// ============================================================
LRESULT CALLBACK AppWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (AppWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return self ? self->WndProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        m_hotkeys.UnregisterAll(hwnd);
        m_controller.Shutdown();
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        if (m_recording && MessageBoxW(hwnd, L"Recording in progress. Exit?",
                                        L"ScreenRecorder", MB_YESNO | MB_ICONWARNING) != IDYES)
            return 0;
        m_controller.StopRecording();
        DestroyWindow(hwnd);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        switch (id) {
        case IDC_START:    OnStartRecording();   break;
        case IDC_STOP:     OnStopRecording();    break;
        case IDC_PAUSE:    OnPauseResume();      break;
        case IDC_BROWSE:   OnBrowseOutput();     break;
        case IDC_REFRESH:  OnRefreshWindows();   break;
        case IDC_SOURCE:
            if (code == CBN_SELCHANGE) {
                ApplySelectionToConfig();
                UpdateWindowInfoText();
            }
            break;
        case IDC_BITRATE:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendMessage(m_hBitrate, CB_GETCURSEL, 0, 0);
                EnableWindow(m_hBitrateVal, (sel == 3) ? TRUE : FALSE);
            }
            break;
        }
        return 0;
    }

    case WM_HOTKEY: {
        int id = (int)wp;
        if (id == HotkeyManager::ID_START_STOP) {
            if (m_recording) OnStopRecording(); else OnStartRecording();
        } else if (id == HotkeyManager::ID_PAUSE_RESUME) {
            OnPauseResume();
        }
        return 0;
    }

    case WM_TIMER:
        if (wp == ID_STATUS_TIMER) UpdateStatusText();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
// CreateControls
// ============================================================
void AppWindow::CreateControls(HWND hwnd) {
    auto make = [&](const wchar_t* cls, const wchar_t* text,
                    DWORD style, int x, int y, int w, int h, int id) -> HWND {
        return CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
                               x, y, w, h, hwnd, (HMENU)(INT_PTR)id, m_hInst, nullptr);
    };

    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    auto apply = [&](HWND c) { SendMessageW(c, WM_SETFONT, (WPARAM)hFont, TRUE); };

    int y = 12, pad = 28, left = 12, labelW = 55;

    // Row: Source (combo + refresh button)
    make(L"STATIC", L"Source", SS_LEFT, left, y, labelW, 20, -1);
    m_hSource = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                     left + labelW, y - 2, 270, 300, IDC_SOURCE);
    apply(m_hSource);
    m_hRefresh = make(L"BUTTON", L"Refresh", BS_PUSHBUTTON | WS_TABSTOP,
                      left + labelW + 275, y - 2, 70, 24, IDC_REFRESH);
    apply(m_hRefresh);
    y += pad;

    // Row: Window info
    m_hWinInfo = make(L"STATIC", L"", SS_LEFT, left, y, 400, 18, IDC_WIN_INFO);
    apply(m_hWinInfo);
    y += pad;

    // Row: FPS + Bitrate
    make(L"STATIC", L"FPS", SS_LEFT, left, y, 30, 20, -1);
    m_hFps30 = make(L"BUTTON", L"30", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                    left + 32, y, 45, 22, IDC_FPS_30);
    m_hFps60 = make(L"BUTTON", L"60", BS_AUTORADIOBUTTON | WS_TABSTOP,
                    left + 80, y, 45, 22, IDC_FPS_60);
    Button_SetCheck(m_hFps30, BST_CHECKED);
    apply(m_hFps30); apply(m_hFps60);

    make(L"STATIC", L"Bitrate", SS_LEFT, left + 160, y, 48, 20, -1);
    m_hBitrate = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                      left + 210, y - 2, 90, 120, IDC_BITRATE);
    SendMessage(m_hBitrate, CB_ADDSTRING, 0, (LPARAM)L"Low (2.5M)");
    SendMessage(m_hBitrate, CB_ADDSTRING, 0, (LPARAM)L"Mid (5M)");
    SendMessage(m_hBitrate, CB_ADDSTRING, 0, (LPARAM)L"High (10M)");
    SendMessage(m_hBitrate, CB_ADDSTRING, 0, (LPARAM)L"Custom");
    SendMessage(m_hBitrate, CB_SETCURSEL, 2, 0);
    apply(m_hBitrate);
    m_hBitrateVal = make(L"EDIT", L"10000", ES_NUMBER | WS_TABSTOP,
                         left + 210, y - 2, 90, 22, IDC_BITRATE_VAL);
    EnableWindow(m_hBitrateVal, FALSE);
    apply(m_hBitrateVal);
    y += pad;

    // Row: Output
    make(L"STATIC", L"Output", SS_LEFT, left, y, labelW, 20, -1);
    m_hOutputPath = make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP,
                         left + labelW, y - 2, 290, 22, IDC_OUTPUT_PATH);
    m_hBrowse = make(L"BUTTON", L"Browse", BS_PUSHBUTTON | WS_TABSTOP,
                     left + labelW + 295, y - 2, 70, 24, IDC_BROWSE);
    apply(m_hOutputPath); apply(m_hBrowse);
    y += pad;

    // Row: Audio
    m_hSysAudio = make(L"BUTTON", L"System Audio", BS_AUTOCHECKBOX | WS_TABSTOP,
                       left, y, 130, 22, IDC_SYS_AUDIO);
    Button_SetCheck(m_hSysAudio, BST_CHECKED);
    m_hMicAudio = make(L"BUTTON", L"Microphone", BS_AUTOCHECKBOX | WS_TABSTOP,
                       left + 140, y, 130, 22, IDC_MIC_AUDIO);
    Button_SetCheck(m_hMicAudio, BST_CHECKED);
    apply(m_hSysAudio); apply(m_hMicAudio);
    y += 30;

    // Row: Buttons
    m_hStart = make(L"BUTTON", L"Start Rec", BS_PUSHBUTTON | WS_TABSTOP,
                    left, y, 100, 30, IDC_START);
    m_hPause = make(L"BUTTON", L"Pause", BS_PUSHBUTTON | WS_TABSTOP,
                    left + 108, y, 80, 30, IDC_PAUSE);
    m_hStop  = make(L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP,
                    left + 196, y, 80, 30, IDC_STOP);
    EnableWindow(m_hPause, FALSE);
    EnableWindow(m_hStop, FALSE);
    apply(m_hStart); apply(m_hPause); apply(m_hStop);
    y += 38;

    // Row: Status
    m_hDuration = make(L"STATIC", L"Time: 00:00:00", SS_LEFT, left, y, 160, 20, IDC_DURATION);
    m_hFileSize = make(L"STATIC", L"Size: 0.0 MB", SS_LEFT, left + 180, y, 150, 20, IDC_FILESIZE);
    apply(m_hDuration); apply(m_hFileSize);
    y += 22;

    m_hStatus = make(L"STATIC", L"Ready", SS_LEFT, left, y, 300, 20, IDC_STATUS);
    apply(m_hStatus);

    SetWindowPos(hwnd, nullptr, 0, 0, 530, y + 50, SWP_NOMOVE | SWP_NOZORDER);
}

// ============================================================
// Source list
// ============================================================
void AppWindow::PopulateSourceList() {
    m_sourceItems.clear();
    SendMessage(m_hSource, CB_RESETCONTENT, 0, 0);

    // --- Monitors ---
    auto monitors = m_controller.GetMonitors();
    for (auto& m : monitors) {
        std::wstring label = std::format(L"[M{}] {}x{} {}",
            m.index, m.width, m.height,
            m.isPrimary ? L"[primary]" : L"");
        int idx = (int)SendMessage(m_hSource, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        if (idx >= 0) {
            m_sourceItems.push_back({ SourceItem::Monitor, m.index, nullptr });
        }
    }

    // --- Windows ---
    m_winEnum.Refresh();
    auto& windows = m_winEnum.Enumerate();
    for (auto& w : windows) {
        std::wstring label = std::format(L"[W] {} - {}",
            w.executable, w.title.substr(0, 40));
        int idx = (int)SendMessage(m_hSource, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        if (idx >= 0) {
            m_sourceItems.push_back({ SourceItem::Window, 0, w.hwnd });
        }
    }

    SendMessage(m_hSource, CB_SETCURSEL, 0, 0);
    ApplySelectionToConfig();
    UpdateWindowInfoText();
}

void AppWindow::OnRefreshWindows() {
    PopulateSourceList();
}

void AppWindow::ApplySettingsToUI() {
    // FPS
    if (m_settings.fps >= 60) Button_SetCheck(m_hFps60, BST_CHECKED);
    else                      Button_SetCheck(m_hFps30, BST_CHECKED);

    // Bitrate
    int brIdx = 2; // default High (10M)
    if      (m_settings.bitrateKbps <= 2500)  brIdx = 0;
    else if (m_settings.bitrateKbps <= 5000)  brIdx = 1;
    else if (m_settings.bitrateKbps <= 10000) brIdx = 2;
    else                                      brIdx = 3;
    SendMessage(m_hBitrate, CB_SETCURSEL, brIdx, 0);
    if (brIdx == 3) {
        wchar_t buf[32];
        swprintf_s(buf, L"%d", m_settings.bitrateKbps);
        SetWindowTextW(m_hBitrateVal, buf);
        EnableWindow(m_hBitrateVal, TRUE);
    } else {
        EnableWindow(m_hBitrateVal, FALSE);
    }

    // Audio
    Button_SetCheck(m_hSysAudio, m_settings.recordSystemAudio ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(m_hMicAudio, m_settings.recordMicrophone ? BST_CHECKED : BST_UNCHECKED);

    // Update config
    m_config.fps = m_settings.fps;
    m_config.bitrateKbps = m_settings.bitrateKbps;
    m_config.captureSysAudio = m_settings.recordSystemAudio;
    m_config.captureMic = m_settings.recordMicrophone;
}

void AppWindow::SaveSettings() {
    if (m_exeDir.empty()) return;
    m_settings.fps = Button_GetCheck(m_hFps60) ? 60 : 30;
    int sel = (int)SendMessage(m_hBitrate, CB_GETCURSEL, 0, 0);
    switch (sel) {
    case 0: m_settings.bitrateKbps = 2500;  break;
    case 1: m_settings.bitrateKbps = 5000;  break;
    case 2: m_settings.bitrateKbps = 10000; break;
    default: {
        wchar_t buf[32];
        GetWindowTextW(m_hBitrateVal, buf, 32);
        m_settings.bitrateKbps = _wtoi(buf);
        if (m_settings.bitrateKbps < 100) m_settings.bitrateKbps = 5000;
        break;
    }
    }
    m_settings.recordSystemAudio = (Button_GetCheck(m_hSysAudio) == BST_CHECKED);
    m_settings.recordMicrophone  = (Button_GetCheck(m_hMicAudio) == BST_CHECKED);
    m_settings.Save(m_exeDir);
}

void AppWindow::ApplySelectionToConfig() {
    int sel = (int)SendMessage(m_hSource, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)m_sourceItems.size()) return;

    auto& item = m_sourceItems[sel];
    if (item.type == SourceItem::Monitor) {
        m_config.captureMode = CaptureMode::Monitor;
        m_config.sourceMonitor = item.monitorIndex;
        m_config.targetWindow  = nullptr;
    } else {
        m_config.captureMode = CaptureMode::Window;
        m_config.sourceMonitor = 0;
        m_config.targetWindow  = item.hwnd;
    }
}

void AppWindow::UpdateWindowInfoText() {
    int sel = (int)SendMessage(m_hSource, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)m_sourceItems.size()) return;

    auto& item = m_sourceItems[sel];
    if (item.type == SourceItem::Window && item.hwnd) {
        wchar_t title[128] = {};
        GetWindowTextW(item.hwnd, title, 128);
        DWORD pid = 0;
        GetWindowThreadProcessId(item.hwnd, &pid);
        RECT rc{};
        GetClientRect(item.hwnd, &rc);
        SetWindowTextW(m_hWinInfo,
            std::format(L"  {:p} | {} | {}x{} | PID={}",
                (void*)item.hwnd, title,
                rc.right - rc.left, rc.bottom - rc.top, pid).c_str());
    } else if (item.type == SourceItem::Monitor) {
        auto monitors = m_controller.GetMonitors();
        for (auto& m : monitors) {
            if (m.index == item.monitorIndex) {
                SetWindowTextW(m_hWinInfo,
                    std::format(L"  {}x{} {}", m.width, m.height,
                        m.isPrimary ? L"[primary]" : L"").c_str());
                break;
            }
        }
    }
}

// ============================================================
// Actions
// ============================================================
void AppWindow::OnStartRecording() {
    ApplySelectionToConfig();
    m_config.fps = Button_GetCheck(m_hFps60) ? 60 : 30;

    // Pre-flight check for window capture
    if (m_config.captureMode == CaptureMode::Window && m_config.targetWindow) {
        bool winValid = IsWindow(m_config.targetWindow) != FALSE;
        bool winIconic = IsIconic(m_config.targetWindow) != FALSE;
        bool winVisible = IsWindowVisible(m_config.targetWindow) != FALSE;
        Logger::Instance().Info(std::format(
            "APP: preflight hwnd={:p} isWindow={} iconic={} visible={}",
            (void*)m_config.targetWindow, (int)winValid, (int)winIconic, (int)winVisible));
        if (!winValid) {
            SetWindowTextW(m_hStatus, L"Target window no longer exists. Please refresh source list.");
            return;
        }
        if (winIconic) {
            SetWindowTextW(m_hStatus, L"Target window is minimized. Please restore it first.");
            return;
        }
        if (!winVisible) {
            SetWindowTextW(m_hStatus, L"Target window is hidden. Please restore it first.");
            return;
        }
    }
    m_config.captureSysAudio = (Button_GetCheck(m_hSysAudio) == BST_CHECKED);
    m_config.captureMic      = (Button_GetCheck(m_hMicAudio) == BST_CHECKED);

    int sel = (int)SendMessage(m_hBitrate, CB_GETCURSEL, 0, 0);
    switch (sel) {
    case 0: m_config.bitrateKbps = 2500;  break;
    case 1: m_config.bitrateKbps = 5000;  break;
    case 2: m_config.bitrateKbps = 10000; break;
    default: {
        wchar_t buf[32];
        GetWindowTextW(m_hBitrateVal, buf, 32);
        m_config.bitrateKbps = _wtoi(buf);
        if (m_config.bitrateKbps < 100) m_config.bitrateKbps = 5000;
        break;
    }
    }

    wchar_t pathBuf[512];
    GetWindowTextW(m_hOutputPath, pathBuf, 512);
    m_config.outputPath = pathBuf;
    std::filesystem::create_directories(
        std::filesystem::path(m_config.outputPath).parent_path());

    Logger::Instance().Info(std::format("APP: StartRec mode={} monitor={} hwnd={:p}",
        m_config.captureMode == CaptureMode::Monitor ? "Monitor" : "Window",
        m_config.sourceMonitor, (void*)m_config.targetWindow));
    if (!m_controller.StartRecording(m_config)) return;

    m_recording = true;
    m_paused = false;

    auto disable = [&](HWND c) { EnableWindow(c, FALSE); };
    disable(m_hSource); disable(m_hRefresh); disable(m_hFps30); disable(m_hFps60);
    disable(m_hBitrate); disable(m_hBitrateVal);
    disable(m_hSysAudio); disable(m_hMicAudio);
    disable(m_hOutputPath); disable(m_hBrowse);
    EnableWindow(m_hStart, FALSE);
    EnableWindow(m_hPause, TRUE);
    EnableWindow(m_hStop, TRUE);
    SetWindowTextW(m_hStart, L"Recording...");
    SetWindowTextW(m_hStatus, L"Recording...");
}

void AppWindow::OnStopRecording() {
    m_controller.StopRecording();
    m_recording = false;
    m_paused = false;

    // Save final file info before re-enabling UI
    auto st = m_controller.GetStatus();
    auto finalPath = m_config.outputPath;
    auto finalSize = st.fileSize;
    auto finalDuration = st.durationMs;

    auto enable = [&](HWND c) { EnableWindow(c, TRUE); };
    enable(m_hSource); enable(m_hRefresh); enable(m_hFps30); enable(m_hFps60);
    enable(m_hBitrate); enable(m_hSysAudio); enable(m_hMicAudio);
    enable(m_hOutputPath); enable(m_hBrowse);
    EnableWindow(m_hStart, TRUE);
    EnableWindow(m_hPause, FALSE);
    EnableWindow(m_hStop, FALSE);
    SetWindowTextW(m_hStart, L"Start Rec");
    SetWindowTextW(m_hPause, L"Pause");

    // Save settings
    SaveSettings();

    // Show completion info
    if (finalSize > 0) {
        auto sec = (int)(finalDuration / 1000);
        double mb = finalSize / (1024.0 * 1024.0);
        auto fp = std::filesystem::path(finalPath);
        auto fname = fp.filename().wstring();
        SetWindowTextW(m_hStatus,
            std::format(L"Saved: {} ({:.1f} MB, {:02d}:{:02d}:{:02d})",
                fname, mb,
                sec / 3600, (sec % 3600) / 60, sec % 60).c_str());

        // Completion prompt
        if (m_settings.showCompletionPrompt) {
            std::wstring msg = std::format(L"Recording saved.\n\nFile: {}\nSize: {:.1f} MB\nDuration: {:02d}:{:02d}:{:02d}",
                fp.wstring(), mb, sec / 3600, (sec % 3600) / 60, sec % 60);
            std::wstring caption = std::format(L"ScreenRecorder {} — Recording Complete", std::wstring(kAppVersion, kAppVersion + strlen(kAppVersion)));
            int ret = MessageBoxW(m_hwnd, msg.c_str(), caption.c_str(), MB_OKCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND);
            if (ret == IDCANCEL) {
                // Open folder
                ShellExecuteW(nullptr, L"open", L"explorer.exe",
                    std::format(L"/select,\"{}\"", fp.wstring()).c_str(), nullptr, SW_SHOWDEFAULT);
            }
        }
    } else {
        SetWindowTextW(m_hStatus, L"Stopped (no frames recorded)");
    }
    UpdateStatusText();
}

void AppWindow::OnPauseResume() {
    if (!m_recording) return;
    if (m_paused) {
        m_controller.ResumeRecording();
        m_paused = false;
        SetWindowTextW(m_hPause, L"Pause");
        SetWindowTextW(m_hStatus, L"Recording...");
    } else {
        m_controller.PauseRecording();
        m_paused = true;
        SetWindowTextW(m_hPause, L"Resume");
        SetWindowTextW(m_hStatus, L"Paused");
    }
}

void AppWindow::OnBrowseOutput() {
    wchar_t path[512] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = m_hwnd;
    ofn.lpstrFilter  = L"MKV Files (*.mkv)\0*.mkv\0All Files\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = 512;
    ofn.lpstrDefExt  = L"mkv";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) {
        SetWindowTextW(m_hOutputPath, path);
        m_config.outputPath = path;
    }
}

void AppWindow::UpdateStatusText() {
    auto st = m_controller.GetStatus();
    auto state = m_controller.GetState();

    if (m_recording || m_paused || state != RecordingState::Idle) {
        int sec = (int)(st.durationMs / 1000);
        SetWindowTextW(m_hDuration,
            std::format(L"Time: {:02d}:{:02d}:{:02d}",
                sec / 3600, (sec % 3600) / 60, sec % 60).c_str());
        double mb = st.fileSize / (1024.0 * 1024.0);
        SetWindowTextW(m_hFileSize,
            std::format(L"Size: {:.1f} MB", mb).c_str());
    }

    // Check for errors from last recording
    ScrError lastErr = m_controller.GetLastError();
    if (lastErr != ScrError::Ok && !m_recording) {
        auto msg = ScrErrorToUserMessage(lastErr);
        if (!msg.empty()) {
            SetWindowTextW(m_hStatus, msg.c_str());
            // Only pop up message box for terminal errors
            switch (lastErr) {
            case ScrError::WindowClosed:
            case ScrError::DuplicationRecreateFailed:
            case ScrError::EncoderInitFailed:
            case ScrError::OutputFileCreateFailed:
                MessageBoxW(m_hwnd, msg.c_str(), L"Recording Error", MB_OK | MB_ICONWARNING);
                break;
            default:
                break;
            }
        }
    }
}
