[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$GameDir,
    [switch]$ReplaceExisting,
    [switch]$Launch,
    [int]$CheckAfter,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$LaunchCommand
)

$ErrorActionPreference = 'Stop'
$repo = 'https://github.com/noahhhi/isaac-highfps/releases'
$launcher = 'isaac-highfps.ps1'
$versionState = '.isaac-highfps-game-version'
$managedState = '.isaac-highfps-managed-sha256'

if ($Launch) {
    $gameExe = Join-Path $PSScriptRoot 'isaac-ng.exe'
    $gameArgs = @()
    if ($LaunchCommand.Count -gt 0) {
        $gameExe = $LaunchCommand[0]
        $gameArgs = @($LaunchCommand | Select-Object -Skip 1)
    }
    $shell = (Get-Process -Id $PID).Path
    $worker = @{
        FilePath = $shell
        ArgumentList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                         ('"{0}"' -f $PSCommandPath), '-CheckAfter', "$PID")
        ErrorAction = 'SilentlyContinue'
    }
    if ($env:OS -eq 'Windows_NT') { $worker.WindowStyle = 'Hidden' }
    Start-Process @worker
    Push-Location -LiteralPath $PSScriptRoot
    try { & $gameExe @gameArgs; exit $LASTEXITCODE }
    finally { Pop-Location }
}
if ($CheckAfter) { $GameDir = $PSScriptRoot }
if (-not $GameDir) {
    $roots = @(
        (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
        (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath
        $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} Steam })
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
$ini = Join-Path $GameDir 'isaac-highfps.ini'
$log = Join-Path $GameDir 'isaac-highfps-update.log'

function Get-GameVersion {
    $ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($game))
    $result = [regex]::Match($ascii, 'Binding of Isaac: Repentance\+ v([0-9]+(?:\.[0-9]+){3}\.J[0-9]+)')
    return $result.Groups[1].Value
}
function Test-AutoUpdate {
    $highfps = $false
    if (Test-Path -LiteralPath $ini -PathType Leaf) {
        foreach ($line in [IO.File]::ReadLines($ini)) {
            if ($line -match '^\s*\[') { $highfps = $line -match '^\s*\[highfps\]\s*$' }
            if ($highfps -and $line -match '^\s*(AutoUpdate|Enabled)\s*=\s*0\s*(;.*)?$') { return $false }
        }
    }
    return $true
}
function Write-Status([string]$Message) {
    if ($CheckAfter) { Add-Content -LiteralPath $log -Value $Message } else { Write-Host $Message }
}
function Assert-GameClosed {
    foreach ($process in Get-Process -Name isaac-ng -ErrorAction SilentlyContinue) {
        if (-not $process.Path -or $process.Path -eq $game) { throw 'Exit Isaac before installing.' }
    }
}
function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Read-State([string]$Name) {
    $path = Join-Path $GameDir $Name
    if (Test-Path -LiteralPath $path -PathType Leaf) { return [IO.File]::ReadAllText($path).Trim() }
    return ''
}
function Set-State([string]$Name, [string]$Value) {
    $path = Join-Path $GameDir $Name
    $pending = Join-Path $GameDir ('.isaac-highfps-state-' + [guid]::NewGuid())
    [IO.File]::WriteAllText($pending, "$Value" + [Environment]::NewLine)
    if ([IO.File]::Exists($path)) {
        $prior = "$pending.previous"
        [IO.File]::Replace($pending, $path, $prior)
        Remove-Item -LiteralPath $prior -Force -ErrorAction SilentlyContinue
    } else {
        [IO.File]::Move($pending, $path)
    }
}
function Get-Asset([string]$Url, [string]$Destination) {
    & curl.exe --disable --proto '=https' --proto-redir '=https' --fail --silent --show-error --location --connect-timeout 8 --max-time 45 --max-filesize 16777216 --output $Destination $Url
    if ($LASTEXITCODE -ne 0) { throw "Download failed ($LASTEXITCODE)." }
}
function Install-File([string]$Name) {
    $from = Join-Path $staging $Name
    $to = Join-Path $GameDir $Name
    if ([IO.File]::Exists($to)) {
        [IO.File]::Replace($from, $to, (Join-Path $staging "$Name.replaced"))
    } else {
        [IO.File]::Move($from, $to)
    }
}

