# Debug script: launch app and check if it starts
$AppPath = Resolve-Path "..\build\Release\ScreenRecorder.exe"
$WorkDir = Resolve-Path ".."

Write-Host "App: $AppPath"
Write-Host "WorkDir: $WorkDir"
Write-Host ""

# Check files
Write-Host "EXE exists: $(Test-Path $AppPath)"
Write-Host "Logs dir writable: $(Test-Path '..\logs')"
Write-Host ""

# Launch
Write-Host "Launching..."
$proc = Start-Process -FilePath $AppPath.Path -WorkingDirectory $WorkDir.Path -PassThru
Write-Host "Process ID: $($proc.Id)"
Start-Sleep 3
Write-Host "Running: $(-not $proc.HasExited)"
Write-Host ""

# Try FindWindow
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32 {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string win);
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int EnumWindows(EnumWindowsProc lpEnumFunc, int lParam);
    public delegate bool EnumWindowsProc(IntPtr hWnd, int lParam);
}
"@

$h = [W32]::FindWindow($null, "ScreenRecorder")
Write-Host "FindWindow(null, 'ScreenRecorder') = $h"
$h2 = [W32]::FindWindow("ScreenRecorderWindow", $null)
Write-Host "FindWindow('ScreenRecorderWindow', null) = $h2"

# Enumerate all top-level windows
$found = @()
$cb = [W32+EnumWindowsProc]{
    param($h, $p)
    $sb = New-Object System.Text.StringBuilder 256
    [W32]::GetWindowText($h, $sb, 256)
    $t = $sb.ToString().Trim()
    if ($t -ne "") { $script:found += "$h : $t" }
    return 1
}
[W32]::EnumWindows($cb, 0)
Write-Host "`nTop-level windows:"
$found | ForEach-Object { Write-Host "  $_" }

# Check log
$log = Get-Content "..\logs\app.log" -Tail 10
Write-Host "`nLog tail:"
$log | ForEach-Object { Write-Host "  $_" }

# Cleanup
if (-not $proc.HasExited) { $proc.Kill() }
