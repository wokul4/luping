function Find-ScreenRecorder {
    $procs = Get-Process -Name "ScreenRecorder" -ErrorAction SilentlyContinue
    foreach ($p in $procs) {
        $hwnd = $p.MainWindowHandle
        if ($hwnd -ne 0 -and $hwnd -ne [IntPtr]::Zero) {
            try {
                $hwndInt = $hwnd.ToInt64()
                Write-Host ("Found: HWND=" + $hwndInt + " (0x" + $hwndInt.ToString("X") + ")")
                return $hwnd
            } catch {
                Write-Host "Error converting HWND"
            }
        } else {
            Write-Host "Process found but MainWindowHandle is 0"
            Write-Host ("  StartTime: " + $p.StartTime)
            Write-Host ("  Responding: " + $p.Responding)
        }
    }
    Write-Host "No ScreenRecorder process found or no window handle"
    return [IntPtr]::Zero
}
$hwnd = Find-ScreenRecorder
if ($hwnd -ne [IntPtr]::Zero) {
    # Send WM_HOTKEY
    Add-Type @'
    using System;
    using System.Runtime.InteropServices;
    public class Win32 {
        [DllImport("user32.dll")] public static extern int PostMessage(IntPtr hWnd, uint Msg, int wParam, int lParam);
    }
'@
    $WM_HOTKEY = 0x0312
    $MOD_CTRL_ALT = 3
    $VK_R = 0x52
    $lParam = ($MOD_CTRL_ALT -band 0xFFFF) -bor (($VK_R -band 0xFFFF) -shl 16)
    $result = [Win32]::PostMessage($hwnd, $WM_HOTKEY, 1, $lParam)
    Write-Host ("PostMessage result: " + $result)
} else {
    Write-Host "Could not find ScreenRecorder window"
    # List all with windows
    Get-Process | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object ProcessName, @{N="HWND";E={$_.MainWindowHandle.ToInt64().ToString("X")}} | ForEach-Object { Write-Host ("  " + $_.ProcessName + " hwnd=0x" + $_.HWND) }
}
