<#
.SYNOPSIS
    Build ScreenRecorder Inno Setup installer.
.PARAMETER Version
    Version string (e.g. 0.1.0-beta)
.PARAMETER ISCCPath
    Path to ISCC.exe (Inno Setup compiler). If not provided, searches PATH.
#>
param(
    [string]$Version = "0.1.0-beta",
    [string]$ISCCPath = ""
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$IssFile = [System.IO.Path]::Combine($ProjectRoot, "installer", "ScreenRecorder.iss")
$DistDir = [System.IO.Path]::Combine($ProjectRoot, "dist")
$DistSrc = [System.IO.Path]::Combine($DistDir, "ScreenRecorder")

Write-Host "=== build_installer.ps1 ===" -ForegroundColor Cyan
Write-Host "Version: $Version"
Write-Host "ISS:     $IssFile"
Write-Host ""

# Check prerequisites
if (-not (Test-Path $DistSrc)) {
    Write-Host "FAIL: dist/ScreenRecorder not found. Run package_release.ps1 first." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $IssFile)) {
    Write-Host "FAIL: installer script not found: $IssFile" -ForegroundColor Red
    exit 1
}

# Find ISCC
if (-not $ISCCPath) {
    $ISCCPath = Get-Command "iscc.exe" -ErrorAction SilentlyContinue
    if ($ISCCPath) {
        $ISCCPath = $ISCCPath.Source
    }
}

if (-not $ISCCPath -or -not (Test-Path $ISCCPath)) {
    Write-Host "ISCC.exe not found in PATH." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To build the installer, install Inno Setup from:" -ForegroundColor Yellow
    Write-Host "  https://jrsoftware.org/isdl.php" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Then add ISCC.exe to PATH or pass -ISCCPath:" -ForegroundColor Yellow
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts/build_installer.ps1 -ISCCPath 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe'" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Installer build: SKIPPED — Inno Setup not installed" -ForegroundColor Yellow
    exit 0
}

# Write version info for Inno Setup (VERSION.txt is read by ISS)
$VersionFile = [System.IO.Path]::Combine($DistSrc, "VERSION.txt")
Set-Content -Path $VersionFile -Value $Version -Encoding ASCII

# Build installer
Write-Host "Running ISCC.exe..." -ForegroundColor Cyan
$output = & $ISCCPath $IssFile 2>&1
$exitCode = $LASTEXITCODE

Write-Host $output

if ($exitCode -ne 0) {
    Write-Host "FAIL: Inno Setup compiler returned exit code $exitCode" -ForegroundColor Red
    exit $exitCode
}

# Find the generated installer
$setupExe = Get-ChildItem -Path $DistDir -Filter "ScreenRecorderSetup_*.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($setupExe) {
    $size = "{0:N1} MB" -f ($setupExe.Length / 1MB)
    Write-Host "`nInstaller: $($setupExe.FullName)" -ForegroundColor Green
    Write-Host "Size:      $size" -ForegroundColor Green
    Write-Host "Installer build: PASS" -ForegroundColor Green
} else {
    Write-Host "FAIL: No installer exe found in dist/" -ForegroundColor Red
    exit 1
}
exit 0
