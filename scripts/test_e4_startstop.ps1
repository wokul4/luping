# E4: Multiple consecutive Start/Stop via WM_HOTKEY PostMessage
param(
    [int]$Iterations = 5,
    [int]$RecordSec = 3
)

function Wait-ForWindow($TimeoutSec) {
    $end = [DateTime]::Now.AddSeconds($TimeoutSec)
    while ([DateTime]::Now -lt $end) {
        $p = Get-Process -Name "ScreenRecorder" -ErrorAction SilentlyContinue
        if ($p -and $p.MainWindowHandle -ne 0) {
            return $p.MainWindowHandle
        }
        Start-Sleep -Milliseconds 300
    }
    return [IntPtr]::Zero
}

function Send-Hotkey($hwnd, $hotkeyId) {
    $WM_HOTKEY = 0x0312
    $MOD_CTRL_ALT = 3
    $VK_R = 0x52
    $lParam = ($MOD_CTRL_ALT -band 0xFFFF) -bor (($VK_R -band 0xFFFF) -shl 16)
    return [Win32]::PostMessage($hwnd, $WM_HOTKEY, $hotkeyId, $lParam)
}

function Wait-ForLogLine($LogFile, $Pattern, $TimeoutSec) {
    $end = [DateTime]::Now.AddSeconds($TimeoutSec)
    while ([DateTime]::Now -lt $end) {
        $content = Get-Content $LogFile -Tail 10 -ErrorAction SilentlyContinue
        if ($content -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern int PostMessage(IntPtr hWnd, uint Msg, int wParam, int lParam);
}
'@

$AppPath = "D:/luping/build/Release/ScreenRecorder.exe"
$LogFile = "D:/luping/build/Release/logs/app.log"
$HotkeyId = 1   # ID_START_STOP

Write-Host "=== E4: Consecutive Start/Stop ($Iterations x ${RecordSec}s) ===" -ForegroundColor Cyan

# Kill old
taskkill /f /im ScreenRecorder.exe 2>$null
Start-Sleep -Seconds 2

# Backup old log
if (Test-Path $LogFile) {
    $backup = "app_before_e4_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".log"
    Copy-Item $LogFile ("D:/luping/build/Release/logs/" + $backup)
    Write-Host "Backed up log -> $backup" -ForegroundColor DarkGray
}

# Launch
Write-Host "Starting app..." -ForegroundColor Yellow
Start-Process -FilePath $AppPath -WorkingDirectory (Split-Path $AppPath -Parent)
$hwnd = Wait-ForWindow -TimeoutSec 15
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Host "FAIL: App window not found within 15s" -ForegroundColor Red
    exit 1
}
Write-Host ("Window HWND=0x" + $hwnd.ToInt64().ToString("X")) -ForegroundColor Green
Start-Sleep -Seconds 2

$passed = 0
$failed = 0

for ($i = 1; $i -le $Iterations; $i++) {
    Write-Host ("--- Iteration $i/$Iterations ---") -ForegroundColor Cyan

    # Start recording
    $r1 = Send-Hotkey $hwnd $HotkeyId
    Write-Host ("  Start -> $r1") -ForegroundColor DarkGray
    Start-Sleep -Seconds $RecordSec

    # Stop recording (thread may take ~6s to exit due to WASAPI init)
    $r2 = Send-Hotkey $hwnd $HotkeyId
    Write-Host ("  Stop  -> $r2") -ForegroundColor DarkGray

    # Wait for thread to fully exit (WASAPI init can take ~6s)
    $ok = Wait-ForLogLine $LogFile "thread finished" 15
    if ($ok) {
        $passed++
        Write-Host "  PASS: thread finished" -ForegroundColor Green
    } else {
        $failed++
        Write-Host "  FAIL: no thread finished within 15s" -ForegroundColor Red
        Get-Content $LogFile -Tail 5
    }
    Start-Sleep -Seconds 1
}

# Summary
Write-Host "`n=== E4 Results ===" -ForegroundColor Cyan
Write-Host "Passed: $passed / $Iterations" -ForegroundColor Green
Write-Host "Failed: $failed / $Iterations" -ForegroundColor Red

# Check captures
$captures = Get-ChildItem "D:/luping/build/Release/captures" -Filter *.mkv | Sort-Object LastWriteTime | Select-Object -Last $Iterations
Write-Host ("New captures: " + $captures.Count) -ForegroundColor Cyan
foreach ($c in $captures) {
    Write-Host ("  " + $c.Name + " (" + "{0:N0}KB" -f ($c.Length/1KB) + ")") -ForegroundColor DarkGray
}

# Check errors in log
$errors = Select-String -Path $LogFile -Pattern "ERROR" -SimpleMatch | Where-Object { $_.Line -notmatch "avcodec_open2" }
if ($errors) {
    Write-Host "`nERROR lines in log:" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host ("  " + $_.Line) -ForegroundColor Red }
} else {
    Write-Host "`nNo ERROR lines in log" -ForegroundColor Green
}

Write-Host "`nLast 10 log lines:" -ForegroundColor Cyan
Get-Content $LogFile -Tail 10

if ($failed -gt 0) { exit 1 }
exit 0

