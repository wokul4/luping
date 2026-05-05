# List and ffprobe the 5 newest captures
$dir = "D:/luping/build/Release/captures"
$ffprobe = "D:/ffmpeg/bin/ffprobe.exe"

$captures = Get-ChildItem $dir -Filter *.mkv | Sort-Object LastWriteTime -Descending | Select-Object -First 5
Write-Host "=== Recent captures ===" -ForegroundColor Cyan
foreach ($c in $captures) {
    $size = "{0:N0}KB" -f ($c.Length / 1KB)
    Write-Host ("$($c.Name)  $($c.LastWriteTime.ToString('HH:mm:ss'))  $size") -ForegroundColor DarkGray
}

# ffprobe the latest
$latest = $captures | Select-Object -First 1
if ($latest) {
    Write-Host "`n=== ffprobe: $($latest.Name) ===" -ForegroundColor Cyan
    & $ffprobe -hide_banner $latest.FullName 2>&1
}
