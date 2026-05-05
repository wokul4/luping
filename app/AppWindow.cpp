#include "AppWindow.h"
#include "../platform/Logger.h"
#include <format>
#include <filesystem>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <gdiplus.h>

extern const char* kAppVersion;

AppWindow::AppWindow() = default;
AppWindow::~AppWindow() { DestroyThemeBrushesAndFonts(); }

static std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

bool AppWindow::Create(HINSTANCE hInstance, const AppSettings& settings, const std::filesystem::path& exeDir) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    m_settings = settings; m_exeDir = exeDir; m_hInst = hInstance;

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProcStatic; wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // no default erase
    wc.lpszClassName = L"ScreenRecorderWindow";
    if (!RegisterClass(&wc)) return false;

    wchar_t eb[MAX_PATH];
    GetModuleFileNameW(nullptr, eb, MAX_PATH);
    Logger::Instance().Info(std::format("APP: exePath={}", std::filesystem::path(eb).string()));
    Logger::Instance().Info("APP: uiVersion=phase12d-flicker-input-color-fix");

    auto title = std::format(L"ScreenRecorder 录屏助手 {}",
        std::wstring(kAppVersion, kAppVersion + strlen(kAppVersion)));
    m_hwnd = CreateWindowExW(0, L"ScreenRecorderWindow", title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
        nullptr, nullptr, hInstance, this);
    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

    auto bgPath = exeDir / L"assets" / L"background.jpg";
    Logger::Instance().Info(std::format("BG: load result={}", m_bgRenderer.Load(bgPath) ? "success" : "failed"));

    CreateThemeBrushesAndFonts();
    CreateControls(m_hwnd);
    ApplySettingsToUI();
    PopulateSourceList();

    auto capDir = m_settings.savedOutputDir.empty()
        ? (m_exeDir / m_settings.outputDir) : std::filesystem::path(m_settings.savedOutputDir);
    std::filesystem::create_directories(capDir);
    wchar_t ts[64]; SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(ts, L"ScreenRecorder_%04d%02d%02d_%02d%02d%02d.mkv",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    auto outPath = capDir / ts;
    SetWindowTextW(m_hOutputPath, outPath.wstring().c_str());
    m_config.outputPath = outPath.wstring();

    m_hotkeys.Register(m_hwnd, HotkeyManager::ID_START_STOP, HotkeyManager::MOD_CTRL_ALT, 'R');
    m_hotkeys.Register(m_hwnd, HotkeyManager::ID_PAUSE_RESUME, HotkeyManager::MOD_CTRL_ALT, 'P');
    m_controller.SetStatusCallback([this](const RecorderStatus&) {
        PostMessageW(m_hwnd, WM_TIMER, ID_STATUS_TIMER, 0);
    });
    if (!m_controller.Initialize()) {
        MessageBoxW(m_hwnd, L"D3D11 初始化失败", L"错误", MB_ICONERROR);
        return false;
    }
    SetTimer(m_hwnd, ID_STATUS_TIMER, 500, nullptr);
    Logger::Instance().Info("APP: ui init complete — double buffering enabled, input text=black");
    return true;
}

void AppWindow::Show(int cmdShow) { ShowWindow(m_hwnd, cmdShow); }
int AppWindow::Run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}

