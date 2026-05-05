# ScreenRecorder — Security & Antivirus Notes

## Current Build Status

ScreenRecorder 0.1.0-beta uses standard Windows APIs:

- **Desktop Duplication API (DXGI)** — for screen capture
- **WASAPI** — for audio capture
- **Direct3D 11** — for GPU operations
- **FFmpeg** — for video/audio encoding

## What This Software Does NOT Do

- Does NOT inject code into other processes
- Does NOT use graphics-hook or Detour hooks
- Does NOT install kernel drivers
- Does NOT collect user data or telemetry
- Does NOT require network access for recording
- Does NOT modify system files or registry beyond install/uninstall

## Antivirus / SmartScreen Warnings

- The executable and installer are **not code-signed**
- Windows SmartScreen may show a warning because the app is:
  - unsigned
  - new / low download count
- Some antivirus software may flag the FFmpeg DLLs heuristically
  - FFmpeg is a widely used open-source multimedia library
  - The DLLs are standard FFmpeg 7.1 builds

## Recommendations

- **Download only from official sources** — do not use redistributed copies
- To reduce warnings in future releases, the project should obtain a code signing certificate
- If SmartScreen appears, click "More info" → "Run anyway"
- Add the `dist/` directory to antivirus exclusions if false positives occur

## Reporting Security Issues

If you believe you've found a security issue, please report it privately to the developer. Do not file a public issue.
