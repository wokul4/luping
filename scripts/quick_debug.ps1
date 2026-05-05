$exe = Resolve-Path "..\build\Release\ScreenRecorder.exe"
$wd  = Resolve-Path ".."

Write-Host "Starting: $($exe.Path)"
Write-Host "WD: $($wd.Path)"

$proc = Start-Process -FilePath $exe.Path -WorkingDirectory $wd.Path -PassThru
Write-Host "PID: $($proc.Id)"
Start-Sleep 5

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W {
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern IntPtr FindWindow(string cls, string win);
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
}
"@

# Method 1: Find by title
$h1 = [W]::FindWindow($null, "ScreenRecorder")
Write-Host "FindWindow(null, 'ScreenRecorder') = $h1"

# Method 2: Find by class
$h2 = [W]::FindWindow("ScreenRecorderWindow", $null)
Write-Host "FindWindow('ScreenRecorderWindow', null) = $h2"

# Method 3: Look at all windows around the PID
$proc2 = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
Write-Host "Still running: $(-not $proc.HasExited)"

# Check recent log
if (Test-Path "..\logs\app.log") {
    $log = Get-Content "..\logs\app.log" -Tail 5
    Write-Host "`nLog:"
    $log | ForEach-Object { Write-Host "  $_" }
}

# Cleanup
if (-not $proc.HasExited) { $proc.Kill() }
