<#
.SYNOPSIS
    Package ScreenRecorder Release into dist/ScreenRecorder/
.DESCRIPTION
    Copies ScreenRecorder.exe, FFmpeg DLLs, config, and docs into a clean dist directory.
.PARAMETER FFmpegBin
    Path to FFmpeg bin directory (e.g. D:/ffmpeg/bin)
.PARAMETER Config
    Build configuration (Release or Debug)
#>
param(
    [string]$FFmpegBin = "D:/ffmpeg/bin",
    [string]$Config = "Release"
)

# PowerShell 5.1-compatible Join-Path (max 2 args)
function Join-Paths([string]$a, [string]$b) {
    return [System.IO.Path]::Combine($a, $b)
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Paths $ProjectRoot (Join-Paths "build" $Config)
$DistDir = Join-Paths $ProjectRoot (Join-Paths "dist" "ScreenRecorder")
$ConfigDir = Join-Paths $DistDir "config"
$LogsDir = Join-Paths $DistDir "logs"
$CapturesDir = Join-Paths $DistDir "captures"

Write-Host "=== Package Release ===" -ForegroundColor Cyan
Write-Host "Project: $ProjectRoot"
Write-Host "Build:   $BuildDir"
Write-Host "Dist:    $DistDir"
Write-Host "FFmpeg:  $FFmpegBin"

# Check prerequisites
$ExePath = Join-Paths $BuildDir "ScreenRecorder.exe"
if (-not (Test-Path $ExePath)) {
    Write-Host "FAIL: $ExePath not found" -ForegroundColor Red
    Write-Host "Build the project first: cmake --build build --config $Config"
    exit 1
}

if (-not (Test-Path $FFmpegBin)) {
    Write-Host "FAIL: FFmpeg bin directory not found: $FFmpegBin" -ForegroundColor Red
    exit 1
}

# Required FFmpeg DLLs
$RequiredDlls = @(
    "avcodec-*.dll",
    "avformat-*.dll",
    "avutil-*.dll",
    "swscale-*.dll",
    "swresample-*.dll"
)

foreach ($pattern in $RequiredDlls) {
    $matches = Get-ChildItem -Path $FFmpegBin -Filter $pattern
    if (-not $matches) {
        Write-Host "FAIL: FFmpeg DLL missing: $pattern in $FFmpegBin" -ForegroundColor Red
        exit 1
    }
}

# Create dist directories
Write-Host "Creating dist directories..." -ForegroundColor Yellow
Remove-Item -Path $DistDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
New-Item -ItemType Directory -Path $ConfigDir -Force | Out-Null
New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
New-Item -ItemType Directory -Path $CapturesDir -Force | Out-Null

# Copy ScreenRecorder.exe
Write-Host "Copying ScreenRecorder.exe..." -ForegroundColor Yellow
Copy-Item -Path $ExePath -Destination $DistDir

# Copy FFmpeg DLLs
Write-Host "Copying FFmpeg DLLs..." -ForegroundColor Yellow
foreach ($pattern in $RequiredDlls) {
    Get-ChildItem -Path $FFmpegBin -Filter $pattern | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination $DistDir
        Write-Host ("  " + $_.Name) -ForegroundColor DarkGray
    }
}

# Copy README and VERSION
Write-Host "Copying docs..." -ForegroundColor Yellow
$ReadmePath = Join-Paths $ProjectRoot "README.md"
$VersionPath = Join-Paths $ProjectRoot "VERSION.txt"
$KnownLimPath = Join-Paths $ProjectRoot (Join-Paths "docs" "known-limitations.md")

if (Test-Path $ReadmePath) { Copy-Item -Path $ReadmePath -Destination $DistDir }
if (Test-Path $VersionPath) { Copy-Item -Path $VersionPath -Destination $DistDir }
if (Test-Path $KnownLimPath) {
    $DocsDir = Join-Paths $DistDir "docs"
    New-Item -ItemType Directory -Path $DocsDir -Force | Out-Null
    Copy-Item -Path $KnownLimPath -Destination $DocsDir
}

# Copy assets (background image)
Write-Host "Copying assets..." -ForegroundColor Yellow
$AssetsSrc = Join-Paths $ProjectRoot "assets"
$AssetsDst = Join-Paths $DistDir "assets"
if (Test-Path $AssetsSrc) {
    New-Item -ItemType Directory -Path $AssetsDst -Force | Out-Null
    Get-ChildItem -Path $AssetsSrc | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination $AssetsDst
        Write-Host ("  " + $_.Name) -ForegroundColor DarkGray
    }
}

# Create default config/settings.json
$SettingsPath = Join-Paths $ConfigDir "settings.json"
$DefaultSettings = @'
{
  "outputDir": "captures",
  "container": "mkv",
  "fps": 30,
  "bitrateKbps": 10000,
  "recordSystemAudio": true,
  "recordMicrophone": true,
  "captureMode": "Monitor",
  "monitorIndex": 0,
  "lastWindowProcess": "",
  "minimizeToTray": false,
  "showCompletionPrompt": true,
  "savedOutputDir": ""
}
'@
Set-Content -Path $SettingsPath -Value $DefaultSettings -Encoding UTF8

# Write .gitkeep in logs/captures
Set-Content -Path (Join-Paths $LogsDir ".gitkeep") -Value "" -Encoding ASCII
Set-Content -Path (Join-Paths $CapturesDir ".gitkeep") -Value "" -Encoding ASCII

# Verify
$exeItem = Get-Item (Join-Paths $DistDir "ScreenRecorder.exe") -ErrorAction SilentlyContinue
if ($exeItem) {
    Write-Host "`n=== Package Summary ===" -ForegroundColor Cyan
    Write-Host ("ScreenRecorder.exe: {0:N0} KB" -f ($exeItem.Length / 1KB))
    $dllCount = (Get-ChildItem $DistDir -Filter "*.dll").Count
    Write-Host ("FFmpeg DLLs: {0}" -f $dllCount)
    $totalSize = (Get-ChildItem $DistDir -Recurse | Measure-Object -Property Length -Sum).Sum
    Write-Host ("Total size: {0:N1} MB" -f ($totalSize / 1MB))
    Write-Host ""
    Write-Host "Dist directory: $DistDir" -ForegroundColor Green
} else {
    Write-Host "FAIL: ScreenRecorder.exe not found in dist after copy" -ForegroundColor Red
    exit 1
}
Write-Host "Package complete." -ForegroundColor Green
