# ScreenRecorder 0.1.0-beta — Release Checklist

## Build

- [ ] `cmake -B build` — configure succeeds
- [ ] `cmake --build build --config Release` — 0 errors
- [ ] `build/Release/ScreenRecorder.exe` exists, > 100 KB

## Dist Package

- [ ] `scripts/package_release.ps1` — completes without error
- [ ] `scripts/check_dist.ps1` — 17/17 PASS
- [ ] `scripts/make_zip_release.ps1` — creates zip, validation PASS
- [ ] FFmpeg DLLs (5) included
- [ ] `config/settings.json` exists and valid
- [ ] `README.md` / `VERSION.txt` included

## Installer

- [ ] `scripts/build_installer.ps1` — completes (or SKIPPED with clear reason)
- [ ] Installer exe exists in `dist/`
- [ ] Installer smoke test: install → launch → record → play

## Smoke Test (Manual)

- [ ] **D1**: Monitor recording 5 s — PASS
- [ ] **D2**: Notepad window recording 5 s — PASS
- [ ] **D3**: Edge/Chrome window recording 5 s — PASS
- [ ] **D4**: Audio present in output file (ffprobe)
- [ ] **D5**: MKV plays in VLC / Media Player
- [ ] **D6**: `logs/app.log` contains version / config loaded / RT: started / RT: done

## Docs

- [ ] `README.md` — complete and accurate
- [ ] `docs/known-limitations.md` — up to date
- [ ] `docs/beta-test-guide.md` — clear instructions for testers
- [ ] `docs/bug-report-template.md` — ready for issue submissions
- [ ] `docs/release-checklist.md` — this file, up to date
- [ ] `docs/security-notes.md` — antivirus / SmartScreen notes

## Version

- [ ] `VERSION.txt` — 0.1.0-beta
- [ ] Window title shows `ScreenRecorder 0.1.0-beta`
- [ ] `app.log` starts with `=== ScreenRecorder 0.1.0-beta ===`
- [ ] `README.md` version matches

## Decision

- [ ] Ready for limited external testing
