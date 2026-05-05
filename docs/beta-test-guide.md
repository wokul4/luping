# ScreenRecorder 0.1.0-beta — Beta Test Guide

## Version

**ScreenRecorder 0.1.0-beta**

## Supported Systems

- Windows 10 x64
- Windows 11 x64

## How to Install

### Zip Version

1. Download `ScreenRecorder_0.1.0-beta.zip`
2. Extract to any folder (e.g. `C:\ScreenRecorder\`)
3. Run `ScreenRecorder.exe`

### Installer Version

1. Download `ScreenRecorderSetup_0.1.0-beta.exe`
2. Run the installer
3. Follow on-screen instructions
4. Launch from Start Menu or Desktop shortcut

## How to Record a Monitor

1. Launch ScreenRecorder
2. Source dropdown defaults to `[M0]` (primary monitor)
3. Click **Start Rec** (or press Ctrl+Alt+R)
4. Wait a few seconds
5. Click **Stop** (or press Ctrl+Alt+R again)
6. A dialog shows the saved file info — click **Cancel** to open the folder

## How to Record a Window

1. Launch ScreenRecorder
2. Click **Refresh** to populate the window list
3. Select a window entry like `[W] notepad.exe — ...`
4. Click **Start Rec**
5. The recorder captures only the window region
6. Click **Stop** to finish

## How to Confirm Recording Success

1. Check that `captures/` contains a `.mkv` file
2. The file name format is `ScreenRecorder_YYYYMMDD_HHMMSS.mkv`
3. Open the file in any video player (VLC, Media Player, etc.)
4. Verify video and audio play correctly

## Where to Find Logs

Logs are at `logs/app.log` (next to ScreenRecorder.exe).

Include this file when reporting bugs.

## What to Test

1. **Can you launch the app?** — Does it start without errors?
2. **Monitor recording** — Does it record the full screen?
3. **Window recording** — Does it record a specific window (Notepad, browser, etc.)?
4. **Audio** — Does the output file have sound? (System audio / microphone)
5. **Performance** — Does it lag or stutter?
6. **Output playback** — Can you play the MKV file?
7. **Browser windows** — Does Chrome/Edge recording work?
8. **Full-screen / games** — Does the recorder work with full-screen apps? (May fail)
9. **Multiple start/stop** — Can you record multiple times without restarting?

## Known Limitations

- **Not a professional game capture tool.** Window recording uses Desktop Duplication API (DDA) with region cropping, not per-window buffer capture.
- **Exclusive full-screen games** may cause ACCESS_LOST errors. The recorder attempts recovery but may fail.
- **Anti-cheat protected games** (EAC, BattlEye, Vanguard, etc.) are likely to block DDA capture.
- **Occluded windows**: When the target window is partially covered by other windows, the captured frame shows whatever is on the desktop behind it.
- **Minimized windows**: The recorder repeats the last frame or writes black frames until the window is restored.
- **HDR displays**: Colour may appear washed out.
- **High refresh rate (≥144 Hz)**: Output is capped at the configured FPS.

## How to Submit a Bug Report

1. Do not close the app after the bug
2. Copy the log file (`logs/app.log`)
3. Fill out the bug report template (see `docs/bug-report-template.md`)
4. Send both the log and the filled template to the developer
5. Do not upload sensitive video content
