Add-Type @'
using System;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string win);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder t, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
'@
$hwnd = [W]::FindWindow("ScreenRecorderWindow", $null)
if ($hwnd -eq 0) {
    Write-Host "Window not found! Listing processes with windows:"
    Get-Process | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object ProcessName, MainWindowHandle | ForEach-Object { Write-Host ("  " + $_.ProcessName + " hwnd=" + $_.MainWindowHandle) }
} else {
    $sb = New-Object System.Text.StringBuilder 256
    [W]::GetWindowText($hwnd, $sb, 256)
    $pid = 0
    [W]::GetWindowThreadProcessId($hwnd, [ref]$pid)
    Write-Host ("Found: HWND=0x" + [Convert]::ToString($hwnd, 16) + " Title='" + $sb.ToString() + "' PID=$pid")
}
