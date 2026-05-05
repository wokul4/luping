# ScreenRecorder - Task Plan

## 项目概述
Windows 10/11 x64 桌面录屏应用。C++20 + CMake + Direct3D 11 + Desktop Duplication API。

## 阶段划分

### Phase 1 ✅
D3D11 初始化 + Desktop Duplication 帧捕获 + 显示器枚举 + PNG 验证

### Phase 2 ✅
H.264 编码 + MKV 输出 (FFmpeg/libavcodec/libx264)

### Phase 3 ✅
WASAPI 音频采集 + AAC 编码 + 音视频同步到 MKV

### Phase 4 ✅
Win32 窗口 + 录制控制器 + 托盘图标 + 全局快捷键

### Phase 5 ✅
游戏录制模式 — 技术调研 + 窗口枚举 + GameCaptureSource 接口 + fallback 设计

### Phase 6 ✅
Source Picker UI + Window Recording Stabilization

### Phase 7 ✅
Recording Reliability & Output Stability — ErrorCode 体系 + 固定输出 + 最小化处理 + ACCESS_LOST 恢复 + 状态机

### Phase 8（待定）
WinUI 3 现代化 UI / 其他增强
Graphics Capture API (Windows.Graphics.Capture) 补充 DD fallback

### Phase 6
libobs 集成（游戏捕获扩展）

### Phase 7
WinUI 3 现代化 UI（可选）
