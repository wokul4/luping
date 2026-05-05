# run_test.ps1 — ScreenRecorder 自动化录制测试
param([string]$Test = "T1")

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path "$ScriptDir\.."
$AppExe = "$ProjectRoot\build\Release\ScreenRecorder.exe"

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W32 {
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc e, IntPtr p);
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
    public const uint WM_COMMAND  = 0x0111;
    public const uint WM_CLOSE    = 0x0010;
    public const uint BM_CLICK    = 0x00F5;
    public const uint CB_SETCURSEL = 0x014E;
    public const int  SW_MINIMIZE = 6;
    public const int  SW_RESTORE  = 9;
    public const int  IDC_START   = 112;
    public const int  IDC_STOP    = 114;
    public const int  IDC_REFRESH = 102;

    public static IntPtr FindByTitle(string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, p) => {
            StringBuilder sb = new StringBuilder(256);
            GetWindowText(h, sb, 256);
            if (sb.ToString() == title) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static void ClickButton(IntPtr parent, int btnId) {
        IntPtr btn = GetDlgItem(parent, btnId);
        if (btn == IntPtr.Zero) {
            System.Console.WriteLine("CLICK: btn " + btnId + " not found");
            return;
        }
        System.Console.WriteLine("CLICK: btn " + btnId + " hwnd=" + btn.ToInt64());
        // Post WM_COMMAND to parent (works across threads)
        // wParam = MAKEWPARAM(btnId, BN_CLICKED)
        int wp = btnId; // BN_CLICKED=0
        PostMessage(parent, 0x0111, (IntPtr)wp, btn);
    }
}
"@

function WaitWindow($sec) {
    $end = [DateTime]::Now.AddSeconds($sec)
    while ([DateTime]::Now -lt $end) {
        $h = [W32]::FindByTitle("ScreenRecorder")
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 300
    }
    return [IntPtr]::Zero
}

function CheckMkv { return Get-ChildItem "$ProjectRoot\captures\*.mkv" | Sort-Object LastWriteTime -Descending | Select-Object -First 1 }

function VerifyMkv($f) {
    $ffp = "$ProjectRoot\ffmpeg\bin\ffprobe.exe"
    $r = & $ffp -v quiet -show_entries format=format_name -of default=noprint_wrappers=1 $f.FullName 2>&1 | Out-String
    return ($r -match "format_name=matroska")
}

# Pre-check
if (-not (Test-Path $AppExe)) { Write-Host "FAIL: no exe" -F Red; exit 1 }
foreach ($n in "avformat-62","avcodec-62","avutil-60","swscale-9","swresample-6") {
    if (-not (Test-Path "$ProjectRoot\build\Release\$n.dll")) { Write-Host "FAIL: $n.dll" -F Red; exit 1 }
}
Write-Host "Pre-check OK" -F Green

# Kill leftover
Get-Process ScreenRecorder -ea 0 | Stop-Process -Force
Start-Sleep 2

# Launch
Write-Host "Launching..."
$proc = Start-Process -FilePath $AppExe -WorkingDirectory $ProjectRoot -PassThru
$appHwnd = WaitWindow 15
if ($appHwnd -eq [IntPtr]::Zero) { Write-Host "FAIL: window not found" -F Red; exit 1 }
Write-Host "App HWND=$appHwnd" -F Green
Start-Sleep 2

$ok = $false

if ($Test -eq "T1") {
    Write-Host "T1: Monitor recording 12s..."
    [W32]::ClickButton($appHwnd, [W32]::IDC_START) # BM_CLICK Start
    Write-Host "  Start button clicked"
    Start-Sleep 12
    [W32]::ClickButton($appHwnd, [W32]::IDC_STOP)  # BM_CLICK Stop
    Write-Host "  Stop button clicked"
    Start-Sleep 3
    $mkv = CheckMkv
    if (-not $mkv) { Write-Host "FAIL: no MKV" -F Red; $ok=$false }
    elseif (-not (VerifyMkv $mkv)) { Write-Host "FAIL: MKV invalid" -F Red; $ok=$false }
    else { Write-Host "PASS: $($mkv.Name) $($mkv.Length) B" -F Green; $ok=$true }
}

