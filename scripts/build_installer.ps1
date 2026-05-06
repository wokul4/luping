<#
.SYNOPSIS
    Build ScreenRecorder Inno Setup installer.
.DESCRIPTION
    Locates ISCC.exe (registry, PATH, known paths), validates prerequisites,
    and compiles the installer.
.PARAMETER InnoPath
    Manual path to ISCC.exe (bypasses auto-detection).
.PARAMETER IssPath
    Manual path to .iss file (default: installer/ScreenRecorder.iss relative to project root).
.PARAMETER CheckOnly
    Only run pre-flight checks and ISCC detection, skip compilation.
#>
param(
    [string]$InnoPath,
    [string]$IssPath,
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"

# ============================================================
# 1. Locate project root from script location
# ============================================================
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

if ($IssPath) {
    $IssFile = $IssPath
} else {
    $IssFile = [System.IO.Path]::Combine($ProjectRoot, "installer", "ScreenRecorder.iss")
}
$IconFile  = [System.IO.Path]::Combine($ProjectRoot, "resources", "icons", "app_icon.ico")
$DistDir   = [System.IO.Path]::Combine($ProjectRoot, "dist", "ScreenRecorder")
$DistExe   = [System.IO.Path]::Combine($DistDir, "ScreenRecorder.exe")

Write-Host "=== Build Installer ===" -ForegroundColor Cyan
Write-Host "Project:  $ProjectRoot"
Write-Host "ISS:      $IssFile"
Write-Host "Icon:     $IconFile"
Write-Host "Dist:     $DistDir"
if ($CheckOnly) {
    Write-Host "Mode:     CheckOnly (no build)" -ForegroundColor DarkGray
}
Write-Host ""

# ============================================================
# 2. Pre-flight checks
# ============================================================
$preflightErrors = 0

if (-not (Test-Path $IssFile)) {
    Write-Host "FAIL: .iss file not found: $IssFile" -ForegroundColor Red
    $preflightErrors++
}

if (-not (Test-Path $IconFile)) {
    Write-Host "FAIL: Icon file not found: $IconFile" -ForegroundColor Red
    $preflightErrors++
}

if (-not (Test-Path $DistExe)) {
    Write-Host "FAIL: dist/ScreenRecorder/ not ready - ScreenRecorder.exe missing" -ForegroundColor Red
    Write-Host ""
    Write-Host "Run package_release.ps1 first:" -ForegroundColor Yellow
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 -FFmpegBin D:/ffmpeg/bin -Config Release"
    $preflightErrors++
}

# Validate .iss [Files] sources
$issContent = Get-Content $IssFile -Raw -Encoding UTF8
$sourcePattern = [regex]'Source:\s*"([^"]*)"'
$sources = $sourcePattern.Matches($issContent) | ForEach-Object { $_.Groups[1].Value }
$issDir = Split-Path -Parent $IssFile
foreach ($src in $sources) {
    $combined = [System.IO.Path]::Combine($issDir, $src)
    if ($src.Contains('*')) {
        $parentDir = [System.IO.Path]::GetFullPath((Split-Path -Parent $combined))
        if (-not (Test-Path $parentDir)) {
            Write-Host "FAIL: .iss [Files] source directory not found: $parentDir (from '$src')" -ForegroundColor Red
            $preflightErrors++
        }
    } else {
        $resolved = [System.IO.Path]::GetFullPath($combined)
        if (-not (Test-Path $resolved)) {
            Write-Host "FAIL: .iss [Files] source not found: $resolved (from '$src')" -ForegroundColor Red
            $preflightErrors++
        }
    }
}

if ($preflightErrors -gt 0) {
    Write-Host ""
    Write-Host "Pre-flight: FAIL ($preflightErrors error(s))" -ForegroundColor Red
    exit 1
}
Write-Host "Pre-flight: PASS" -ForegroundColor Green
Write-Host ""

# ============================================================
# 3. Locate ISCC.exe
# ============================================================
$iscc = $null
$checkedPaths = [System.Collections.ArrayList]::new()
$findMethod = ""

if ($InnoPath) {
    # --- A. User-specified path ---
    if (Test-Path $InnoPath) {
        $iscc = $InnoPath
        $findMethod = "user-specified -InnoPath"
    } else {
        Write-Host "FAIL: ISCC.exe not found at specified path: $InnoPath" -ForegroundColor Red
        exit 1
    }
}

# --- B. Default install paths ---
if (-not $iscc) {
    $defaultPaths = @(
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    )
    foreach ($p in $defaultPaths) {
        [void]$checkedPaths.Add($p)
        if (Test-Path $p) {
            $iscc = $p
            $findMethod = "default install path"
            break
        }
    }
}

# --- C. PATH lookup ---
if (-not $iscc) {
    $fromPath = Get-Command "iscc.exe" -ErrorAction SilentlyContinue
    if ($fromPath) {
        $iscc = $fromPath.Source
        $findMethod = "PATH"
    } else {
        [void]$checkedPaths.Add("(PATH: not found)")
    }
}

# --- D. Common custom directories ---
if (-not $iscc) {
    $customDirs = @(
        "D:\tools\Inno Setup 6\ISCC.exe",
        "D:\Tools\Inno Setup 6\ISCC.exe",
        "D:\Program Files\Inno Setup 6\ISCC.exe",
        "E:\tools\Inno Setup 6\ISCC.exe"
    )
    foreach ($p in $customDirs) {
        [void]$checkedPaths.Add($p)
        if (Test-Path $p) {
            $iscc = $p
            $findMethod = "custom directory"
            break
        }
    }
}

# --- E. Registry lookup ---
if (-not $iscc) {
    $regPaths = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*"
    )
    foreach ($regPath in $regPaths) {
        if ($iscc) { break }
        $items = Get-ItemProperty -Path $regPath -ErrorAction SilentlyContinue
        if (-not $items) { continue }
        foreach ($item in $items) {
            if (-not $item.DisplayName) { continue }
            if ($item.DisplayName -match "Inno Setup") {
                $installLocation = $item.InstallLocation
                if (-not $installLocation) { continue }
                $candidate = [System.IO.Path]::Combine($installLocation, "ISCC.exe")
                if (Test-Path $candidate) {
                    $iscc = $candidate
                    $findMethod = "registry ($($item.DisplayName))"
                    break
                }
            }
        }
    }
    if (-not $iscc) {
        [void]$checkedPaths.Add("(registry: no Inno Setup entry found)")
    }
}

