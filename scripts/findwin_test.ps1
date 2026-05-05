Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class FW {
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern IntPtr FindWindow(string cls, string win);
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc e, int p);
    public delegate bool EnumWindowsProc(IntPtr h, int p);
}
"@

# Find any ScreenRecorder-related windows
$found = @()
$cb = [FW+EnumWindowsProc]{ param($h,$p)
    $sb = New-Object System.Text.StringBuilder 256
    $sb2 = New-Object System.Text.StringBuilder 256
    [FW]::GetWindowText($h, $sb, 256)
    [FW]::GetClassName($h, $sb2, 256)
    $t = $sb.ToString().Trim()
    $c = $sb2.ToString().Trim()
    if ($t -like "*Screen*" -or $c -like "*Screen*") {
        $script:found += "h=$h title='$t' class='$c'"
    }
    if ($t -eq "ScreenRecorder") {
        Write-Host "FOUND: h=$h title='$t' class='$c'" -ForegroundColor Green
    }
    return $true
}
[FW]::EnumWindows($cb, 0)

# Test various FindWindow calls
Write-Host "`nFindWindow tests:"
Write-Host "  (null, 'ScreenRecorder'): $([FW]::FindWindow($null, 'ScreenRecorder'))"
Write-Host "  ('ScreenRecorderWindow', null): $([FW]::FindWindow('ScreenRecorderWindow', $null))"
Write-Host "  ('#32770', null): $([FW]::FindWindow('#32770', $null))"
