param(
    [string]$GameDir,
    [switch]$ReplaceExisting
)

$ErrorActionPreference = 'Stop'
$repo = 'https://github.com/noahhhi/isaac-highfps/releases'

if (-not $GameDir) {
    $roots = @(
        (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
        (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath
        (Join-Path ${env:ProgramFiles(x86)} Steam)
    )
    :findSteam foreach ($root in $roots) {
        if (-not $root) { continue }
        $libraries = @($root)
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf -PathType Leaf) {
            foreach ($line in Get-Content -LiteralPath $vdf) {
                if ($line -match '^\s*"path"\s*"([^"]+)"\s*$') {
                    $libraries += $Matches[1].Replace('\\', '\')
                }
            }
        }
        foreach ($library in $libraries) {
            $candidate = Join-Path $library 'steamapps\common\The Binding of Isaac Rebirth'
            if (Test-Path -LiteralPath (Join-Path $candidate 'isaac-ng.exe') -PathType Leaf) {
                $GameDir = $candidate
                break findSteam
            }
        }
    }
}
if (-not $GameDir -or -not (Test-Path -LiteralPath (Join-Path $GameDir 'isaac-ng.exe') -PathType Leaf)) {
    throw 'Isaac was not found. Pass -GameDir with the directory containing isaac-ng.exe.'
}
$GameDir = (Resolve-Path -LiteralPath $GameDir).ProviderPath
$game = Join-Path $GameDir 'isaac-ng.exe'

function Assert-GameClosed {
    if (Get-Process -Name isaac-ng -ErrorAction SilentlyContinue) {
        throw 'Exit Isaac before installing.'
    }
}
function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Get-Asset([string]$Url, [string]$Destination) {
    & curl.exe --disable --proto '=https' --proto-redir '=https' --fail --silent --show-error --location --connect-timeout 8 --max-time 45 --output $Destination $Url
    if ($LASTEXITCODE -ne 0) { throw "Download failed ($LASTEXITCODE)." }
}
function Install-File([string]$Name) {
    $from = Join-Path $staging $Name
    $to = Join-Path $GameDir $Name
    if ([System.IO.File]::Exists($to)) {
        # Rename on the same volume, preserving the original if replacement fails.
        [System.IO.File]::Replace($from, $to, (Join-Path $staging "$Name.replaced"))
    } else {
        [System.IO.File]::Move($from, $to)
    }
}

Assert-GameClosed
$download = Join-Path ([System.IO.Path]::GetTempPath()) ("isaac-highfps-" + [guid]::NewGuid())
$lock = Join-Path $GameDir '.isaac-highfps-install.lock'
$locked = $false
$staging = $null
try {
    [void][System.IO.Directory]::CreateDirectory($download)
    $manifestFile = Join-Path $download 'j510-update.txt'
    Get-Asset "$repo/latest/download/j510-update.txt" $manifestFile
    $manifest = [System.IO.File]::ReadAllLines($manifestFile)
    if ($manifest.Length -ne 6 -or $manifest[0] -cne 'isaac-highfps-j510-v1' -or
        $manifest[1] -cnotmatch '^[1-9][0-9]{0,8}$') { throw 'Invalid J510 release manifest.' }
    foreach ($checksum in $manifest[2..5]) {
        if ($checksum -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid release checksum.' }
    }
    if ((Get-Sha256 $game) -cne $manifest[2]) {
        throw 'This release needs the supported Repentance+ J510 executable. No files changed.'
    }

    $assets = "$repo/download/j510-r$($manifest[1])"
    $files = @('winmm.dll', 'isaac-highfps-update.exe', 'isaac-highfps.ini')
    for ($i = 0; $i -lt $files.Length; $i++) {
        $name = $files[$i]
        $asset = Join-Path $download $name
        Get-Asset "$assets/$name" $asset
        if ((Get-Sha256 $asset) -cne $manifest[$i + 3]) {
            throw "Checksum mismatch for $name. No files changed."
        }
    }
    [void](New-Item -Path $lock -ItemType Directory)
    $locked = $true
    Assert-GameClosed
    if ((Get-Sha256 $game) -cne $manifest[2]) { throw 'Game executable changed during download.' }
    foreach ($name in $files) {
        $target = Get-Item -LiteralPath (Join-Path $GameDir $name) -Force -ErrorAction SilentlyContinue
        if ($target -and ($target.PSIsContainer -or ($target.Attributes -band [IO.FileAttributes]::ReparsePoint))) {
            throw "Cannot replace $name. Expected an ordinary file."
        }
    }
    $dll = Join-Path $GameDir 'winmm.dll'
    if ((Test-Path -LiteralPath $dll) -and -not $ReplaceExisting) {
        $known = @($manifest[3],
            'd1273d694d9d86c8b137ceb68d3f32575316ee749fb3c634dca736f3a91d78ae',
            'c4523d848b97242c26bc88c7f35461f23120aaf0e4ef6a53cce197ae4d42e97b',
            'd1126945479e0d7ee6b60e474cd62d7ea6671afed7df103a61006678629ec8f4')
        if ((Get-Sha256 $dll) -notin $known) {
            throw 'Existing winmm.dll differs. If it is safe to replace it, rerun with -ReplaceExisting to back it up and continue.'
        }
    }
    $staging = Join-Path $GameDir ('.isaac-highfps-install-' + [guid]::NewGuid())
    [void][System.IO.Directory]::CreateDirectory($staging)
    $backup = $null
    foreach ($name in $files) {
        Copy-Item -LiteralPath (Join-Path $download $name) -Destination $staging
        $target = Join-Path $GameDir $name
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            if (-not $backup) {
                $backup = Join-Path $GameDir ('highfps-backup-' + [guid]::NewGuid())
                [void][System.IO.Directory]::CreateDirectory($backup)
            }
            Copy-Item -LiteralPath $target -Destination $backup
        }
    }
    if ($backup) { Write-Host "Backup saved in $backup" }
    if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'isaac-highfps.ini'))) {
        Install-File 'isaac-highfps.ini'
    }
    Install-File 'isaac-highfps-update.exe'
    Install-File 'winmm.dll'
    Write-Host "Installed HighFPS J510 r$($manifest[1]) in $GameDir"
} finally {
    Remove-Item -LiteralPath $download -Recurse -Force -ErrorAction SilentlyContinue
    if ($staging) { Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue }
    if ($locked) { Remove-Item -LiteralPath $lock -Force -ErrorAction SilentlyContinue }
}
