#include "BackgroundRenderer.h"
#include "../platform/Logger.h"
#include <format>

// Gdiplus headers & linkage
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

BackgroundRenderer::BackgroundRenderer() {
    GdiplusStartupInput in;
    if (GdiplusStartup(&m_gdiplusToken, &in, nullptr) == Ok) {
        m_gdiplusStarted = true;
    } else {
        Logger::Instance().Warning("BG: GdiplusStartup failed");
    }
}

BackgroundRenderer::~BackgroundRenderer() {
    if (m_image) {
        delete static_cast<Image*>(m_image);
        m_image = nullptr;
    }
    if (m_gdiplusStarted) {
        GdiplusShutdown(m_gdiplusToken);
    }
}

bool BackgroundRenderer::Load(const std::filesystem::path& imagePath) {
    if (!m_gdiplusStarted) return false;

    auto path = imagePath.wstring();
    auto* img = new Image(path.c_str());
    if (img->GetLastStatus() != Ok) {
        delete img;
        Logger::Instance().Warning(std::format("BG: failed to load {}", imagePath.string()));
        return false;
    }

    if (m_image) delete static_cast<Image*>(m_image);
    m_image = img;
    Logger::Instance().Info(std::format("BG: loaded {} ({}x{})",
        imagePath.string(),
        img->GetWidth(), img->GetHeight()));
    return true;
}

void BackgroundRenderer::Draw(HDC hdc, int clientW, int clientH) {
    if (!m_image || !m_gdiplusStarted) {
        DrawFallback(hdc, clientW, clientH);
        return;
    }

    auto* img = static_cast<Image*>(m_image);
    int imgW = (int)img->GetWidth();
    int imgH = (int)img->GetHeight();
    if (imgW <= 0 || imgH <= 0) {
        DrawFallback(hdc, clientW, clientH);
        return;
    }

    // Center-crop: scale so the image covers the entire area
    float scaleX = (float)clientW / imgW;
    float scaleY = (float)clientH / imgH;
    float scale = max(scaleX, scaleY);

    int drawW = (int)(imgW * scale);
    int drawH = (int)(imgH * scale);
    int drawX = (clientW - drawW) / 2;
    int drawY = (clientH - drawH) / 2;

    Graphics g(hdc);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(img, drawX, drawY, drawW, drawH);
}

void BackgroundRenderer::DrawFallback(HDC hdc, int clientW, int clientH) {
    // Dark blue-black gradient fallback
    Graphics g(hdc);
    Color c1(30, 20, 50);   // dark blue
    Color c2(10, 10, 20);   // near black
    Rect r(0, 0, clientW, clientH);

    // Simple top-to-bottom gradient via two rects
    // Upper 60%: dark blue
    // Lower 40%: black
    int midY = clientH * 60 / 100;
    SolidBrush b1(c1);
    SolidBrush b2(c2);
    g.FillRectangle(&b1, Rect(0, 0, clientW, midY));
    g.FillRectangle(&b2, Rect(0, midY, clientW, clientH - midY));
}
