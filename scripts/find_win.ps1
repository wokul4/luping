Add-Type @'
using System;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string win);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder t, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
'@

$hwnd1 = [W]::FindWindow("ScreenRecorderWindow", $null)
$sb = New-Object System.Text.StringBuilder 256
if ($hwnd1 -ne 0) {
    [W]::GetWindowText($hwnd1, $sb, 256)
    Write-Host ("ByClass: 0x" + [Convert]::ToString($hwnd1, 16) + " '" + $sb.ToString() + "'")
} else {
    Write-Host "ByClass: 0 (not found)"
}

$hwnd2 = [W]::FindWindow($null, "ScreenRecorder")
if ($hwnd2 -ne 0) {
    $sb2 = New-Object System.Text.StringBuilder 256
    [W]::GetWindowText($hwnd2, $sb2, 256)
    Write-Host ("ByTitle: 0x" + [Convert]::ToString($hwnd2, 16) + " '" + $sb2.ToString() + "'")
} else {
    Write-Host "ByTitle: 0 (not found)"
}

# Also try HWND from process
$procs = Get-Process -Name "ScreenRecorder" -ErrorAction SilentlyContinue
foreach ($p in $procs) {
    $hwnd3 = $p.MainWindowHandle
    if ($hwnd3 -ne 0) {
        $sb3 = New-Object System.Text.StringBuilder 256
        [W]::GetWindowText($hwnd3, $sb3, 256)
        Write-Host ("FromProcess: 0x" + [Convert]::ToString($hwnd3, 16) + " '" + $sb3.ToString() + "'")
    }
}
