#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "../core/RecorderController.h"
#include "../core/RecordingConfig.h"
#include "../core/AppSettings.h"
#include "../capture/GameCaptureSource.h"
#include "../capture/GameWindowEnumerator.h"
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
    void PopulateSourceList();
    void ApplySelectionToConfig();
    void UpdateWindowInfoText();

    void OnStartRecording();
    void OnStopRecording();
    void OnPauseResume();
    void OnBrowseOutput();
    void OnRefreshWindows();
    void UpdateStatusText();
    void SaveSettings();

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
    void ReadUItoConfig();

    RecorderController m_controller;
    HotkeyManager      m_hotkeys;
    RecordingConfig    m_config;
    AppSettings        m_settings;
    std::filesystem::path m_exeDir;

    bool m_recording = false;
    bool m_paused    = false;
};