static HFONT MakeFont(int size, int weight) {
    return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

void AppWindow::CreateThemeBrushesAndFonts() {
    m_bgBrush = CreateSolidBrush(RGB(18, 32, 52));
    m_staticBrush = CreateSolidBrush(RGB(18, 32, 52));
    m_inputBrush = CreateSolidBrush(RGB(232, 236, 242));
    m_fontTitle = MakeFont(20, FW_BOLD);
    m_fontLabel = MakeFont(16, FW_NORMAL);
    m_fontControl = MakeFont(15, FW_NORMAL);
    m_fontButton = MakeFont(16, FW_BOLD);
    m_fontStatus = MakeFont(24, FW_BOLD);
    m_fontSmall = MakeFont(14, FW_NORMAL);
}

void AppWindow::DestroyThemeBrushesAndFonts() {
    auto d = [](void* p) { if (p) DeleteObject(p); };
    d(m_bgBrush); d(m_staticBrush); d(m_inputBrush);
    d(m_fontTitle); d(m_fontLabel); d(m_fontControl);
    d(m_fontButton); d(m_fontStatus); d(m_fontSmall);
    m_bgBrush = m_staticBrush = m_inputBrush = nullptr;
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
        if (m_recording && MessageBoxW(hwnd, L"正在录制中，确定退出？",
            L"录屏助手", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
        m_controller.StopRecording();
        DestroyWindow(hwnd);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        switch (id) {
        case IDC_START:   OnStartRecording(); break;
        case IDC_STOP:    OnStopRecording();  break;
        case IDC_PAUSE:   OnPauseResume();    break;
        case IDC_BROWSE:  OnBrowseOutput();   break;
        case IDC_REFRESH: OnRefreshWindows(); break;
        case IDC_SOURCE:
            if (HIWORD(wp) == CBN_SELCHANGE) { ApplySelectionToConfig(); UpdateWindowInfoText(); }
            break;
        case IDC_BITRATE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                EnableWindow(m_hBitrateVal, (int)SendMessage(m_hBitrate, CB_GETCURSEL, 0, 0) == 3);
            }
            break;
        }
        return 0;
    }

    case WM_HOTKEY: {
        int id = (int)wp;
        if (id == HotkeyManager::ID_START_STOP) {
            if (m_recording) OnStopRecording(); else OnStartRecording();
        } else if (id == HotkeyManager::ID_PAUSE_RESUME) OnPauseResume();
        return 0;
    }

    case WM_TIMER:
        if (wp == ID_STATUS_TIMER) UpdateStatusText();
        return 0;

    case WM_ERASEBKGND:
        return 1; // completely suppress background erase — no flicker

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        // Double buffer: paint to memory DC then BitBlt
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        // Draw background
        m_bgRenderer.Draw(memDC, w, h);

        Gdiplus::Graphics g(memDC);
        Gdiplus::SolidBrush overlay(Gdiplus::Color(80, 0, 0, 0));
        g.FillRectangle(&overlay, Gdiplus::Rect(0, 0, w, h));

        int pw = (std::min)(740, w - 40);
        Gdiplus::SolidBrush panel(Gdiplus::Color(235, 6, 12, 28));
        g.FillRectangle(&panel, Gdiplus::Rect(24, 48, pw, h - 72));

        auto drawCard = [&](int sx, int sy, int sw, int sh, const wchar_t* t) {
            Gdiplus::SolidBrush card(Gdiplus::Color(240, 8, 18, 36));
            g.FillRectangle(&card, Gdiplus::Rect(sx, sy, sw, sh));
            Gdiplus::SolidBrush accent(Gdiplus::Color(200, 45, 140, 255));
            g.FillRectangle(&accent, Gdiplus::Rect(sx, sy, 3, sh));
            Gdiplus::Font f(L"Microsoft YaHei UI", 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush tb(Gdiplus::Color(255, 245, 248, 255));
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
            g.DrawString(t, -1, &f, Gdiplus::PointF((Gdiplus::REAL)(sx + 14), (Gdiplus::REAL)(sy + 7)), &tb);
        };
        drawCard(m_sec.srcX, m_sec.srcY, m_sec.srcW, m_sec.srcH, L"录制源");
        drawCard(m_sec.setX, m_sec.setY, m_sec.setW, m_sec.setH, L"录制设置");
        drawCard(m_sec.actX, m_sec.actY, m_sec.actW, m_sec.actH, L"操作");

        // Status area
        Gdiplus::SolidBrush staBg(Gdiplus::Color(240, 6, 14, 26));
        g.FillRectangle(&staBg, Gdiplus::Rect(m_sec.staX, m_sec.staY, m_sec.staW, m_sec.staH));
        Gdiplus::SolidBrush staAccent(Gdiplus::Color(160, 80, 220, 130));
        g.FillRectangle(&staAccent, Gdiplus::Rect(m_sec.staX, m_sec.staY, 3, m_sec.staH));

        Gdiplus::Font statusFont(L"Microsoft YaHei UI", 18, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font smallFont(L"Microsoft YaHei UI", 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush whiteBr(Gdiplus::Color(255, 245, 248, 255));
        Gdiplus::SolidBrush greenBr(Gdiplus::Color(255, 80, 220, 130));
        Gdiplus::SolidBrush dimBr(Gdiplus::Color(255, 200, 212, 228));

        bool rec = m_statusText.find(L"正在录制") != std::wstring::npos;
        bool ok = m_statusText.find(L"就绪") != std::wstring::npos;
        auto& stBr = (rec || ok) ? greenBr : whiteBr;

        g.DrawString(m_statusText.c_str(), -1, &statusFont,
            Gdiplus::PointF((Gdiplus::REAL)(m_sec.staX + 18), (Gdiplus::REAL)(m_sec.staY + 10)), &stBr);
        g.DrawString(m_durText.c_str(), -1, &smallFont,
            Gdiplus::PointF((Gdiplus::REAL)(m_sec.staX + 18), (Gdiplus::REAL)(m_sec.staY + 50)), &dimBr);
        g.DrawString(m_sizeText.c_str(), -1, &smallFont,
            Gdiplus::PointF((Gdiplus::REAL)(m_sec.staX + 280), (Gdiplus::REAL)(m_sec.staY + 50)), &dimBr);

        // Blit to screen in one shot
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        int cw = LOWORD(lp), ch = HIWORD(lp);
        LayoutControls(cw, ch);
        InvalidateRect(hwnd, nullptr, FALSE); // FALSE = no background erase
        return 0;
    }

    case WM_GETMINMAXINFO: {
        ((MINMAXINFO*)lp)->ptMinTrackSize = { 1000, 650 };
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(245, 248, 255)); // white text on dark bg
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(245, 248, 255)); // white text
        return (LRESULT)m_staticBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(232, 236, 242));   // light gray bg
        SetTextColor(hdc, RGB(0, 0, 0));        // BLACK text
        return (LRESULT)m_inputBrush;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(240, 242, 248));   // light bg
        SetTextColor(hdc, RGB(0, 0, 0));        // BLACK text
        return (LRESULT)m_inputBrush;
    }

    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lp;
        int id = (int)wp;
        bool en = !(dis->itemState & ODS_DISABLED);
        bool pr = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF bg, fg;
        if (id == IDC_START) {
            bg = en ? (pr ? RGB(20, 100, 200) : RGB(35, 130, 255)) : RGB(60, 70, 85);
            fg = RGB(255, 255, 255);
        } else {
            bg = en ? (pr ? RGB(50, 60, 75) : RGB(42, 58, 78)) : RGB(55, 60, 70);
            fg = en ? RGB(220, 228, 240) : RGB(140, 150, 165);
        }

        SetDCBrushColor(dis->hDC, bg);
        SelectObject(dis->hDC, GetStockObject(DC_BRUSH));
        RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
            dis->rcItem.right - 1, dis->rcItem.bottom - 1, 6, 6);

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);
        wchar_t txt[128];
        GetWindowTextW(dis->hwndItem, txt, 128);
        HFONT old = (HFONT)SelectObject(dis->hDC, m_fontButton);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, old);
        return TRUE;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
