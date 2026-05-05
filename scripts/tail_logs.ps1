# tail_logs.ps1 — 分析 ScreenRecorder app.log
param(
    [string]$LogFile = "..\logs\app.log",
    [int]$Lines = 200,
    [switch]$Full
)

if (!(Test-Path $LogFile)) {
    Write-Host "ERROR: $LogFile not found" -ForegroundColor Red
    exit 1
}

$content = Get-Content $LogFile -Tail $Lines

Write-Host "=== ScreenRecorder Log Tail ===" -ForegroundColor Cyan
Write-Host "File: $LogFile"
Write-Host "Showing: last $Lines lines"
Write-Host ""

# Keywords to highlight
$highlight = @{
    'ERROR'         = 'Red'
    'WARN'          = 'Yellow'
    'WindowClosed'  = 'Red'
    'WindowMinimized' = 'Yellow'
    'DuplicationAccessLost' = 'Magenta'
    'DuplicationRecreateFailed' = 'Red'
    'STATS'         = 'Green'
    'GCS:'          = 'Cyan'
    'RT:'           = 'White'
    '=== '          = 'Cyan'
    'capture'       = 'Gray'
    'finalizing'    = 'Yellow'
    'RecorderController' = 'Gray'
}

$filtered = @()
foreach ($line in $content) {
    $matched = $false
    foreach ($key in $highlight.Keys) {
        if ($line -match $key) {
            $color = $highlight[$key]
            # Use GetAccentColor for dark terminal readability
            if ($Full) {
                Write-Host "  " -NoNewline
                Write-Host $line -ForegroundColor $color
            } else {
                $filtered += @{ line = $line; color = $color }
            }
            $matched = $true
            break
        }
    }
    if (!$matched -and $Full) {
        Write-Host "  $line" -ForegroundColor DarkGray
    }
}

if (!$Full) {
    Write-Host "Filtered entries (use -Full for full output):" -ForegroundColor Cyan
    foreach ($f in $filtered) {
        Write-Host $f.line -ForegroundColor $f.color
    }
}

Write-Host "`n=== Summary ===" -ForegroundColor Cyan
$errorCount = ($content | Select-String "ERROR").Count
$warnCount = ($content | Select-String "WARN").Count
$statsCount = ($content | Select-String "STATS").Count
Write-Host "Errors: $errorCount  Warnings: $warnCount  STATS lines: $statsCount" -ForegroundColor $(if ($errorCount -gt 0) { "Red" } else { "Green" })
