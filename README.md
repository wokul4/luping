# ScreenRecorder

A lightweight Windows screen recorder for Windows 10/11 x64.

**Version:** 0.1.0-beta

## Features

- **Display recording** — record any connected monitor
- **Window recording** — record a specific application window
- **System audio** — capture system audio output (WASAPI loopback)
- **Microphone** — capture microphone input
- **H.264 video** + **AAC audio**
- **MKV output** with automatic file naming
- **Fixed FPS output** — DDA timeout frames are repeated to maintain
  target frame rate
- **Minimized window handling** — repeats last frame or writes black
- **Window close detection** — auto-stops when target window closes

## Requirements

- Windows 10 or Windows 11 (x64)

## Usage

1. Launch `ScreenRecorder.exe`
2. Select a capture source:
   - **Monitor** — choose from `[M0]`, `[M1]`, etc.
   - **Window** — choose from `[W] process-name — window title`
3. Configure FPS (30 / 60) and bitrate
4. Click **Start Rec** (or press Ctrl+Alt+R)
5. Recording begins; status shows elapsed time and file size
6. Click **Stop** (or press Ctrl+Alt+R again)
7. Completion dialog shows file path, size, and duration
8. Click **Cancel** on the dialog to open the file in Explorer

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Alt+R | Start / Stop recording |
| Ctrl+Alt+P | Pause / Resume recording |

## Output Files

Recordings are saved to `captures/` (next to the executable) with names
like `ScreenRecorder_20260505_183012.mkv`.

## Building from Source

### Prerequisites

- Visual Studio 2022 (or Build Tools) with C++20 support
- CMake 3.20+
- FFmpeg SDK (include/lib for development)

### Build Steps

```powershell
cd D:/luping
cmake --build build --config Release
```

Output: `build/Release/ScreenRecorder.exe`

### Package for Distribution

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 `
    -FFmpegBin D:/ffmpeg/bin -Config Release
```

Output: `dist/ScreenRecorder/`

### Verify Package

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check_dist.ps1
```

## Dependencies

- **FFmpeg** (LGPL/GPL) — video/audio encoding + muxing
  (`avformat`, `avcodec`, `avutil`, `swscale`, `swresample`)
- **Windows SDK** — D3D11, DXGI, WASAPI, Desktop Duplication API

Licenses for FFmpeg DLLs are included in the `dist/` package.

## Known Limitations

See [docs/known-limitations.md](docs/known-limitations.md).

- Window capture uses DDA + region crop (not true per-window capture)
- Occluded windows may show desktop content behind the window
- Exclusive full-screen games may cause ACCESS_LOST
- Anti-cheat protected games are likely to reject DDA capture
- HDR colour reproduction is not managed
- Not an OBS replacement

## Disclaimer

This is a beta-quality tool. Recording in production or mission-critical
environments is not recommended. Game capture compatibility is not
guaranteed. Use at your own risk.