// LayoutControls
// ============================================================
void AppWindow::LayoutControls(int width, int height) {
    int pw = (std::min)(740, width - 40);
    if (pw < 400) pw = 400;
    int sx = 32, sw = pw - 8, iw = sw - 36;

    // Source
    m_sec.srcX = sx; m_sec.srcY = kPanelY; m_sec.srcW = sw; m_sec.srcH = 124;
    int sy1 = m_sec.srcY, cy1 = sy1 + 36;
    SetWindowPos(m_hSource, nullptr, sx + 18, cy1 - 2, 520, 180, SWP_NOZORDER);
    SetWindowPos(m_hRefresh, nullptr, sx + iw - 100, cy1 - 2, 100, 34, SWP_NOZORDER);
    SetWindowPos(m_hWinInfo, nullptr, sx + 18, cy1 + 40, iw - 20, 22, SWP_NOZORDER);

    // Settings
    m_sec.setX = sx; m_sec.setY = m_sec.srcY + m_sec.srcH + 14;
    m_sec.setW = sw; m_sec.setH = 206;
    int sy2 = m_sec.setY;
    int r1 = sy2 + 36, r2 = r1 + 42, r3 = r2 + 42, r4 = r3 + 42;

    SetWindowPos(m_hFps30, nullptr, sx + 116, r1 - 2, 100, 28, SWP_NOZORDER);
    SetWindowPos(m_hFps60, nullptr, sx + 226, r1 - 2, 100, 28, SWP_NOZORDER);
    SetWindowPos(m_hBitrate, nullptr, sx + 116, r2 - 2, 200, 120, SWP_NOZORDER);
    SetWindowPos(m_hBitrateVal, nullptr, sx + 370, r2 - 2, 100, 30, SWP_NOZORDER);
    SetWindowPos(m_hSysAudio, nullptr, sx + 18, r3, 150, 28, SWP_NOZORDER);
    SetWindowPos(m_hMicAudio, nullptr, sx + 180, r3, 150, 28, SWP_NOZORDER);

    int pathW = iw - 200;
    SetWindowPos(m_hOutputPath, nullptr, sx + 116, r4 - 2, pathW, 30, SWP_NOZORDER);
    SetWindowPos(m_hBrowse, nullptr, sx + 116 + pathW + 8, r4 - 2, 92, 34, SWP_NOZORDER);

    // Actions
    m_sec.actX = sx; m_sec.actY = m_sec.setY + m_sec.setH + 14;
    m_sec.actW = sw; m_sec.actH = 96;
    int sy3 = m_sec.actY, btnY = sy3 + 22;
    int b1 = 200, b2 = 160, b3 = 160, gap = 18, total = b1 + b2 + b3 + gap * 2;
    int bx = sx + 18 + (iw - total) / 2;
    if (bx < sx + 18) bx = sx + 18;
    SetWindowPos(m_hStart, nullptr, bx, btnY, b1, 44, SWP_NOZORDER);
    SetWindowPos(m_hPause, nullptr, bx + b1 + gap, btnY, b2, 44, SWP_NOZORDER);
    SetWindowPos(m_hStop,  nullptr, bx + b1 + b2 + gap * 2, btnY, b3, 44, SWP_NOZORDER);

    // Status — cache rect for partial invalidation
    int staY = (std::max)(m_sec.actY + m_sec.actH + 14, height - 90 - 100);
    m_sec.staX = sx; m_sec.staY = staY; m_sec.staW = sw; m_sec.staH = 100;
    SetRect(&m_statusRect, sx, staY, sx + sw, staY + 100);

    Logger::Instance().Info(std::format("APP: layout {}x{} pw={}", width, height, pw));
}

