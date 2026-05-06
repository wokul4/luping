#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "../core/RecorderController.h"
#include "../core/RecordingConfig.h"
#include "../core/AppSettings.h"
#include "../capture/GameCaptureSource.h"
#include "../capture/GameWindowEnumerator.h"
#include "../ui/BackgroundRenderer.h"
#include "HotkeyManager.h"

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    bool Create(HINSTANCE hInstance, const AppSettings& settings, const std::filesystem::path& exeDir);
    void Show(int cmdShow);
    int  Run();

private:
    struct SourceItem {
        enum Type { Monitor, Window };
        Type type;
        int  monitorIndex = 0;
        HWND hwnd         = nullptr;
    };

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void CreateControls(HWND hwnd);
    void LayoutControls(int width, int height);
    void PopulateSourceList();
    void ApplySelectionToConfig();
    void UpdateWindowInfoText();

    void OnStartRecording();
    void OnStopRecording();
    void OnPauseResume();
    void OnBrowseOutput();
    void OnRefreshWindows();
    void UpdateStatusText();
    void SetStatusText(const std::wstring& text);
    void SaveSettings();

    void CreateThemeBrushesAndFonts();
    void DestroyThemeBrushesAndFonts();

    // Fixed layout constants
    static constexpr int kPanelX = 32;
    static constexpr int kPanelY = 56;
    static constexpr int kPanelW = 740;
    static constexpr int kInnerPad = 20;
    static constexpr int kLabelW = 90;
    static constexpr int kRowH = 36;
    static constexpr int kSectionGap = 18;

    // Section positions (computed in LayoutControls)
    struct SectionRects {
        int srcX, srcY, srcW, srcH;
        int setX, setY, setW, setH;
        int actX, actY, actW, actH;
        int staX, staY, staW, staH;
    } m_sec{};

    enum CtrlId {
        IDC_SOURCE      = 101,
        IDC_REFRESH     = 102,
        IDC_WIN_INFO    = 103,
        IDC_FPS_30      = 104,
        IDC_FPS_60      = 105,
        IDC_BITRATE     = 106,
        IDC_BITRATE_VAL = 107,
        IDC_SYS_AUDIO   = 108,
        IDC_MIC_AUDIO   = 109,
        IDC_OUTPUT_PATH = 110,
        IDC_BROWSE      = 111,
        IDC_START       = 112,
        IDC_PAUSE       = 113,
        IDC_STOP        = 114,
        IDC_DURATION    = 115,
        IDC_FILESIZE    = 116,
        IDC_STATUS      = 117,
        ID_STATUS_TIMER = 201,
    };

    HINSTANCE m_hInst  = nullptr;
    HWND      m_hwnd   = nullptr;

    // Controls
    HWND m_hSource      = nullptr;
    HWND m_hRefresh     = nullptr;
    HWND m_hWinInfo     = nullptr;
    HWND m_hFps30       = nullptr;
    HWND m_hFps60       = nullptr;
    HWND m_hBitrate     = nullptr;
    HWND m_hBitrateVal  = nullptr;
    HWND m_hSysAudio    = nullptr;
    HWND m_hMicAudio    = nullptr;
    HWND m_hOutputPath  = nullptr;
    HWND m_hBrowse      = nullptr;
    HWND m_hStart       = nullptr;
    HWND m_hPause       = nullptr;
    HWND m_hStop        = nullptr;
    HWND m_hDuration    = nullptr;
    HWND m_hFileSize    = nullptr;
    HWND m_hStatus      = nullptr;

    // Source tracking
    std::vector<SourceItem> m_sourceItems;
    GameWindowEnumerator    m_winEnum;

    void ApplySettingsToUI();

    RecorderController m_controller;
    HotkeyManager      m_hotkeys;
    RecordingConfig    m_config;
    AppSettings        m_settings;
    std::filesystem::path m_exeDir;

    bool m_recording = false;
    bool m_paused    = false;

    BackgroundRenderer m_bgRenderer;

    // Brushes
    HBRUSH m_bgBrush       = nullptr;
    HBRUSH m_staticBrush   = nullptr;
    HBRUSH m_inputBrush    = nullptr; // light gray for edit/combo
    HBRUSH m_winInfoBrush  = nullptr; // dark bg for info bar

    // Status text (drawn in WM_PAINT, not white static controls)
    std::wstring m_statusText  = L"状态：就绪";
    std::wstring m_durText     = L"时长：00:00:00";
    std::wstring m_sizeText    = L"文件大小：0.0 MB";
    RECT m_statusRect = {};   // cached status area rect for partial invalidation
    RECT m_winInfoRect = {};  // info bar rect, drawn in WM_PAINT
    std::wstring m_winInfoText; // info bar text, drawn in WM_PAINT

    // Fonts
    HFONT m_fontTitle    = nullptr; // 22pt bold for section titles
    HFONT m_fontLabel    = nullptr; // 18pt for labels
    HFONT m_fontControl  = nullptr; // 17pt for controls
    HFONT m_fontButton   = nullptr; // 18pt bold for buttons
    HFONT m_fontStatus   = nullptr; // 28pt bold for status text
    HFONT m_fontSmall    = nullptr; // 15pt for details

    // Colors
    static constexpr COLORREF cText       = RGB(245, 248, 255);
    static constexpr COLORREF cTextDim    = RGB(195, 208, 230);
    static constexpr COLORREF cTextDis    = RGB(140, 150, 165);
    static constexpr COLORREF cBlue       = RGB(45, 140, 255);
    static constexpr COLORREF cGreen      = RGB(80, 220, 130);
    static constexpr COLORREF cRed        = RGB(240, 90, 90);
    static constexpr COLORREF cPanelBg    = RGB(12, 22, 38);
    static constexpr COLORREF cSectionBg  = RGB(18, 32, 52);
    static constexpr COLORREF cBorder     = RGB(70, 105, 150);
};
