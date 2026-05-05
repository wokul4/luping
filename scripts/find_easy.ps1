$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path "$ScriptDir\.."

# Minimal FindWindow test
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Easy {
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindow(string cls, string win);

    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool EnumWindows(EnumWindowsProc e, IntPtr p);
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);

    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);

    public static IntPtr FindScreenRecorder() {
        IntPtr result = IntPtr.Zero;
        EnumWindows((h, p) => {
            StringBuilder sb = new StringBuilder(256);
            GetWindowText(h, sb, 256);
            if (sb.ToString() == "ScreenRecorder") {
                result = h;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
"@

# Check if app is running
Get-Process ScreenRecorder -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "Running PID=$($_.Id)" -F Yellow }

# Try to find
$h1 = [Easy]::FindWindow($null, "ScreenRecorder")
Write-Host "FindWindow(null, 'ScreenRecorder') = $h1"
$h2 = [Easy]::FindWindow("ScreenRecorderWindow", $null)
Write-Host "FindWindow('ScreenRecorderWindow', null) = $h2"
$h3 = [Easy]::FindScreenRecorder()
Write-Host "FindScreenRecorder() = $h3"
