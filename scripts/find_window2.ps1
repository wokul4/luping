Add-Type @'
using System;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string win);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder t, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr GetTopWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetDesktopWindow();
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, int cmd);
    public const int GW_HWNDNEXT = 2;
}
'@
# Try different class names and window titles
$variants = @(
    ,@("ScreenRecorderWindow", $null)
    ,@($null, "ScreenRecorder")
    ,@("ScreenRecorderWindow", "ScreenRecorder")
)
foreach ($v in $variants) {
    $hwnd = [W]::FindWindow($v[0], $v[1])
    $cls = if ($v[0]) { $v[0] } else { "null" }
    $title = if ($v[1]) { $v[1] } else { "null" }
    if ($hwnd -ne 0) {
        $sb = New-Object System.Text.StringBuilder 256
        [W]::GetWindowText($hwnd, $sb, 256)
        Write-Host ("FindWindow(cls=$cls, title=$title) => 0x" + [Convert]::ToString($hwnd, 16) + " '" + $sb.ToString() + "'")
    } else {
        Write-Host ("FindWindow(cls=$cls, title=$title) => 0 (not found)")
    }
}
