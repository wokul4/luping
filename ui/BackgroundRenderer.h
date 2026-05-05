#pragma once
#include <windows.h>
#include <string>
#include <filesystem>

class BackgroundRenderer {
public:
    BackgroundRenderer();
    ~BackgroundRenderer();

    // Load image — returns true on success, false = use fallback
    bool Load(const std::filesystem::path& imagePath);

    // Draw onto HDC, fitting the client rect with center-crop
    void Draw(HDC hdc, int clientW, int clientH);

    // Draw fallback dark gradient if image unavailable
    void DrawFallback(HDC hdc, int clientW, int clientH);

    bool IsLoaded() const { return m_image != nullptr; }

private:
    void* m_image = nullptr; // Gdiplus::Image*
    ULONG_PTR m_gdiplusToken = 0;
    bool m_gdiplusStarted = false;
};
