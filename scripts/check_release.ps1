# check_release.ps1 — ScreenRecorder Release 目录验证
param(
    [string]$BuildDir = "..\build\Release",
    [string]$LogDir = "..\logs",
    [string]$CaptureDir = "..\captures"
)

$ErrorActionPreference = "Stop"
$pass = 0
$fail = 0

function Check {
    param($Name, $Condition, $Message)
    if (& $Condition) {
        Write-Host "  PASS  $Name" -ForegroundColor Green
        $script:pass++
    } else {
        Write-Host "  FAIL  $Name — $Message" -ForegroundColor Red
        $script:fail++
    }
}

Write-Host "=== ScreenRecorder Release Check ===" -ForegroundColor Cyan

# 1. EXE 存在
Check "ScreenRecorder.exe exists" {
    Test-Path "$BuildDir/ScreenRecorder.exe"
} "File not found"

# 2. EXE 大小 > 100KB
Check "EXE size > 100KB" {
    (Get-Item "$BuildDir/ScreenRecorder.exe").Length -gt 100KB
} "File too small"

# 3. FFmpeg DLLs
$ffDlls = @("avformat-62.dll", "avcodec-62.dll", "avutil-60.dll", "swscale-9.dll", "swresample-6.dll")
foreach ($dll in $ffDlls) {
    Check "FFmpeg DLL: $dll" { Test-Path "$BuildDir/$dll" } "Missing $dll"
}

# 4. Logs dir
Check "Logs dir exists or creatable" {
    $p = Resolve-Path $BuildDir; $log = Join-Path $p "..\..\logs"
    if (!(Test-Path $log)) { New-Item -ItemType Directory $log -Force | Out-Null }
    Test-Path $log
} "Cannot create logs/"

# 5. Captures dir
Check "Captures dir exists" {
    $p = Resolve-Path $BuildDir; $cap = Join-Path $p "..\..\captures"
    if (!(Test-Path $cap)) { New-Item -ItemType Directory $cap -Force | Out-Null }
    Test-Path $cap
} "Cannot create captures/"

# 6. Last MKV > 0 (if any)
$latestMkv = Get-ChildItem "$CaptureDir/*.mkv" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($latestMkv) {
    Check "Latest MKV size > 0" { $latestMkv.Length -gt 0 } "File is empty"
}

# 7. app.log exists
$appLog = "$LogDir/app.log"
Check "app.log exists" { Test-Path $appLog } "Missing"

# 8. app.log contains recent entries
if (Test-Path $appLog) {
    $logAge = (Get-Date) - (Get-Item $appLog).LastWriteTime
    Check "app.log is recent" { $logAge.TotalHours -lt 24 } "Older than 24h"
}

Write-Host "`n=== Summary ===" -ForegroundColor Cyan
Write-Host "Pass: $pass  Fail: $fail" -ForegroundColor $(if ($fail -eq 0) { "Green" } else { "Red" })
exit $fail