if ($Test -eq "T2") {
    # Ensure notepad
    $npad = [W32]::FindByTitle("无标题 - Notepad")
    if ($npad -eq [IntPtr]::Zero) { Start-Process notepad.exe; Start-Sleep 3 }
    # Refresh source list
    [W32]::ClickButton($appHwnd, [W32]::IDC_REFRESH)
    Start-Sleep 1

    Write-Host "T2: Notepad recording 12s..."
    [W32]::ClickButton($appHwnd, [W32]::IDC_START)
    Write-Host "  Start clicked"
    Start-Sleep 12
    [W32]::ClickButton($appHwnd, [W32]::IDC_STOP)
    Write-Host "  Stop clicked"
    Start-Sleep 3
    $mkv = CheckMkv
    if (-not $mkv) { Write-Host "FAIL: no MKV" -F Red; $ok=$false }
    elseif (-not (VerifyMkv $mkv)) { Write-Host "FAIL: MKV invalid" -F Red; $ok=$false }
    else { Write-Host "PASS: $($mkv.Name) $($mkv.Length) B" -F Green; $ok=$true }
}

if ($Test -eq "T5") {
    $npad = [W32]::FindByTitle("无标题 - Notepad")
    if ($npad -eq [IntPtr]::Zero) { Start-Process notepad.exe; Start-Sleep 3; $npad = [W32]::FindByTitle("无标题 - Notepad") }
    [W32]::ClickButton($appHwnd, [W32]::IDC_REFRESH)
    Start-Sleep 1

    Write-Host "T5: Minimize Notepad during recording..."
    [W32]::ClickButton($appHwnd, [W32]::IDC_START)
    Start-Sleep 3
    Write-Host "  Minimizing Notepad..."
    [W32]::ShowWindow($npad, [W32]::SW_MINIMIZE)
    Start-Sleep 5
    Write-Host "  Restoring Notepad..."
    [W32]::ShowWindow($npad, [W32]::SW_RESTORE)
    Start-Sleep 3
    [W32]::ClickButton($appHwnd, [W32]::IDC_STOP)
    Start-Sleep 3
    $mkv = CheckMkv
    if (-not $mkv) { Write-Host "FAIL: no MKV" -F Red; $ok=$false }
    elseif (-not (VerifyMkv $mkv)) { Write-Host "FAIL: MKV invalid" -F Red; $ok=$false }
    else { Write-Host "PASS: $($mkv.Name) $($mkv.Length) B" -F Green; $ok=$true }
}
if ($Test -eq "T6") {
    $npad = [W32]::FindByTitle("无标题 - Notepad")
    if ($npad -eq [IntPtr]::Zero) { Start-Process notepad.exe; Start-Sleep 3; $npad = [W32]::FindByTitle("无标题 - Notepad") }
    [W32]::ClickButton($appHwnd, [W32]::IDC_REFRESH)
    Start-Sleep 1
    Write-Host "T6: Close Notepad during recording..."
    [W32]::ClickButton($appHwnd, [W32]::IDC_START)
    Start-Sleep 3
    Write-Host "  Closing Notepad..."
    [W32]::PostMessage($npad, [W32]::WM_CLOSE, [IntPtr]0, [IntPtr]0)
    Start-Sleep 3
    [W32]::ClickButton($appHwnd, [W32]::IDC_STOP)
    Start-Sleep 3
    $mkv = CheckMkv
    if (-not $mkv) { Write-Host "FAIL: no MKV" -F Red; $ok=$false }
    elseif (-not (VerifyMkv $mkv)) { Write-Host "FAIL: MKV invalid" -F Red; $ok=$false }
    else { Write-Host "PASS: $($mkv.Name) $($mkv.Length) B" -F Green; $ok=$true }
}

# Close
[W32]::PostMessage($appHwnd, [W32]::WM_CLOSE, [IntPtr]0, [IntPtr]0)
Start-Sleep 3
if (-not $proc.HasExited) { $proc.Kill() }

# Log check
if (Test-Path "$ProjectRoot\logs\app.log") {
    $log = Get-Content "$ProjectRoot\logs\app.log" -Tail 30
    $errs = $log | Select-String "ERROR"
    $rec  = $log | Select-String "RT:"
    Write-Host "`nLog errors: $($errs.Count)" -F $(if ($errs) { 'Red' } else { 'Green' })
    $rec | ForEach-Object { Write-Host "  $_" -F Gray }
}

Write-Host "`nTest $Test : $(if ($ok) { 'PASS' } else { 'FAIL' })" -F $(if ($ok) { 'Green' } else { 'Red' })
if ($ok) { exit 0 } else { exit 1 }
