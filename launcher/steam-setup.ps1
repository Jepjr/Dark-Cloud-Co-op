# Dark Cloud Co-op: Steam setup, run by "Set up Steam.bat" beside the launcher, only when the player runs it (Jep, 2026-11-03:
# nothing about Steam is set up for the player; they run this themselves). Steam joins need three things, and this script
# checks each and asks before changing anything (No is the default: just pressing Enter changes nothing):
#   1. Steam running (only said: start Steam yourself).
#   2. Valve's steam_api64.dll in game\, which the mod cannot ship: downloaded from the Steamworks.NET Standalone 2025.164.1
#      release on GitHub, and only a steam_api64.dll whose SHA-256 is that release's is kept.
#   3. Spacewar (Valve's free sample app, AppID 480) in Steam: the mod plays over Steam as Spacewar, so Steam friends see
#      you "playing Spacewar". Installed by Steam itself (steam://install/480 opens Steam's own install window).
# The game shows the same three checks in its lobby and at MULTIPLAYER > JOIN > STEAM.
# Test switches (a player never sets them): DCO_STEAM_SETUP_ANSWERS (the answers in order, one letter each, '-' for Enter),
# DCO_STEAM_SETUP_CHECK=r,s (Steam running and Spacewar installed, 0 or 1, in place of the registry), DCO_STEAM_SETUP_NO_OPEN
# (says the steam:// link instead of opening it), DC_STEAM_DLL_URL (a file:// URL too), DC_STEAM_DLL_SHA256, and
# DC_CONSENT_MUTATE=default_yes (the twin: Enter means yes).
$ErrorActionPreference = 'Stop'
$mod = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $mod 'game\steam_api64.dll'
$url = if ($env:DC_STEAM_DLL_URL) { $env:DC_STEAM_DLL_URL } else { 'https://github.com/rlabrecque/Steamworks.NET/releases/download/2025.164.1/Steamworks.NET-Standalone_2025.164.1.zip' }
$want = if ($env:DC_STEAM_DLL_SHA256) { $env:DC_STEAM_DLL_SHA256 } else { 'EB17909A76668CF9AE0B92A618A34A50F6C73D3A6787CB4DD8CE36A8B10BFB75' }
$answers = "$env:DCO_STEAM_SETUP_ANSWERS"
$script:answerAt = 0

function Ask([string]$Question) {
    $reply = if ($answers) {
        $c = if ($script:answerAt -lt $answers.Length) { $answers[$script:answerAt] } else { '-' }
        $script:answerAt++
        $r = if ($c -eq '-') { '' } else { "$c" }
        Write-Host "$Question [y/N] $r"
        $r
    } else {
        Read-Host "$Question [y/N]"
    }
    $yes = if ($env:DC_CONSENT_MUTATE -eq 'default_yes') { $reply -notmatch '^\s*[nN]' } else { $reply -match '^\s*[yY]' }
    Write-Host "  steam-setup: answered $(if ($yes) { 'yes' } else { 'no' })"
    $yes
}

function Get-SteamState {
    if ($env:DCO_STEAM_SETUP_CHECK) {
        $r, $s = $env:DCO_STEAM_SETUP_CHECK -split ','
        return @{ Running = $r -eq '1'; Spacewar = $s -eq '1' }
    }
    $running = $false
    try {
        $steamPid = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam\ActiveProcess' -Name pid -ErrorAction Stop).pid
        $running = $steamPid -ne 0 -and [bool](Get-Process -Id $steamPid -ErrorAction SilentlyContinue)
    } catch { }
    $spacewar = $false
    try { $spacewar = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam\Apps\480' -Name Installed -ErrorAction Stop).Installed -eq 1 } catch { }
    @{ Running = $running; Spacewar = $spacewar }
}

# (.NET's SHA-256, not Get-FileHash: its module may not load when the .bat is started from PowerShell 7)
function Get-Sha256([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $file = [IO.File]::OpenRead($Path)
    try { -join ($sha.ComputeHash($file) | ForEach-Object { $_.ToString('X2') }) } finally { $file.Dispose(); $sha.Dispose() }
}

function Get-SteamDll {
    $work = Join-Path $mod 'game\steam-dll-download'
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    New-Item -ItemType Directory -Force $work | Out-Null
    try {
        $zip = Join-Path $work 'steamworks.zip'
        Write-Host "  steam-setup: fetching $url"
        $proto = if ($env:DC_STEAM_DLL_URL) { '=https,file' } else { '=https' }
        & "$env:SystemRoot\System32\curl.exe" -L --fail --silent --show-error --max-time 120 --proto $proto --proto-redir '=https' -o $zip $url
        if ($LASTEXITCODE) { throw "the download failed (curl $LASTEXITCODE)" }
        & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $work
        if ($LASTEXITCODE) { throw 'the download was not a zip' }
        # Only Valve's steam_api64.dll of that release: every one in the zip is hashed, one must match.
        $found = $null
        foreach ($f in Get-ChildItem -LiteralPath $work -Recurse -File -Filter 'steam_api64.dll') {
            $hash = Get-Sha256 $f.FullName
            $same = $hash -eq $want
            Write-Host "  steam-setup: $($f.FullName.Substring($work.Length + 1)) SHA-256 $hash$(if ($same) { " (the release's)" })"
            if ($same -and -not $found) { $found = $f.FullName }
        }
        if (-not $found) { throw 'the download was not the expected file' }
        Copy-Item -LiteralPath $found -Destination $dll -Force
        Write-Host "  steam-setup: fetched: $dll"
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host 'Dark Cloud Co-op: Steam setup'
Write-Host 'Steam joins need Steam running, Valve''s steam_api64.dll in the game folder, and Spacewar installed in Steam.'
Write-Host 'IP, NAT code and Local play need none of this.'
Write-Host ''
$state = Get-SteamState
Write-Host "  steam-setup: Steam running $(if ($state.Running) { 'yes' } else { 'no' }), steam_api64.dll $(if (Test-Path -LiteralPath $dll) { 'yes' } else { 'no' }), Spacewar installed $(if ($state.Spacewar) { 'yes' } else { 'no' })"

if (Test-Path -LiteralPath $dll) {
    Write-Host 'steam_api64.dll: already in the game folder.'
} elseif (Ask 'steam_api64.dll: download it from GitHub (Steamworks.NET 2025.164.1, about 0.5 MB)?') {
    try { Get-SteamDll; Write-Host 'steam_api64.dll: done.' } catch { Write-Host "  steam-setup: fetch failed: $_"; Write-Host "steam_api64.dll: $_. Nothing was changed." }
} else {
    Write-Host 'steam_api64.dll: not downloaded. You can also put it in the game folder yourself (README.md).'
}

if ($state.Spacewar) {
    Write-Host 'Spacewar: already installed.'
} elseif (Ask 'Spacewar: install it in Steam (Valve''s free sample app, about 20 MB)? Friends see you "playing Spacewar" while you play over Steam.') {
    if ($env:DCO_STEAM_SETUP_NO_OPEN) { Write-Host '  steam-setup: would open steam://install/480' }
    else { Start-Process 'steam://install/480'; Write-Host 'Spacewar: Steam opens its install window; confirm it there.' }
} else {
    Write-Host 'Spacewar: not installed.'
}

if (-not $state.Running) { Write-Host 'Steam is not running: start Steam and sign in before playing over Steam.' }
Write-Host '  steam-setup: done'