# ============================================================
# 4. ISCC not found — report and exit
# ============================================================
if (-not $iscc) {
    Write-Host "FAIL: ISCC.exe not found on this machine." -ForegroundColor Red
    Write-Host ""
    Write-Host "This is NOT a project build failure." -ForegroundColor White
    Write-Host "The installer script (installer/ScreenRecorder.iss) is ready." -ForegroundColor White
    Write-Host "The only missing piece is the Inno Setup compiler on this computer." -ForegroundColor White
    Write-Host ""
    Write-Host "Checked paths:" -ForegroundColor Yellow
    foreach ($p in $checkedPaths) {
        $mark = if (Test-Path $p) { "FOUND" } else { "not found" }
        Write-Host "  $mark  $p" -ForegroundColor DarkGray
    }
    Write-Host ""
    Write-Host "--- Next Steps ---" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Option A: Install Inno Setup 6, then run:" -ForegroundColor Yellow
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts/build_installer.ps1"
    Write-Host ""
    Write-Host "  Download from: https://jrsoftware.org/isinfo.php" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "Option B: If already installed in a custom location, specify the path:" -ForegroundColor Yellow
    Write-Host '  powershell -ExecutionPolicy Bypass -File scripts/build_installer.ps1 -InnoPath "D:\tools\ISCC.exe"'
    Write-Host ""
    Write-Host "To re-check the environment without building:" -ForegroundColor DarkGray
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts/build_installer.ps1 -CheckOnly"
    exit 1
}

# ============================================================
# 5. ISCC found — report
# ============================================================
Write-Host "ISCC:     $iscc" -ForegroundColor Green
Write-Host "Found via: $findMethod" -ForegroundColor DarkGray
Write-Host ""

if ($CheckOnly) {
    Write-Host "=== Environment Check Complete ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "Summary:" -ForegroundColor White
    Write-Host "  Pre-flight:    PASS" -ForegroundColor Green
    Write-Host "  ISCC.exe:      $iscc" -ForegroundColor Green
    Write-Host "  ISS:           $IssFile" -ForegroundColor DarkGray
    Write-Host "  SetupIconFile: ..\resources\icons\app_icon.ico" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "Ready to build. Remove -CheckOnly to compile." -ForegroundColor Yellow
    exit 0
}

# ============================================================
# 6. Compile
# ============================================================
Write-Host "Compiling installer..." -ForegroundColor Yellow
Write-Host "  ISCC: $iscc"
Write-Host "  ISS:  $IssFile"
Write-Host ""

Push-Location $issDir
try {
    $issFileName = Split-Path -Leaf $IssFile
    & $iscc $issFileName
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "FAIL: ISCC exited with code $LASTEXITCODE" -ForegroundColor Red
        Pop-Location
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

# ============================================================
# 7. Report output
# ============================================================
$setupName = "ScreenRecorderSetup_0.1.0-beta"
if ($issContent -match 'OutputBaseFilename\s*=\s*(\S+)') {
    $setupName = $matches[1]
}

$outputDirRel = "..\dist"
if ($issContent -match 'OutputDir\s*=\s*(\S+)') {
    $outputDirRel = $matches[1]
}
$outputDir = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($issDir, $outputDirRel))

$setupPath = [System.IO.Path]::Combine($outputDir, "$setupName.exe")

Write-Host ""
if (Test-Path $setupPath) {
    $setupItem = Get-Item $setupPath
    Write-Host "=== Installer Built ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "Installer: $setupPath"
    Write-Host "Size:      $('{0:N1} MB' -f ($setupItem.Length / 1MB))"
    Write-Host "ISCC:      $iscc"
    Write-Host "ISS:       $IssFile"
    Write-Host "Icon:      SetupIconFile=..\resources\icons\app_icon.ico"
    Write-Host ""
    Write-Host "Run to install:" -ForegroundColor Yellow
    Write-Host "  $setupPath"
} else {
    Write-Host "FAIL: Setup exe not found at expected path: $setupPath" -ForegroundColor Red
    exit 1
}
