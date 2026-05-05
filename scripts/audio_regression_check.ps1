<#
.SYNOPSIS
    Check audio duration vs video duration in latest MKV capture.
    PASS if delta < 0.5 seconds.
#>
param(
    [string]$CapturesDir = "",
    [string]$FFprobePath = "D:/ffmpeg/bin/ffprobe.exe"
)

if (-not $CapturesDir) {
    $ScriptDir = Split-Path -Parent $PSScriptRoot
    $CapturesDir = [System.IO.Path]::Combine($ScriptDir, "dist", "ScreenRecorder", "captures")
    if (-not (Test-Path $CapturesDir)) {
        $CapturesDir = [System.IO.Path]::Combine($ScriptDir, "build", "Release", "captures")
    }
}

$latest = Get-ChildItem $CapturesDir -Filter *.mkv | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $latest) {
    Write-Host "FAIL: No MKV files found in $CapturesDir" -ForegroundColor Red
    exit 1
}

Write-Host "=== Audio Regression Check ===" -ForegroundColor Cyan
Write-Host "File: $($latest.FullName)" -ForegroundColor DarkGray

# Run ffprobe with JSON output (avoids stderr/ErrorRecord mixing)
$jsonText = & $FFprobePath -hide_banner -loglevel quiet -show_entries `
    stream=index,codec_type,codec_name:stream_tags=DURATION -of json $latest.FullName
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0 -or -not $jsonText) {
    Write-Host "FAIL: ffprobe failed (exit code $exitCode)" -ForegroundColor Red
    exit 1
}

# Parse JSON output
$json = $jsonText | ConvertFrom-Json

# Convert HH:MM:SS.mmm string to fractional seconds
function Convert-TimeToSec([string]$t) {
    if ([string]::IsNullOrEmpty($t)) { return 0.0 }
    $parts = $t.Split(':')
    if ($parts.Count -ne 3) { return 0.0 }
    $hh = [int]$parts[0]
    $mm = [int]$parts[1]
    $ss = [double]$parts[2]
    return $hh * 3600.0 + $mm * 60.0 + $ss
}

$videoDuration = 0.0
$audioDuration = 0.0
$hasVideo = $false
$hasAudio = $false

foreach ($stream in $json.streams) {
    $codec = $stream.codec_name
    $type = $stream.codec_type
    $durStr = if ($stream.tags.DURATION) { $stream.tags.DURATION } else { "N/A" }
    $durSec = Convert-TimeToSec $durStr

    if ($type -eq "video") {
        $hasVideo = $true
        $videoDuration = $durSec
        Write-Host ("  Video: codec={0} dur={1}s ({2})" -f $codec, $durSec, $durStr) -ForegroundColor DarkGray
    } elseif ($type -eq "audio") {
        $hasAudio = $true
        $audioDuration = $durSec
        Write-Host ("  Audio: codec={0} dur={1}s ({2})" -f $codec, $durSec, $durStr) -ForegroundColor DarkGray
    }
}

if (-not $hasVideo) {
    Write-Host "FAIL: No video stream found" -ForegroundColor Red
    exit 1
}

if (-not $hasAudio) {
    Write-Host "WARN: No audio stream found" -ForegroundColor Yellow
}

$delta = [Math]::Abs($audioDuration - $videoDuration)
Write-Host ("`nVideo: {0:N3}s  Audio: {1:N3}s  Delta: {2:N3}s" -f $videoDuration, $audioDuration, $delta) -ForegroundColor Cyan

if ($hasAudio -and $delta -lt 0.5) {
    Write-Host "PASS: Audio/Video duration delta < 0.5s" -ForegroundColor Green
    exit 0
} elseif ($hasAudio) {
    Write-Host "FAIL: Audio/Video duration delta $delta >= 0.5s" -ForegroundColor Red
    exit 1
} else {
    Write-Host "SKIP: No audio stream to check" -ForegroundColor Yellow
    exit 0
}
