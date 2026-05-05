<#
.SYNOPSIS
    Create ScreenRecorder zip release package from dist/ScreenRecorder/
.PARAMETER Version
    Version string (e.g. 0.1.0-beta)
#>
param(
    [string]$Version = "0.1.0-beta"
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SourceDir = [System.IO.Path]::Combine($ProjectRoot, "dist", "ScreenRecorder")
$DistDir = [System.IO.Path]::Combine($ProjectRoot, "dist")
$ZipName = "ScreenRecorder_${Version}.zip"
$ZipPath = [System.IO.Path]::Combine($DistDir, $ZipName)

Write-Host "=== make_zip_release.ps1 ===" -ForegroundColor Cyan
Write-Host "Source: $SourceDir"
Write-Host "Zip:    $ZipPath"
Write-Host ""

if (-not (Test-Path $SourceDir)) {
    Write-Host "FAIL: Source directory not found: $SourceDir" -ForegroundColor Red
    Write-Host "Run package_release.ps1 first."
    exit 1
}

# Check required files
$required = @(
    "ScreenRecorder.exe",
    "config/settings.json",
    "README.md",
    "VERSION.txt"
)
foreach ($f in $required) {
    $fp = [System.IO.Path]::Combine($SourceDir, $f)
    if (-not (Test-Path $fp)) {
        Write-Host "FAIL: Required file missing: $f" -ForegroundColor Red
        exit 1
    }
}

# Clean up old zip
Remove-Item -Path $ZipPath -Force -ErrorAction SilentlyContinue

# Add-Type for ZipFile (requires .NET Framework 4.5+)
Add-Type -AssemblyName System.IO.Compression.FileSystem

# Create temp staging directory with clean structure
$TempDir = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), "ScreenRecorder_zip_staging")
$CleanDir = [System.IO.Path]::Combine($TempDir, "ScreenRecorder")

Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $CleanDir -Force | Out-Null
New-Item -ItemType Directory -Path ([System.IO.Path]::Combine($CleanDir, "config")) | Out-Null
New-Item -ItemType Directory -Path ([System.IO.Path]::Combine($CleanDir, "logs")) | Out-Null
New-Item -ItemType Directory -Path ([System.IO.Path]::Combine($CleanDir, "captures")) | Out-Null
New-Item -ItemType Directory -Path ([System.IO.Path]::Combine($CleanDir, "docs")) | Out-Null

# Copy files (excluding temp logs, captures)
Copy-Item -Path ([System.IO.Path]::Combine($SourceDir, "ScreenRecorder.exe")) -Destination $CleanDir
Get-ChildItem -Path $SourceDir -Filter "*.dll" | ForEach-Object { Copy-Item -Path $_.FullName -Destination $CleanDir }
Copy-Item -Path ([System.IO.Path]::Combine($SourceDir, "README.md")) -Destination $CleanDir
Copy-Item -Path ([System.IO.Path]::Combine($SourceDir, "VERSION.txt")) -Destination $CleanDir
Copy-Item -Path ([System.IO.Path]::Combine($SourceDir, "config", "settings.json")) -Destination ([System.IO.Path]::Combine($CleanDir, "config"))
$knownLim = [System.IO.Path]::Combine($SourceDir, "docs", "known-limitations.md")
if (Test-Path $knownLim) {
    Copy-Item -Path $knownLim -Destination ([System.IO.Path]::Combine($CleanDir, "docs"))
}

# .gitkeep in empty dirs
Set-Content -Path ([System.IO.Path]::Combine($CleanDir, "logs", ".gitkeep")) -Value "" -Encoding ASCII
Set-Content -Path ([System.IO.Path]::Combine($CleanDir, "captures", ".gitkeep")) -Value "" -Encoding ASCII

# Create zip
[System.IO.Compression.ZipFile]::CreateFromDirectory($TempDir, $ZipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)

# Cleanup temp
Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue

# Validate zip
$zipItem = Get-Item $ZipPath
$zipSize = "{0:N1} MB" -f ($zipItem.Length / 1MB)
$zipSizeKB = "{0:N0} KB" -f ($zipItem.Length / 1KB)

Write-Host "=== Zip Release Complete ===" -ForegroundColor Green
Write-Host "Path:  $ZipPath"
Write-Host "Size:  $zipSize ($zipSizeKB)"

# Validate zip contents via temp extraction
$ValidateDir = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), "ScreenRecorder_zip_validate")
Remove-Item -Path $ValidateDir -Recurse -Force -ErrorAction SilentlyContinue
try {
    [System.IO.Compression.ZipFile]::ExtractToDirectory($ZipPath, $ValidateDir)
    $validDir = [System.IO.Path]::Combine($ValidateDir, "ScreenRecorder")

    $valErrors = 0
    $vExe = [System.IO.Path]::Combine($validDir, "ScreenRecorder.exe")
    if (-not (Test-Path $vExe)) { Write-Host "  FAIL: ScreenRecorder.exe missing in zip" -ForegroundColor Red; $valErrors++ }
    foreach ($dll in @("avcodec-*.dll", "avformat-*.dll", "avutil-*.dll", "swscale-*.dll", "swresample-*.dll")) {
        $found = Get-ChildItem ([System.IO.Path]::Combine($validDir, $dll)) -ErrorAction SilentlyContinue
        if (-not $found) { Write-Host "  FAIL: DLL $dll missing in zip" -ForegroundColor Red; $valErrors++ }
    }
    $vCfg = [System.IO.Path]::Combine($validDir, "config", "settings.json")
    if (-not (Test-Path $vCfg)) { Write-Host "  FAIL: settings.json missing in zip" -ForegroundColor Red; $valErrors++ }
    $vReadme = [System.IO.Path]::Combine($validDir, "README.md")
    if (-not (Test-Path $vReadme)) { Write-Host "  FAIL: README.md missing in zip" -ForegroundColor Red; $valErrors++ }

    if ($valErrors -eq 0) {
        Write-Host "Validation: PASS (all files verified)" -ForegroundColor Green
    } else {
        Write-Host "Validation: FAIL ($valErrors errors)" -ForegroundColor Red
        exit 1
    }
} finally {
    Remove-Item -Path $ValidateDir -Recurse -Force -ErrorAction SilentlyContinue
}

$fileCount = "N/A"
try {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    $fileCount = $zip.Entries.Count
    $zip.Dispose()
} catch {}
Write-Host "Files in zip: $fileCount"
Write-Host ""
Write-Host "Ready: $ZipPath" -ForegroundColor Green
exit 0