// ============================================================
// CreateControls
// ============================================================
void AppWindow::CreateControls(HWND hwnd) {
    auto mk = [&](const wchar_t* cls, const wchar_t* t, DWORD s,
                  int x, int y, int w, int h, int id) -> HWND {
        return CreateWindowExW(0, cls, t, s | WS_CHILD | WS_VISIBLE,
            x, y, w, h, hwnd, (HMENU)(INT_PTR)id, m_hInst, nullptr);
    };
    auto setFont = [&](HWND c, HFONT f) { SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); };
    int dx = 0, dy = 0;

    m_hSource = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, dx, dy, 100, 180, IDC_SOURCE);
    setFont(m_hSource, m_fontControl);
    m_hRefresh = mk(L"BUTTON", L"刷新", BS_PUSHBUTTON | WS_TABSTOP, dx, dy, 100, 34, IDC_REFRESH);
    setFont(m_hRefresh, m_fontLabel);
    m_hWinInfo = mk(L"STATIC", L"", SS_LEFT, dx, dy, 100, 22, IDC_WIN_INFO);
    setFont(m_hWinInfo, m_fontSmall);

    m_hFps30 = mk(L"BUTTON", L"30 FPS", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, dx, dy, 100, 28, IDC_FPS_30);
    m_hFps60 = mk(L"BUTTON", L"60 FPS", BS_AUTORADIOBUTTON | WS_TABSTOP, dx, dy, 100, 28, IDC_FPS_60);
    Button_SetCheck(m_hFps30, BST_CHECKED);
    setFont(m_hFps30, m_fontControl); setFont(m_hFps60, m_fontControl);

    m_hBitrate = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, dx, dy, 200, 120, IDC_BITRATE);
    SendMessage(m_hBitrate, CB_ADDSTRING, 0, (LPARAM)L"低 (2500 kbps)");
    SendMessage(m_hBitrate, CB_ADDSTRING, 0, (LPARAM)L"中 (5000 kbps)");
    SendMessage(m_hBitrate, CB_ADDSTRING, 0, (LPARAM)L"高 (10000 kbps)");
    SendMessage(m_hBitrate, CB_ADDSTRING, 0, (LPARAM)L"自定义");
    SendMessage(m_hBitrate, CB_SETCURSEL, 2, 0);
    setFont(m_hBitrate, m_fontControl);
    m_hBitrateVal = mk(L"EDIT", L"10000", ES_NUMBER | WS_TABSTOP, dx, dy, 100, 30, IDC_BITRATE_VAL);
    EnableWindow(m_hBitrateVal, FALSE);
    setFont(m_hBitrateVal, m_fontControl);

    m_hSysAudio = mk(L"BUTTON", L"录制系统声音", BS_AUTOCHECKBOX | WS_TABSTOP, dx, dy, 150, 28, IDC_SYS_AUDIO);
    Button_SetCheck(m_hSysAudio, BST_CHECKED);
    setFont(m_hSysAudio, m_fontLabel);
    m_hMicAudio = mk(L"BUTTON", L"录制麦克风", BS_AUTOCHECKBOX | WS_TABSTOP, dx, dy, 150, 28, IDC_MIC_AUDIO);
    Button_SetCheck(m_hMicAudio, BST_CHECKED);
    setFont(m_hMicAudio, m_fontLabel);

    m_hOutputPath = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, dx, dy, 100, 30, IDC_OUTPUT_PATH);
    setFont(m_hOutputPath, m_fontSmall);
    m_hBrowse = mk(L"BUTTON", L"浏览", BS_PUSHBUTTON | WS_TABSTOP, dx, dy, 92, 34, IDC_BROWSE);
    setFont(m_hBrowse, m_fontLabel);

    // Owner-draw buttons
    m_hStart = mk(L"BUTTON", L"开始录制", BS_OWNERDRAW | WS_TABSTOP, dx, dy, 200, 44, IDC_START);
    m_hPause = mk(L"BUTTON", L"暂停", BS_OWNERDRAW | WS_TABSTOP, dx, dy, 160, 44, IDC_PAUSE);
    m_hStop  = mk(L"BUTTON", L"停止录制", BS_OWNERDRAW | WS_TABSTOP, dx, dy, 160, 44, IDC_STOP);
    EnableWindow(m_hPause, FALSE);
    EnableWindow(m_hStop, FALSE);

    // Dummy status controls (0-size, rendered in WM_PAINT)
    m_hStatus = mk(L"STATIC", L"", SS_LEFT, dx, dy, 0, 0, IDC_STATUS);
    m_hDuration = mk(L"STATIC", L"", SS_LEFT, dx, dy, 0, 0, IDC_DURATION);
    m_hFileSize = mk(L"STATIC", L"", SS_LEFT, dx, dy, 0, 0, IDC_FILESIZE);
}

