# ScreenRecorder 录屏助手

一款轻量级 Windows 屏幕录制工具，适用于 Windows 10/11 x64。

**当前版本：** 0.1.0-beta

---

## 主要功能

- **显示器录制** — 选择任意显示器进行全屏录制
- **窗口录制** — 录制指定应用程序窗口区域
- **系统声音** — 捕获系统音频输出（WASAPI 回环）
- **麦克风** — 捕获麦克风输入
- **H.264 视频** + **AAC 音频**
- **MKV 输出**，自动生成文件名
- **固定帧率输出** — DDA 超时时自动重复上一帧，保证帧率稳定
- **最小化窗口处理** — 重复上一帧或输出黑帧
- **窗口关闭检测** — 目标窗口关闭时自动停止录制

## 使用方法

1. 从 `dist/ScreenRecorder_0.1.0-beta.zip` 解压，或安装安装包
2. 双击 `ScreenRecorder.exe` 启动
3. 选择录制源：
   - **显示器** — 选择 `[M0] 主显示器 1920x1080`
   - **窗口** — 选择 `[窗口] notepad.exe - ...`
4. 设置帧率（30 / 60 FPS）和码率
5. 点击「开始录制」（或按 Ctrl+Alt+R）
6. 开始录制后状态栏显示时长和文件大小
7. 点击「停止录制」（或再次按 Ctrl+Alt+R）
8. 弹出录制完成提示，可按「打开文件夹」查看文件

## 键盘快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl+Alt+R | 开始 / 停止录制 |
| Ctrl+Alt+P | 暂停 / 继续录制 |

## 输出文件

录制文件保存在 `captures/` 目录（在 exe 所在目录下），文件名为：
`ScreenRecorder_20260505_183012.mkv`

## 安装方式

### Zip 包

解压后直接运行 `ScreenRecorder.exe`。

### 安装包（Inno Setup）

运行 `ScreenRecorderSetup_0.1.0-beta.exe`，按提示安装。

## 系统要求

- Windows 10 x64 或 Windows 11 x64
- FFmpeg DLL（已包含在发布包中）

## 已知限制

详见 [docs/known-limitations.md](docs/known-limitations.md)。

- 窗口录制使用 DDA + 区域裁剪，非真正逐窗口捕获
- 窗口被遮挡时可能录到遮挡画面
- 独占全屏游戏可能导致 ACCESS_LOST
- 反作弊游戏很可能拒绝 DDA 捕获
- HDR 色彩未做转换处理
- **本工具不是 OBS 替代品**

## 测试反馈

如果您参与外部测试，请参阅：

- [外部测试指南](docs/beta-test-guide.md)
- [Bug 报告模板](docs/bug-report-template.md)
- [已知限制](docs/known-limitations.md)

日志文件位置：`logs/app.log`（在 exe 所在目录下）

## 开发者构建

### 前置条件

- Visual Studio 2022（或 Build Tools）with C++20 支持
- CMake 3.20+
- FFmpeg SDK（需设置 `FFMPEG_ROOT` 环境变量或 CMake 变量）

### 构建步骤

```powershell
cd D:/luping
cmake -B build
cmake --build build --config Release
```

输出：`build/Release/ScreenRecorder.exe`

### 发布打包

```powershell
# 创建 dist/ScreenRecorder/
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 `
    -FFmpegBin D:/ffmpeg/bin -Config Release

# 验证 dist
powershell -ExecutionPolicy Bypass -File scripts/check_dist.ps1

# 生成 zip
powershell -ExecutionPolicy Bypass -File scripts/make_zip_release.ps1 -Version 0.1.0-beta
```

## 依赖

- **FFmpeg**（LGPL/GPL）— 视频/音频编码和封装
  - `avformat`、`avcodec`、`avutil`、`swscale`、`swresample`
- **Windows SDK** — D3D11、DXGI、WASAPI、Desktop Duplication API

## 免责声明

此为 Beta 版本软件，不保证在所有环境中稳定运行。在关键任务或生产环境中使用请自行承担风险。游戏录制兼容性不做保证。

---

## Building from Source (English)

See the "Developer Build" section above. All source code is in the `app/`, `core/`, `capture/`, `encoder/`, `audio/`, and `platform/` directories.
