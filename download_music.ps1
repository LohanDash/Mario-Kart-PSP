$ErrorActionPreference = "Stop"

$PlaylistUrl = "https://www.youtube.com/playlist?list=PLESFnlO3kNnoCKG0MGtTCGkAvU_OyUJjC"
$OutputDirectory = Join-Path $PSScriptRoot "data\music"
$ArchiveFile = Join-Path $OutputDirectory "downloaded.txt"

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Find-Command {
    param([string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $localExe = Join-Path $PSScriptRoot "$Name.exe"
    if (Test-Path $localExe) {
        return $localExe
    }

    throw "$Name est introuvable. Installe-le ou place $Name.exe à côté du script."
}

$YtDlp = Find-Command "yt-dlp"
$Ffmpeg = Find-Command "ffmpeg"

Write-Host "yt-dlp : $YtDlp"
Write-Host "FFmpeg : $Ffmpeg"
Write-Host "Destination : $OutputDirectory"
Write-Host ""

& $YtDlp `
    --yes-playlist `
    --ignore-errors `
    --continue `
    --no-overwrites `
    --download-archive $ArchiveFile `
    --windows-filenames `
    --ffmpeg-location (Split-Path $Ffmpeg) `
    --extract-audio `
    --audio-format mp3 `
    --audio-quality "128K" `
    --embed-metadata `
    --parse-metadata "playlist_index:%(track_number)s" `
    --output "$OutputDirectory\%(playlist_index)03d - %(title)s.%(ext)s" `
    $PlaylistUrl

if ($LASTEXITCODE -ne 0) {
    throw "yt-dlp s'est terminé avec le code d'erreur $LASTEXITCODE."
}

Write-Host ""
Write-Host "Terminé : les fichiers sont dans data\music"