// ============================================================
void AppWindow::PopulateSourceList() {
    m_sourceItems.clear();
    SendMessage(m_hSource, CB_RESETCONTENT, 0, 0);
    for (auto& m : m_controller.GetMonitors()) {
        auto lb = std::format(L"[M{}] {}x{} {}", m.index, m.width, m.height, m.isPrimary ? L"主显示器" : L"");
        int idx = (int)SendMessage(m_hSource, CB_ADDSTRING, 0, (LPARAM)lb.c_str());
        if (idx >= 0) m_sourceItems.push_back({ SourceItem::Monitor, m.index, nullptr });
    }
    m_winEnum.Refresh();
    for (auto& w : m_winEnum.Enumerate()) {
        auto lb = std::format(L"[窗口] {} - {}", w.executable, w.title.substr(0, 40));
        int idx = (int)SendMessage(m_hSource, CB_ADDSTRING, 0, (LPARAM)lb.c_str());
        if (idx >= 0) m_sourceItems.push_back({ SourceItem::Window, 0, w.hwnd });
    }
    SendMessage(m_hSource, CB_SETCURSEL, 0, 0);
    ApplySelectionToConfig();
    UpdateWindowInfoText();
    RECT rc; GetClientRect(m_hwnd, &rc);
    LayoutControls(rc.right - rc.left, rc.bottom - rc.top);
}

