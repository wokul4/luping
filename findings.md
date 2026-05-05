# ScreenRecorder - Findings

## 技术调研

### Desktop Duplication API
- Windows 8+ 可用，Win10/Win11 稳定
- 需要 IDXGIOutputDuplication 接口
- 通过 DuplicateOutput 获得帧，需要 ReleaseFrame 释放
- 帧格式为 DXGI_FORMAT_B8G8R8A8_UNORM
- 支持获取移动区域 (MoveRects) 和脏区域 (DirtyRects)，优化编码用

### D3D11 初始化
- 用 D3D11CreateDevice 创建 device/context
- 需要 IDXGIDevice/IDXGIAdapter/IDXGIOutput 链来获取输出
- 创建 Texture2D 用于 staging copy (CPU 可读)

### PNG 保存
- WIC (Windows Imaging Component) 可以从 ID3D11Texture2D 编码 PNG
- 需要 ID2D1DeviceContext + IWICBitmap 做转换
- 或者直接用 staging texture → system memory → stb_image_write

### CMake + Visual Studio 2022
- 需要 CMAKE_MINIMUM_REQUIRED 3.20+
- D3D11 库: d3d11.lib, dxgi.lib, dxguid.lib
- WIC 库: windowscodecs.lib