$download = $null
$staging = $null
$held = $null
try {
    if ($CheckAfter) {
        $owner = Get-Process -Id $CheckAfter -ErrorAction SilentlyContinue
        if (-not (Test-AutoUpdate)) { return }
        $version = Get-GameVersion
        if (-not $version -or (Read-State $versionState) -eq $version) { return }
        $checkLock = Join-Path $GameDir '.isaac-highfps-check.lock'
        try { [void](New-Item -Path $checkLock -ItemType Directory) }
        catch [System.IO.IOException] { return }
        $held = $checkLock
        if ((Read-State $versionState) -eq $version) { return }
        Set-State $versionState $version
        Write-Status "Checking GitHub for game version $version."
    } else {
        Assert-GameClosed
        $version = Get-GameVersion
        if (-not $version) { throw 'Could not read the Isaac version.' }
    }
    $download = Join-Path ([IO.Path]::GetTempPath()) ("isaac-highfps-" + [guid]::NewGuid())
    [void][IO.Directory]::CreateDirectory($download)
    $manifest = Join-Path $download 'highfps-builds.txt'
    Get-Asset "$repo/latest/download/highfps-builds.txt" $manifest
    $entries = [IO.File]::ReadAllLines($manifest)
    if ($entries[0] -cne 'isaac-highfps-builds-v1') { throw 'Invalid release manifest.' }
    $entry = $entries | Where-Object { ($_ -split '\s+', 2)[0] -ceq $version } | Select-Object -First 1
    if (-not $entry) {
        $message = "No stable HighFPS release for Isaac $version. Existing files kept."
        if ($CheckAfter) { Write-Status $message; return }
        throw $message
    }
    $fields = $entry -split '\s+'
    if ($fields.Length -ne 5 -or $fields[2] -cnotmatch '^[a-zA-Z0-9][a-zA-Z0-9.-]{0,63}$') {
        throw 'Invalid release entry.'
    }
    $listed, $gameHash, $tag, $dllHash, $iniHash = $fields
    foreach ($checksum in @($gameHash, $dllHash, $iniHash)) {
        if ($checksum -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid release checksum.' }
    }
    if ((Get-Sha256 $game) -cne $gameHash) {
        throw 'Game executable differs from the supported build. Existing files kept.'
    }
    $dll = Join-Path $GameDir 'winmm.dll'
    if ($CheckAfter -and (Test-Path -LiteralPath $dll -PathType Leaf) -and (Get-Sha256 $dll) -ceq $dllHash) {
        Write-Status 'The correct DLL is already present.'
        return
    }

    $assets = "$repo/download/$tag"
    $dllAsset = Join-Path $download 'winmm.dll'
    Get-Asset "$assets/winmm.dll" $dllAsset
    if ((Get-Sha256 $dllAsset) -cne $dllHash) { throw 'DLL checksum mismatch.' }
    $iniAsset = Join-Path $download 'isaac-highfps.ini'
    if (-not (Test-Path -LiteralPath $ini)) {
        Get-Asset "$assets/isaac-highfps.ini" $iniAsset
        if ((Get-Sha256 $iniAsset) -cne $iniHash) { throw 'INI checksum mismatch.' }
    }

    if ($CheckAfter) {
        Write-Status 'Verified the download. Waiting for Isaac to close.'
        if ($owner -and -not $owner.HasExited) { Wait-Process -InputObject $owner -ErrorAction SilentlyContinue }
        if (-not (Test-AutoUpdate)) { return }
        Remove-Item -LiteralPath $held -Force
        $held = $null
    }
    $installLock = Join-Path $GameDir '.isaac-highfps-install.lock'
    [void](New-Item -Path $installLock -ItemType Directory)
    $held = $installLock
    Assert-GameClosed
    if ((Get-Sha256 $game) -cne $gameHash) { throw 'Game executable changed during download.' }
    $files = @('winmm.dll', 'isaac-highfps.ini', $launcher)
    foreach ($name in $files) {
        $target = Get-Item -LiteralPath (Join-Path $GameDir $name) -Force -ErrorAction SilentlyContinue
        if ($target -and ($target.PSIsContainer -or ($target.Attributes -band [IO.FileAttributes]::ReparsePoint))) {
            throw "Cannot replace $name. Expected an ordinary file."
        }
    }
    if ((Test-Path -LiteralPath $dll) -and -not $ReplaceExisting) {
        $known = @($dllHash, (Read-State $managedState),
            '717cd2a2cd2f9592a7f106229f811d10e48117fd9226dd05c7bed90312e80c45',
            'd1273d694d9d86c8b137ceb68d3f32575316ee749fb3c634dca736f3a91d78ae',
            'c4523d848b97242c26bc88c7f35461f23120aaf0e4ef6a53cce197ae4d42e97b',
            'd1126945479e0d7ee6b60e474cd62d7ea6671afed7df103a61006678629ec8f4')
        if ((Get-Sha256 $dll) -notin $known) {
            throw 'Existing winmm.dll differs. To back it up and continue, run the installer with -ReplaceExisting.'
        }
    }

    $staging = Join-Path $GameDir ('.isaac-highfps-install-' + [guid]::NewGuid())
    [void][IO.Directory]::CreateDirectory($staging)
    Copy-Item -LiteralPath $dllAsset -Destination $staging
    if ((Test-Path -LiteralPath $iniAsset) -and -not (Test-Path -LiteralPath $ini)) {
        Copy-Item -LiteralPath $iniAsset -Destination $staging
    }
    if (-not $CheckAfter) { Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $staging $launcher) }
    $backup = $null
    foreach ($name in $files) {
        $target = Join-Path $GameDir $name
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            if (-not $backup) {
                $backup = Join-Path $GameDir ('highfps-backup-' + [guid]::NewGuid())
                [void][IO.Directory]::CreateDirectory($backup)
            }
            Copy-Item -LiteralPath $target -Destination $backup
        }
    }
    if ($backup) { Write-Status "Backup saved in $backup" }
    foreach ($name in @('isaac-highfps.ini', $launcher, 'winmm.dll')) {
        if (Test-Path -LiteralPath (Join-Path $staging $name)) { Install-File $name }
    }
    Set-State $managedState $dllHash
    Set-State $versionState $version
    Write-Status "Installed HighFPS $tag for Isaac $listed in $GameDir"
    if (-not $CheckAfter) {
        Write-Host ('To enable launch-time version checks, set Steam Launch Options to:' +
            [Environment]::NewLine +
            ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{0}" -Launch %command%' -f
             (Join-Path $GameDir $launcher)))
    }
} catch {
    if (-not $CheckAfter) { throw }
    if ($held) { Write-Status $_.Exception.Message }
} finally {
    if ($download) { Remove-Item -LiteralPath $download -Recurse -Force -ErrorAction SilentlyContinue }
    if ($staging) { Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue }
    if ($held) { Remove-Item -LiteralPath $held -Force -ErrorAction SilentlyContinue }
}