void AppWindow::OnRefreshWindows() { PopulateSourceList(); }

void AppWindow::ApplySettingsToUI() {
    Button_SetCheck(m_hFps60, m_settings.fps >= 60 ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(m_hFps30, m_settings.fps < 60 ? BST_CHECKED : BST_UNCHECKED);
    int br = 2;
    if (m_settings.bitrateKbps <= 2500) br = 0;
    else if (m_settings.bitrateKbps <= 5000) br = 1;
    else if (m_settings.bitrateKbps <= 10000) br = 2;
    else br = 3;
    SendMessage(m_hBitrate, CB_SETCURSEL, br, 0);
    if (br == 3) {
        wchar_t b[32]; swprintf_s(b, L"%d", m_settings.bitrateKbps);
        SetWindowTextW(m_hBitrateVal, b); EnableWindow(m_hBitrateVal, TRUE);
    } else EnableWindow(m_hBitrateVal, FALSE);
    Button_SetCheck(m_hSysAudio, m_settings.recordSystemAudio ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(m_hMicAudio, m_settings.recordMicrophone ? BST_CHECKED : BST_UNCHECKED);
    m_config.fps = m_settings.fps; m_config.bitrateKbps = m_settings.bitrateKbps;
    m_config.captureSysAudio = m_settings.recordSystemAudio;
    m_config.captureMic = m_settings.recordMicrophone;
}

void AppWindow::SaveSettings() {
    if (m_exeDir.empty()) return;
    m_settings.fps = Button_GetCheck(m_hFps60) ? 60 : 30;
    int s = (int)SendMessage(m_hBitrate, CB_GETCURSEL, 0, 0);
    switch (s) {
    case 0: m_settings.bitrateKbps = 2500; break;
    case 1: m_settings.bitrateKbps = 5000; break;
    case 2: m_settings.bitrateKbps = 10000; break;
    default: {
        wchar_t b[32]; GetWindowTextW(m_hBitrateVal, b, 32);
        m_settings.bitrateKbps = _wtoi(b);
        if (m_settings.bitrateKbps < 100) m_settings.bitrateKbps = 5000;
        break;
    }
    }
    m_settings.recordSystemAudio = (Button_GetCheck(m_hSysAudio) == BST_CHECKED);
    m_settings.recordMicrophone = (Button_GetCheck(m_hMicAudio) == BST_CHECKED);
    m_settings.Save(m_exeDir);
}

void AppWindow::ApplySelectionToConfig() {
    int s = (int)SendMessage(m_hSource, CB_GETCURSEL, 0, 0);
    if (s < 0 || s >= (int)m_sourceItems.size()) return;
    auto& item = m_sourceItems[s];
    if (item.type == SourceItem::Monitor) {
        m_config.captureMode = CaptureMode::Monitor;
        m_config.sourceMonitor = item.monitorIndex;
        m_config.targetWindow = nullptr;
    } else {
        m_config.captureMode = CaptureMode::Window;
        m_config.sourceMonitor = 0;
        m_config.targetWindow = item.hwnd;
    }
}

void AppWindow::UpdateWindowInfoText() {
    int s = (int)SendMessage(m_hSource, CB_GETCURSEL, 0, 0);
    if (s < 0 || s >= (int)m_sourceItems.size()) return;
    auto& item = m_sourceItems[s];
    if (item.type == SourceItem::Window && item.hwnd) {
        wchar_t t[128]; GetWindowTextW(item.hwnd, t, 128);
        DWORD pid; GetWindowThreadProcessId(item.hwnd, &pid);
        RECT rc; GetClientRect(item.hwnd, &rc);
        SetWindowTextW(m_hWinInfo, std::format(L"当前窗口：{:p} | {} | {}x{} | PID={}",
            (void*)item.hwnd, t, rc.right - rc.left, rc.bottom - rc.top, pid).c_str());
    } else if (item.type == SourceItem::Monitor) {
        for (auto& m : m_controller.GetMonitors()) {
            if (m.index == item.monitorIndex) {
                SetWindowTextW(m_hWinInfo, std::format(L"当前显示器：{}x{} {}",
                    m.width, m.height, m.isPrimary ? L"主显示器" : L"").c_str());
                break;
            }
        }
    }
}

// ============================================================
void AppWindow::OnStartRecording() {
    ApplySelectionToConfig();
    m_config.fps = Button_GetCheck(m_hFps60) ? 60 : 30;
    if (m_config.captureMode == CaptureMode::Window && m_config.targetWindow) {
        bool v = IsWindow(m_config.targetWindow) != FALSE;
        bool i = IsIconic(m_config.targetWindow) != FALSE;
        bool h = IsWindowVisible(m_config.targetWindow) != FALSE;
        Logger::Instance().Info(std::format("APP: preflight hwnd={:p} isWindow={} iconic={} visible={}",
            (void*)m_config.targetWindow, (int)v, (int)i, (int)h));
        if (!v) { SetStatusText(L"目标窗口不存在，请刷新录制源列表。"); return; }
        if (i)  { SetStatusText(L"目标窗口已最小化，请先还原窗口。"); return; }
        if (!h) { SetStatusText(L"目标窗口不可见，请先显示窗口。"); return; }
    }
    m_config.captureSysAudio = (Button_GetCheck(m_hSysAudio) == BST_CHECKED);
    m_config.captureMic = (Button_GetCheck(m_hMicAudio) == BST_CHECKED);
    int s = (int)SendMessage(m_hBitrate, CB_GETCURSEL, 0, 0);
    switch (s) {
    case 0: m_config.bitrateKbps = 2500; break;
    case 1: m_config.bitrateKbps = 5000; break;
    case 2: m_config.bitrateKbps = 10000; break;
    default: {
        wchar_t b[32]; GetWindowTextW(m_hBitrateVal, b, 32);
        m_config.bitrateKbps = _wtoi(b); if (m_config.bitrateKbps < 100) m_config.bitrateKbps = 5000;
        break;
    }
    }
    wchar_t pb[512]; GetWindowTextW(m_hOutputPath, pb, 512);
    m_config.outputPath = pb;
    std::filesystem::create_directories(std::filesystem::path(m_config.outputPath).parent_path());
    Logger::Instance().Info(std::format("APP: StartRec mode={} monitor={} hwnd={:p}",
        m_config.captureMode == CaptureMode::Monitor ? "Monitor" : "Window",
        m_config.sourceMonitor, (void*)m_config.targetWindow));
    if (!m_controller.StartRecording(m_config)) return;

    m_recording = true; m_paused = false;
    auto dis = [&](HWND c) { EnableWindow(c, FALSE); };
    dis(m_hSource); dis(m_hRefresh); dis(m_hFps30); dis(m_hFps60);
    dis(m_hBitrate); dis(m_hBitrateVal); dis(m_hSysAudio); dis(m_hMicAudio);
    dis(m_hOutputPath); dis(m_hBrowse);
    EnableWindow(m_hStart, FALSE); EnableWindow(m_hPause, TRUE); EnableWindow(m_hStop, TRUE);
    SetWindowTextW(m_hStart, L"录制中...");
    SetStatusText(L"正在录制");
}

void AppWindow::OnStopRecording() {
    m_controller.StopRecording();
    m_recording = false; m_paused = false;
    auto st = m_controller.GetStatus();
    auto finalPath = m_config.outputPath;
    auto finalSize = st.fileSize;
    auto finalDuration = st.durationMs;

    auto en = [&](HWND c) { EnableWindow(c, TRUE); };
    en(m_hSource); en(m_hRefresh); en(m_hFps30); en(m_hFps60);
    en(m_hBitrate); en(m_hSysAudio); en(m_hMicAudio);
    en(m_hOutputPath); en(m_hBrowse);
    EnableWindow(m_hStart, TRUE); EnableWindow(m_hPause, FALSE); EnableWindow(m_hStop, FALSE);
    SetWindowTextW(m_hStart, L"开始录制");
    SetWindowTextW(m_hPause, L"暂停");
    SaveSettings();

    if (finalSize > 0) {
        int sec = (int)(finalDuration / 1000);
        double mb = finalSize / (1024.0 * 1024.0);
        auto fp = std::filesystem::path(finalPath);
        SetStatusText(std::format(L"已保存：{}  ({:.1f} MB，{:02d}:{:02d}:{:02d})",
            fp.filename().wstring(), mb, sec / 3600, (sec % 3600) / 60, sec % 60));
        if (m_settings.showCompletionPrompt) {
            auto vw = std::wstring(kAppVersion, kAppVersion + strlen(kAppVersion));
            auto msg = std::format(L"录制已保存\n\n文件：{}\n大小：{:.1f} MB\n时长：{:02d}:{:02d}:{:02d}\n\n按「确定」可在保存目录查看。",
                fp.wstring(), mb, sec / 3600, (sec % 3600) / 60, sec % 60);
            if (MessageBoxW(m_hwnd, msg.c_str(),
                std::format(L"录制完成 — ScreenRecorder {}", vw).c_str(),
                MB_OKCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND) == IDCANCEL) {
                ShellExecuteW(nullptr, L"open", L"explorer.exe",
                    std::format(L"/select,\"{}\"", fp.wstring()).c_str(), nullptr, SW_SHOWDEFAULT);
            }
        }
    } else {
        SetStatusText(L"停止（未录制到任何帧）");
    }
    UpdateStatusText();
}

void AppWindow::SetStatusText(const std::wstring& text) {
    m_statusText = L"状态：" + text;
    // Only invalidate status area, not whole window
    if (m_hwnd && m_statusRect.right > m_statusRect.left)
        InvalidateRect(m_hwnd, &m_statusRect, FALSE);
}

void AppWindow::OnPauseResume() {
    if (!m_recording) return;
    if (m_paused) {
        m_controller.ResumeRecording(); m_paused = false;
        SetWindowTextW(m_hPause, L"暂停");
        SetStatusText(L"正在录制");
    } else {
        m_controller.PauseRecording(); m_paused = true;
        SetWindowTextW(m_hPause, L"继续");
        SetStatusText(L"已暂停");
    }
}

void AppWindow::OnBrowseOutput() {
    wchar_t p[512] = {};
    OPENFILENAMEW ofn{ sizeof(ofn), m_hwnd };
    ofn.lpstrFilter = L"MKV 文件 (*.mkv)\0*.mkv\0所有文件\0*.*\0";
    ofn.lpstrFile = p; ofn.nMaxFile = 512;
    ofn.lpstrDefExt = L"mkv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) { SetWindowTextW(m_hOutputPath, p); m_config.outputPath = p; }
}

void AppWindow::UpdateStatusText() {
    auto st = m_controller.GetStatus();
    auto state = m_controller.GetState();
    if (m_recording || m_paused || state != RecordingState::Idle) {
        int sec = (int)(st.durationMs / 1000);
        m_durText = std::format(L"时长：{:02d}:{:02d}:{:02d}", sec / 3600, (sec % 3600) / 60, sec % 60);
        m_sizeText = std::format(L"文件大小：{:.1f} MB", st.fileSize / (1024.0 * 1024.0));
    }
    ScrError e = m_controller.GetLastError();
    if (e != ScrError::Ok && !m_recording) {
        auto msg = ScrErrorToUserMessage(e);
        if (!msg.empty()) {
            m_statusText = L"状态：" + msg;
            switch (e) {
            case ScrError::WindowClosed: case ScrError::DuplicationRecreateFailed:
            case ScrError::EncoderInitFailed: case ScrError::OutputFileCreateFailed:
                MessageBoxW(m_hwnd, msg.c_str(), L"录制错误", MB_OK | MB_ICONWARNING);
                break;
            default: break;
            }
        }
    }
    // Partial invalidation: only status area
    if (m_hwnd && m_statusRect.right > m_statusRect.left)
        InvalidateRect(m_hwnd, &m_statusRect, FALSE);
}
