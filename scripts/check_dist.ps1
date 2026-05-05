<#
.SYNOPSIS
    Verify dist/ScreenRecorder/ structure and contents.
.PARAMETER DistDir
    Path to dist directory (default: <project>/dist/ScreenRecorder)
#>
param(
    [string]$DistDir = ""
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $DistDir) { $DistDir = [System.IO.Path]::Combine($ProjectRoot, "dist", "ScreenRecorder") }

$passed = 0
$failed = 0
$warned = 0

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

function Warn {
    param([string]$Msg, [scriptblock]$Cond)
    if (& $Cond) {
        Write-Host ("  PASS  " + $Msg) -ForegroundColor Green
        $script:passed++
    } else {
        Write-Host ("  WARN  " + $Msg) -ForegroundColor Yellow
        $script:warned++
    }
}

Write-Host "=== check_dist.ps1 ===" -ForegroundColor Cyan
Write-Host "Target: $DistDir`n"

# Basic structure
Check "Dist directory exists"        { Test-Path $DistDir }
Check "ScreenRecorder.exe exists"    { (Get-Item (Join-Path $DistDir "ScreenRecorder.exe") -ErrorAction SilentlyContinue).Length -gt 100KB }
Check "config directory exists"      { Test-Path (Join-Path $DistDir "config") }
Check "logs directory exists"        { Test-Path (Join-Path $DistDir "logs") }
Check "captures directory exists"    { Test-Path (Join-Path $DistDir "captures") }

# Config
$settingsPath = [System.IO.Path]::Combine($DistDir, "config", "settings.json")
Check "config/settings.json exists"  { Test-Path $settingsPath }
if (Test-Path $settingsPath) {
    try {
        $json = Get-Content $settingsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        Check "settings.json valid JSON"        { $null -ne $json }
        Check "settings.json has fps"           { $null -ne $json.fps }
        Check "settings.json has bitrateKbps"   { $null -ne $json.bitrateKbps }
    } catch {
        Check "settings.json valid JSON"        { $false }
    }
}

# FFmpeg DLLs
$requiredDlls = @("avcodec", "avformat", "avutil", "swscale", "swresample")
foreach ($dll in $requiredDlls) {
    $found = Get-ChildItem (Join-Path $DistDir "$dll-*.dll") -ErrorAction SilentlyContinue
    Check "FFmpeg DLL: $dll" { $found -and $found.Count -gt 0 }
}

# Docs
Check "README.md exists"             { Test-Path (Join-Path $DistDir "README.md") }
Check "VERSION.txt exists"           { Test-Path (Join-Path $DistDir "VERSION.txt") }
Warn   "docs/known-limitations.md"    { Test-Path ([System.IO.Path]::Combine($DistDir, "docs", "known-limitations.md")) }
Check "assets/background.jpg"           { Test-Path ([System.IO.Path]::Combine($DistDir, "assets", "background.jpg")) }

# Summary
Write-Host "`n=== Results ===" -ForegroundColor Cyan
Write-Host "Passed: $passed" -ForegroundColor Green
if ($warned -gt 0) { Write-Host "Warned: $warned" -ForegroundColor Yellow }
if ($failed -gt 0) { Write-Host "Failed: $failed" -ForegroundColor Red }

if ($failed -gt 0) {
    exit 1
}
exit 0
