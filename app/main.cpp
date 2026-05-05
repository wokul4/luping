#include <windows.h>
#include <filesystem>
#include "platform/Logger.h"
#include "core/AppSettings.h"
#include "app/AppWindow.h"

const char* kAppVersion = "0.1.0-beta";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int cmdShow) {
    // Use exe-relative paths so it works from any working directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    auto exeDir = std::filesystem::path(exePath).parent_path();

    std::filesystem::create_directories(exeDir / "logs");
    std::filesystem::create_directories(exeDir / "captures");

    // Init logger
    Logger::Instance().Init((exeDir / "logs" / "app.log").string());
    Logger::Instance().Info(std::format("=== ScreenRecorder {} ===", kAppVersion));

    // Load settings
    AppSettings settings;
    settings.LoadOrCreate(exeDir);

    // COM for main thread (needed for WIC and some shell dialogs)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxA(nullptr, "COM init failed", "Error", MB_ICONERROR);
        return 1;
    }

    AppWindow window;
    if (!window.Create(hInstance, settings, exeDir)) {
        MessageBoxA(nullptr, "Window creation failed", "Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    window.Show(cmdShow);
    int ret = window.Run();

    CoUninitialize();
    return ret;
}
