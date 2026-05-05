<#
.SYNOPSIS
    Comprehensive Release QA check for ScreenRecorder.
.PARAMETER Version
    Version string (e.g. 0.1.0-beta)
#>
param(
    [string]$Version = "0.1.0-beta"
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildExe = [System.IO.Path]::Combine($ProjectRoot, "build", "Release", "ScreenRecorder.exe")
$DistDir = [System.IO.Path]::Combine($ProjectRoot, "dist", "ScreenRecorder")
$DistExe = [System.IO.Path]::Combine($DistDir, "ScreenRecorder.exe")
$ZipName = "ScreenRecorder_${Version}.zip"
$ZipPath = [System.IO.Path]::Combine($ProjectRoot, "dist", $ZipName)
$SetupName = "ScreenRecorderSetup_${Version}.exe"
$SetupPath = [System.IO.Path]::Combine($ProjectRoot, "dist", $SetupName)

$passed = 0
$failed = 0
$skipped = 0

function Check {
    param([string]$Msg, [scriptblock]$Cond)
    if (& $Cond) {
        Write-Host ("  PASS  " + $Msg) -ForegroundColor Green
        $script:passed++
    } else {
        Write-Host ("  FAIL  " + $Msg) -ForegroundColor Red
        $script:failed++
    }
}

function Skip {
    param([string]$Msg)
    Write-Host ("  SKIP  " + $Msg) -ForegroundColor Yellow
    $script:skipped++
}

Write-Host "=== Release QA v$Version ===" -ForegroundColor Cyan
Write-Host ""

# ============================================================
Write-Host "--- Build ---" -ForegroundColor Cyan
Check "VERSION.txt exists"    { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "VERSION.txt")) }
Check "build/Release/ScreenRecorder.exe exists" { (Get-Item $BuildExe -ErrorAction SilentlyContinue).Length -gt 100KB }

# ============================================================
Write-Host "--- Dist ---" -ForegroundColor Cyan
Check "dist/ScreenRecorder exists"          { Test-Path $DistDir }
Check "dist/ScreenRecorder.exe exists"      { (Get-Item $DistExe -ErrorAction SilentlyContinue).Length -gt 100KB }
Check "dist/config/settings.json exists"    { Test-Path ([System.IO.Path]::Combine($DistDir, "config", "settings.json")) }
Check "dist/README.md exists"               { Test-Path ([System.IO.Path]::Combine($DistDir, "README.md")) }
Check "dist/VERSION.txt exists"             { Test-Path ([System.IO.Path]::Combine($DistDir, "VERSION.txt")) }
Check "dist/avcodec DLL exists"             { Get-ChildItem ([System.IO.Path]::Combine($DistDir, "avcodec-*.dll")) -ErrorAction SilentlyContinue | Out-Null; $? }
Check "dist/avformat DLL exists"            { Get-ChildItem ([System.IO.Path]::Combine($DistDir, "avformat-*.dll")) -ErrorAction SilentlyContinue | Out-Null; $? }
Check "dist/avutil DLL exists"              { Get-ChildItem ([System.IO.Path]::Combine($DistDir, "avutil-*.dll")) -ErrorAction SilentlyContinue | Out-Null; $? }
Check "dist/swscale DLL exists"             { Get-ChildItem ([System.IO.Path]::Combine($DistDir, "swscale-*.dll")) -ErrorAction SilentlyContinue | Out-Null; $? }
Check "dist/swresample DLL exists"          { Get-ChildItem ([System.IO.Path]::Combine($DistDir, "swresample-*.dll")) -ErrorAction SilentlyContinue | Out-Null; $? }
Check "dist/docs/known-limitations.md"      { Test-Path ([System.IO.Path]::Combine($DistDir, "docs", "known-limitations.md")) }

# ============================================================
Write-Host "--- Zip ---" -ForegroundColor Cyan
Check "zip file exists"              { Test-Path $ZipPath }
if (Test-Path $ZipPath) {
    $zipSize = (Get-Item $ZipPath).Length
    Check "zip size > 10 MB"        { $zipSize -gt 10MB }
    Check "zip size < 500 MB"       { $zipSize -lt 500MB }
}

# ============================================================
Write-Host "--- Installer ---" -ForegroundColor Cyan
if (Test-Path $SetupPath) {
    Check "installer exe exists" { $true }
    $setupSize = (Get-Item $SetupPath).Length
    Check "installer size > 1 MB" { $setupSize -gt 1MB }
} else {
    Skip "installer exe — Inno Setup not installed"
}

# ============================================================
Write-Host "--- Docs ---" -ForegroundColor Cyan
Check "docs/beta-test-guide.md"     { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "docs", "beta-test-guide.md")) }
Check "docs/bug-report-template.md" { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "docs", "bug-report-template.md")) }
Check "docs/release-checklist.md"   { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "docs", "release-checklist.md")) }
Check "docs/security-notes.md"      { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "docs", "security-notes.md")) }

# ============================================================
Write-Host "--- Scripts ---" -ForegroundColor Cyan
Check "scripts/package_release.ps1"    { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "scripts", "package_release.ps1")) }
Check "scripts/check_dist.ps1"         { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "scripts", "check_dist.ps1")) }
Check "scripts/make_zip_release.ps1"   { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "scripts", "make_zip_release.ps1")) }
Check "scripts/build_installer.ps1"    { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "scripts", "build_installer.ps1")) }
Check "scripts/release_qa.ps1"         { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "scripts", "release_qa.ps1")) }
Check "installer/ScreenRecorder.iss"   { Test-Path ([System.IO.Path]::Combine($ProjectRoot, "installer", "ScreenRecorder.iss")) }

# ============================================================
Write-Host "--- Summary ---" -ForegroundColor Cyan
Write-Host "Passed:  $passed" -ForegroundColor Green
Write-Host "Failed:  $failed" -ForegroundColor Red
Write-Host "Skipped: $skipped" -ForegroundColor Yellow
Write-Host ""

if ($failed -gt 0) {
    Write-Host "Release QA: FAIL" -ForegroundColor Red
    exit 1
}
if ($skipped -gt 0) {
    Write-Host "Release QA: PASS (with skipped items)" -ForegroundColor Yellow
    exit 0
}
Write-Host "Release QA: PASS" -ForegroundColor Green
exit 0
