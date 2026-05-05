# ScreenRecorder 0.1.0-beta — Known Limitations

## Current Window Capture Uses DDA + Region Crop

Window recording uses Desktop Duplication API (DDA) to capture the monitor,
then crops to the target window's region. This is **not** true per-window
game capture.

## Occluded Windows

When the target window is partially or fully occluded by other windows,
the captured frame will show whatever is on the desktop behind the window.
DDA captures monitor pixels, not application buffers.

## Minimized Windows

When the target window is minimized, DDA has no frame content for it.
The recorder repeats the last captured frame or writes black frames.
No new window content is captured until the window is restored.

## Exclusive Full-Screen Games

DDA may produce ACCESS_LOST when an application switches to exclusive
full-screen mode (e.g., many DirectX games). The recorder attempts
automatic recovery with repeated frames, but may fail if the display
mode change is permanent.

## Anti-Cheat Protected Games

Anti-cheat systems (EAC, BattlEye, Vanguard, etc.) may block DDA capture,
resulting in black frames or ACCESS_LOST. This is by design on the
anti-cheat side and cannot be worked around without elevated hooks.

## HDR / High Refresh Rate / Multi-GPU

- HDR displays: Recording colour may appear washed out or over-saturated.
- High refresh rate (≥144 Hz): FPS is capped at the configured rate
  (30 or 60). Display changes between frame slots are dropped.
- Multi-GPU laptops: DDA may not capture the correct adapter on
  hybrid graphics systems. Running ScreenRecorder on the high-performance
  GPU is recommended.

## General Disclaimer

ScreenRecorder is **not** an OBS replacement. It is a lightweight,
proof-of-concept screen capture tool. For professional streaming or
game capture, use OBS Studio, Nvidia ShadowPlay, or similar.

## Future Work (Not Committed)

- True per-window capture via Windows Graphics Capture API
- Game capture via libobs / graphics-hook integration
- HDR colour space conversion
- GPU encoder selection (NVENC / AMF / QSV)
