# Dark Cloud Co-op setup: adds the mod to a Chronicle install (the Dark Cloud decompilation's PC port), building it on this
# PC from source. Run by "Set up Dark Cloud Co-op.bat" beside it, in Windows PowerShell 5.1 (every Windows 10/11 has it).
#
# The layout (release3c, Jep 11-10): the mod lives only in <Chronicle>\mods\Dark Cloud Co-op (the launcher, game\ with the
# built exe and its DLLs, save\ with settings and log: about 25 MB). It uses the Chronicle checkout itself, Chronicle's own
# toolchain folder (<Chronicle>\.kazarov\windows, where its build.bat keeps it) and Chronicle's game data
# (<Chronicle>\port\run\data). It never downloads over a Chronicle folder and never makes a copy of one.
#
# In order (each step says what it does and stops with a plain line when it cannot):
#   1. Your Chronicle: the checkout this setup sits in (unzipped into <Chronicle>\mods\), else the one you pick, else none.
#      It must be at the commit this release is pinned to, and its game data (if it has any) must be the NTSC (US 1.02)
#      disc's as that commit extracts it: otherwise the setup stops, writing nothing, and says what you have, what is
#      needed and how to get it. With no Chronicle at all, it sets up Chronicle at the pin in this folder's Chronicle\.
#   2. Your disc image, only when Chronicle has no game data yet: the NTSC (US) disc, version 1.02.
#   3. What it downloads, asked once: portable Git, PowerShell 7, CMake, Python and 7-Zip when this PC has none (each
#      checked by SHA-256), Chronicle when you have none, and Chronicle's pinned compiler toolchain when its toolchain
#      folder lacks it (Chronicle's tools\windows\acquire.ps1 without its last step, the Rust toolchain: nothing here needs
#      Rust; Chronicle's own build.bat adds it later if you build Chronicle itself).
#   4. The build: the mod's source (setup\mod.bundle) in <Chronicle>\mods\Dark Cloud Co-op.build, built there with the
#      mod's build\build.ps1 -NoDcdata (Chronicle at the pin plus the mod's files and recipes). That folder is temporary.
#   5. The game data, only when Chronicle has none: your disc extracted into <Chronicle>\port\run\data by the release's
#      disc extractor (setup\dcdata.exe: Chronicle's dcdata, prebuilt from Chronicle's sources at the pin and checked by
#      its SHA-256 in setup\release.txt; it is the one part that links Rust).
#   6. The mod folder, <Chronicle>\mods\Dark Cloud Co-op: the launcher, game\, the README; an earlier release's save\ kept.
#   7. The temporary build folder deleted (it holds no links; checked first).
#   8. Steam joins (optional): only if you say yes, "Set up Steam.bat" runs, and it asks again before each change.
# Two people running the same release build the same darkcloud.exe, byte for byte (its SHA-256 is printed at the end).
#
# Folders it makes beside itself: tools\ (portable tools), downloads\ (what it downloaded), Chronicle\ (only when you had
# no Chronicle), setup-log.txt. Run it again after a failure: what is done is kept. Your disc image is only read.
# Test switches (a player never sets them): DCO_SETUP_YES=1 (no questions: yes to the downloads, no to Steam),
# DCO_SETUP_CHRONICLE (the Chronicle checkout to use; "none" for none), DCO_SETUP_ISO (the disc image),
# DCO_SETUP_PORTABLE=1 (the portable tools even where the PC has its own), DCO_SETUP_MOD_REPO (another place to clone the
# mod from than setup\mod.bundle or the release's URL), DCO_SETUP_JOBS (the build's parallel jobs).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Here = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Log = Join-Path $Here 'setup-log.txt'
$Auto = $env:DCO_SETUP_YES -eq '1'
$t0 = Get-Date
$script:steps = @()

function Say([string]$Text) { Write-Host $Text; Add-Content -LiteralPath $Log -Value "$(Get-Date -Format 'HH:mm:ss') $Text" }
function Stop-Setup([string]$Why, [string]$Help = '') {
    Say ''
    Say "SETUP STOPPED: $Why"
    if ($Help) { Say $Help }
    Say "(The whole log is in $Log. Run the setup again once it is fixed: finished steps are kept.)"
    exit 1
}
# One step: its name, its time, and any error it throws said as the reason the setup stopped.
function Step([string]$Name, [scriptblock]$Body) {
    Say ''
    Say "== $Name"
    $s = Get-Date
    try { & $Body } catch { Stop-Setup "$Name failed: $($_.Exception.Message)" }
    $secs = [int]((Get-Date) - $s).TotalSeconds
    $script:steps += "$Name $secs s"
    Say "   ($secs s)"
}
# A file's SHA-256 in capitals (by .NET itself: Get-FileHash is missing when Windows PowerShell inherits another's modules).
function Get-Sha256([string]$Path) {
    $s = [IO.File]::OpenRead($Path)
    try { return -join ([Security.Cryptography.SHA256]::Create().ComputeHash($s) | ForEach-Object { $_.ToString('X2') }) } finally { $s.Close() }
}
# Whether git finds a commit in a repository (no output, no error).
function Test-Commit([string]$Repo, [string]$Commit) {
    $ErrorActionPreference = 'Continue'
    & $git -C $Repo cat-file -e "$Commit^{commit}" 2>&1 | Out-Null
    return $LASTEXITCODE -eq 0
}
function Ask([string]$Question, [bool]$Default) {
    if ($Auto) { Say "$Question $(if ($Default) { 'yes' } else { 'no' }) (DCO_SETUP_YES)"; return $Default }
    $r = Read-Host "$Question $(if ($Default) { '[Y/n]' } else { '[y/N]' })"
    if ($Default) { return $r -notmatch '^\s*[nN]' } else { return $r -match '^\s*[yY]' }
}
# A program's run, its output into the log; stops the step with its last lines when it fails.
function Run([string]$Exe, [string[]]$Arguments, [string]$What) {
    Add-Content -LiteralPath $Log -Value "> $Exe $($Arguments -join ' ')"
    $ErrorActionPreference = 'Continue'   # (Windows PowerShell turns a program's stderr lines into errors)
    $out = & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($out) { Add-Content -LiteralPath $Log -Value $out }
    if ($code) { throw "$What (exit code $code): $(@($out | Select-Object -Last 6) -join ' | ')" }
    return $out
}
# A long program's run (Jep 11-10: the window must never look frozen), its output into the log as Run's: every half second
# $Status gives (text, percent or -1) for a bar (Write-Progress) and, for consoles that draw no bar, a plain line when the
# percent moves 5 points or 15 s pass ("Building the game: 812 / 1450 (56%)", or the text and the time so far).
function Format-Elapsed([double]$Secs) { '{0}:{1:00}' -f [int][math]::Floor($Secs / 60), [int][math]::Floor($Secs % 60) }
function Run-Watched([string]$Exe, [string[]]$Arguments, [string]$What, [string]$Activity, [scriptblock]$Status) {
    Add-Content -LiteralPath $Log -Value "> $Exe $($Arguments -join ' ')"
    $quoted = @($Arguments | ForEach-Object { if ($_ -match '[\s"]' -or $_ -eq '') { '"' + (($_ -replace '"', '\"') -replace '\\$', '\\') + '"' } else { $_ } }) -join ' '
    $outFile = [IO.Path]::GetTempFileName(); $errFile = [IO.Path]::GetTempFileName()
    $p = Start-Process -FilePath $Exe -ArgumentList $quoted -NoNewWindow -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    $null = $p.Handle   # (keeps the exit code readable once it ends)
    $s = Get-Date; $lastPct = -100; $lastSaid = $s; $saved = $ProgressPreference; $ProgressPreference = 'Continue'
    try {
        while (-not $p.WaitForExit(500)) {
            $secs = ((Get-Date) - $s).TotalSeconds
            $st = @(& $Status $outFile $secs)
            $text = "$($st[0])"; $pct = if ($st.Count -gt 1) { [int]$st[1] } else { -1 }
            if ($pct -ge 0) { Write-Progress -Activity $Activity -Status $text -PercentComplete ([Math]::Min(100, $pct)) }
            else { Write-Progress -Activity $Activity -Status "$text ($(Format-Elapsed $secs))" }
            if (($pct -ge 0 -and $pct -ge $lastPct + 5) -or ((Get-Date) - $lastSaid).TotalSeconds -ge 15) {
                Say "   $text, $(Format-Elapsed $secs)"
                $lastSaid = Get-Date; if ($pct -ge 0) { $lastPct = $pct }
            }
        }
    } finally { Write-Progress -Activity $Activity -Completed; $ProgressPreference = $saved }
    $p.WaitForExit()
    $code = $p.ExitCode
    $out = @(Get-Content -LiteralPath $outFile -ErrorAction SilentlyContinue) + @(Get-Content -LiteralPath $errFile -ErrorAction SilentlyContinue)
    foreach ($f in $outFile, $errFile) { [IO.File]::Delete($f) }
    if ($out) { Add-Content -LiteralPath $Log -Value $out }
    if ($code) { throw "$What (exit code $code): $(@($out | Select-Object -Last 6) -join ' | ')" }
    return $out
}
# The last line matching $Pattern of a file another program is writing (shared read), or ''.
function Get-LastLine([string]$Path, [string]$Pattern = '\S') {
    try {
        $fs = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
        try {
            $len = $fs.Length; $take = [int][Math]::Min($len, 16384); $null = $fs.Seek($len - $take, 'Begin')
            $buf = New-Object byte[] $take; $null = $fs.Read($buf, 0, $take)
        } finally { $fs.Close() }
        $lines = @([Text.Encoding]::UTF8.GetString($buf) -split "`r?`n" | Where-Object { $_ -match $Pattern })
        if ($lines.Count) { return "$($lines[-1])".Trim() }
    } catch { }
    return ''
}
# A folder's size in bytes (0 when it is missing).
function Get-FolderBytes([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return 0 }
    $sum = (Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
    if ($sum) { return [int64]$sum } else { return 0 }
}
function Format-Mb([int64]$Bytes) { return "$([math]::Round($Bytes / 1MB)) MB" }
# Deletes a folder this setup made, only when nothing inside it is a link or junction (a delete never follows one).
function Remove-OwnFolder([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    if ((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "$Path is a link: not deleted" }
    $links = @(Get-ChildItem -LiteralPath $Path -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue)
    if ($links) { throw "$Path holds a link ($($links[0].FullName)): not deleted" }
    Remove-Item -LiteralPath $Path -Recurse -Force
}

$release = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release.txt')) {
    if ($line -match '^\s*([\w-]+)\s+(.+?)\s*$') { $release[$Matches[1]] = $Matches[2] }
}
foreach ($k in 'version', 'mod-commit', 'chronicle', 'chronicle-url', 'dcdata-sha256', 'data-release', 'data-models-sha256') {
    if (-not $release[$k]) { Stop-Setup "setup\release.txt has no $k line: download the release again" }
}
$pin = $release.chronicle
Set-Content -LiteralPath $Log -Value "Dark Cloud Co-op setup $($release.version) (mod $($release['mod-commit'])), $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
Say "Dark Cloud Co-op setup, release $($release.version), for Chronicle $($pin.Substring(0, 8))"
if (-not [Environment]::Is64BitOperatingSystem -or [Environment]::OSVersion.Version.Major -lt 10) { Stop-Setup 'Dark Cloud Co-op needs 64-bit Windows 10 or 11.' }
# The release's disc extractor (prebuilt, so the setup downloads no Rust), checked before anything else.
$dcdata = Join-Path $PSScriptRoot 'dcdata.exe'
if (-not (Test-Path -LiteralPath $dcdata)) { Stop-Setup 'setup\dcdata.exe (the disc extractor) is missing: download the release again and unzip all of it.' }
if ((Get-Sha256 $dcdata) -ne $release['dcdata-sha256'].ToUpperInvariant()) { Stop-Setup 'setup\dcdata.exe is not the release''s own disc extractor (its SHA-256 differs): download the release again.' }

# The disc's SYSTEM.CNF (BOOT2's program name, VER, VMODE), read from the ISO 9660 root, or $null.
function Read-DiscId([string]$Path) {
    $f = [IO.File]::OpenRead($Path)
    try {
        $buf = New-Object byte[] 2048
        $f.Position = 16 * 2048; [void]$f.Read($buf, 0, 2048)
        if ([Text.Encoding]::ASCII.GetString($buf, 1, 5) -ne 'CD001') { return $null }
        $rootLba = [BitConverter]::ToUInt32($buf, 156 + 2); $rootSize = [BitConverter]::ToUInt32($buf, 156 + 10)
        if ($rootSize -gt 1MB) { return $null }
        $dir = New-Object byte[] $rootSize
        $f.Position = [int64]$rootLba * 2048; [void]$f.Read($dir, 0, $rootSize)
        $i = 0
        while ($i -lt $rootSize) {
            $len = $dir[$i]
            if ($len -eq 0) { $i = ([math]::Floor($i / 2048) + 1) * 2048; continue }
            $name = [Text.Encoding]::ASCII.GetString($dir, $i + 33, $dir[$i + 32])
            if ($name -like 'SYSTEM.CNF*') {
                $lba = [BitConverter]::ToUInt32($dir, $i + 2); $size = [math]::Min([BitConverter]::ToUInt32($dir, $i + 10), 4096)
                $cnf = New-Object byte[] $size
                $f.Position = [int64]$lba * 2048; [void]$f.Read($cnf, 0, $size)
                $text = [Text.Encoding]::ASCII.GetString($cnf)
                return [pscustomobject]@{
                    Boot = $(if ($text -match 'BOOT2\s*=\s*cdrom0:\\([^;\s]+)') { $Matches[1] } else { '?' })
                    Version = $(if ($text -match 'VER\s*=\s*(\S+)') { $Matches[1] } else { '?' })
                    Mode = $(if ($text -match 'VMODE\s*=\s*(\S+)') { $Matches[1] } else { '?' })
                }
            }
            $i += $len
        }
        return $null
    } finally { $f.Close() }
}

# Whether a folder is a Chronicle checkout (its kazarov.conf, port\ and the Windows scripts).
function Test-Chronicle([string]$Dir) {
    return (Test-Path -LiteralPath (Join-Path $Dir 'kazarov.conf') -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $Dir 'port') -PathType Container) -and
           (Test-Path -LiteralPath (Join-Path $Dir 'tools\windows\acquire.ps1') -PathType Leaf)
}
# The commit a checkout has out, read from its .git without git (a worktree's .git file, a loose ref or packed-refs), or ''.
function Get-GitHead([string]$Dir) {
    $gitDir = Join-Path $Dir '.git'
    if (Test-Path -LiteralPath $gitDir -PathType Leaf) {
        $line = (Get-Content -LiteralPath $gitDir -TotalCount 1)
        if ($line -notmatch '^gitdir:\s*(.+)$') { return '' }
        $gitDir = $Matches[1].Trim()
        if (-not [IO.Path]::IsPathRooted($gitDir)) { $gitDir = Join-Path $Dir $gitDir }
    }
    $headFile = Join-Path $gitDir 'HEAD'
    if (-not (Test-Path -LiteralPath $headFile)) { return '' }
    $head = "$(Get-Content -LiteralPath $headFile -TotalCount 1)".Trim()
    if ($head -match '^[0-9a-f]{40}$') { return $head }
    if ($head -notmatch '^ref:\s*(\S+)$') { return '' }
    $ref = $Matches[1]
    # A worktree's own HEAD names a ref of the main repository (its commondir).
    $common = $gitDir
    $commonFile = Join-Path $gitDir 'commondir'
    if (Test-Path -LiteralPath $commonFile) { $c = "$(Get-Content -LiteralPath $commonFile -TotalCount 1)".Trim(); $common = if ([IO.Path]::IsPathRooted($c)) { $c } else { Join-Path $gitDir $c } }
    foreach ($g in $gitDir, $common) {
        $refFile = Join-Path $g ($ref -replace '/', '\')
        if (Test-Path -LiteralPath $refFile -PathType Leaf) { return "$(Get-Content -LiteralPath $refFile -TotalCount 1)".Trim() }
        $packed = Join-Path $g 'packed-refs'
        if (Test-Path -LiteralPath $packed) {
            foreach ($l in Get-Content -LiteralPath $packed) { if ($l -match "^([0-9a-f]{40}) $([regex]::Escape($ref))$") { return $Matches[1] } }
        }
    }
    return ''
}
# The data's release (languages.json's "release", e.g. "NTSC 1.02") or ''.
function Get-DataRelease([string]$Data) {
    $f = Join-Path $Data 'languages.json'
    if (-not (Test-Path -LiteralPath $f)) { return '' }
    if ((Get-Content -Raw -LiteralPath $f) -match '"release"\s*:\s*"([^"]*)"') { return $Matches[1] }
    return ''
}
# Why a data folder is not the one this release plays ('' when it is): the disc's release, then dcdata's models index as
# Chronicle at the pin writes it for that disc (models.json's SHA-256 in release.txt).
function Get-DataMismatch([string]$Data) {
    $rel = Get-DataRelease $Data
    if ($rel -ne $release['data-release']) { return "it is from a $(if ($rel) { "$rel disc" } else { 'disc of an unknown release (no languages.json)' })" }
    $models = Join-Path $Data 'models.json'
    if (-not (Test-Path -LiteralPath $models)) { return "it has no models.json (extracted by a Chronicle older than $($pin.Substring(0, 8)))" }
    if ((Get-Sha256 $models) -ne $release['data-models-sha256'].ToUpperInvariant()) { return "it was extracted by another Chronicle version than $($pin.Substring(0, 8)) (its models.json differs)" }
    return ''
}

# 1. Your Chronicle: the checkout around this setup, the one named or picked, or none.
$chronicle = $null
$cloneChronicle = $false
$named = $env:DCO_SETUP_CHRONICLE
if ($named -and $named -ne 'none') {
    $chronicle = [IO.Path]::GetFullPath($named)
    if (-not (Test-Chronicle $chronicle)) { Stop-Setup "$chronicle is not a Chronicle checkout (no kazarov.conf, port\ and tools\windows\acquire.ps1)." }
} elseif (-not $named) {
    for ($d = $Here; $d; $d = Split-Path $d) { if (Test-Chronicle $d) { $chronicle = $d; break } }
    if (-not $chronicle -and (Test-Chronicle (Join-Path $Here 'Chronicle'))) { $chronicle = Join-Path $Here 'Chronicle' }   # (an earlier run's)
    if (-not $chronicle -and -not $Auto -and (Ask 'Do you already have Chronicle (the Dark Cloud PC port) on this PC?' $false)) {
        Add-Type -AssemblyName System.Windows.Forms
        $pick = New-Object System.Windows.Forms.FolderBrowserDialog
        $pick.Description = 'Dark Cloud Co-op: pick your Chronicle folder (the one with build.bat and kazarov.conf)'
        if ($pick.ShowDialog() -ne 'OK') { Stop-Setup 'No Chronicle folder picked.' 'Run the setup again and pick it, or say no to set Chronicle up here.' }
        $chronicle = $pick.SelectedPath
        if (-not (Test-Chronicle $chronicle)) { Stop-Setup "$chronicle is not a Chronicle checkout (no kazarov.conf, port\ and tools\windows\acquire.ps1)." 'Pick the folder that holds build.bat and kazarov.conf.' }
    }
}
if (-not $chronicle) { $chronicle = Join-Path $Here 'Chronicle'; $cloneChronicle = $true }
$data = Join-Path $chronicle 'port\run\data'
$mod = Join-Path $chronicle 'mods\Dark Cloud Co-op'
$work = Join-Path $chronicle 'mods\Dark Cloud Co-op.build'
$deps = Join-Path $chronicle '.kazarov\windows'
$needData = $true
Step '1/8 Your Chronicle' {
    if ($cloneChronicle) {
        Say "   none found: the setup sets up Chronicle at commit $($pin.Substring(0, 8)) in $chronicle"
        return
    }
    $head = Get-GitHead $chronicle
    Say "   $chronicle, commit $(if ($head) { $head.Substring(0, 8) } else { '(not a git checkout)' })"
    $how = "In a Command Prompt in $chronicle`n      git fetch origin`n      git checkout $pin`n   then run this setup again. (Or use the Dark Cloud Co-op release made for the Chronicle version you have.)"
    if (-not $head) { Stop-Setup "$chronicle is not a git checkout, so its version cannot be checked. This release needs Chronicle at commit $pin." "Clone it: git clone https://github.com/TheMoonPeople/Chronicle.git, then git checkout $pin in it, and run this setup again." }
    if ($head -ne $pin) { Stop-Setup "Your Chronicle is at commit $head; this release of Dark Cloud Co-op needs commit $pin. Nothing was changed." "   $how" }
    if (Test-Path -LiteralPath (Join-Path $data 'data.hd2')) {
        $why = Get-DataMismatch $data
        if ($why) {
            Stop-Setup "Your Chronicle's game data ($data) is not the data this release plays: $why. Nothing was changed." ("   This release plays the NTSC (US, version 1.02) disc's data as Chronicle $($pin.Substring(0, 8)) extracts it, only for now (PAL is not tested yet).`n" +
                "   To use it: move $data somewhere else (Chronicle itself then plays the new data too: this replaces your $(if ((Get-DataRelease $data) -match 'PAL') { 'PAL ' })data), put your NTSC disc image beside this setup, and run it again: it extracts the disc there.`n   Or wait for the Dark Cloud Co-op release that plays PAL.")
        }
        Say "   game data: $data, $($release['data-release']), as Chronicle $($pin.Substring(0, 8)) extracts it"
        $script:needData = $false
    } elseif (Test-Path -LiteralPath $data) {
        if (@(Get-ChildItem -LiteralPath $data -Force).Count) { Stop-Setup "$data holds files but no data.hd2 (an unfinished extraction?). Nothing was changed." "Move it somewhere else and run the setup again." }
    }
    # The mod's own folder: new, or an earlier release's (VERSION.txt, no source); never a source checkout.
    if (Test-Path -LiteralPath $mod) {
        if ((Test-Path -LiteralPath (Join-Path $mod '.git')) -or (Test-Path -LiteralPath (Join-Path $mod 'src'))) {
            Stop-Setup "$mod is a Dark Cloud Co-op source checkout: the setup leaves it alone. Nothing was changed." 'Build it with its build\build.ps1, or set the release up in another Chronicle folder.'
        }
        if (@(Get-ChildItem -LiteralPath $mod -Force).Count -and -not (Test-Path -LiteralPath (Join-Path $mod 'VERSION.txt'))) {
            Stop-Setup "$mod holds files that are not a Dark Cloud Co-op release's. Nothing was changed." 'Move them somewhere else and run the setup again.'
        }
        if (Test-Path -LiteralPath (Join-Path $mod 'VERSION.txt')) { Say "   an earlier release in $mod`: replaced, its save\ kept" }
    }
}

# The game data has a file whose path inside data\ is 139 characters (a Japanese name, escaped), and Windows' paths stop
# at 259: data\ must have a path of 120 characters or fewer when the setup extracts it, checked before any download.
$dataPart = Join-Path $chronicle 'port\run\data.part'
if ($needData -and $dataPart.Length -gt 120) {
    Stop-Setup "The game data's folder would be $data ($($dataPart.Length - 5) characters; at most 115 fit under Windows' path limit with the disc's longest file name)." "Move $(if ($cloneChronicle) { 'this setup folder somewhere with a shorter path, e.g. C:\Dark Cloud Co-op setup' } else { 'your Chronicle folder somewhere with a shorter path, e.g. C:\Chronicle' }), and run it again."
}

# 2. The disc image, only when Chronicle has no game data yet.
$iso = $null
if ($needData) {
    Step '2/8 Your Dark Cloud disc image' {
        $script:iso = $env:DCO_SETUP_ISO
        if (-not $script:iso) {
            $found = @(Get-ChildItem -LiteralPath $Here -Filter '*.iso' -File -ErrorAction SilentlyContinue) +
                     @(Get-ChildItem -LiteralPath (Join-Path $chronicle 'rom') -Filter '*.iso' -File -Recurse -ErrorAction SilentlyContinue)
            $found = @($found | Where-Object { $d = Read-DiscId $_.FullName; $d -and $d.Boot -eq 'SCUS_971.11' })
            if ($found) { $script:iso = $found[0].FullName }
        }
        if (-not $script:iso) {
            Say 'Pick your Dark Cloud disc image (.iso) in the window that opens.'
            Add-Type -AssemblyName System.Windows.Forms
            $pick = New-Object System.Windows.Forms.OpenFileDialog
            $pick.Title = 'Dark Cloud Co-op: pick your Dark Cloud disc image (NTSC, US 1.02)'
            $pick.Filter = 'Disc images (*.iso)|*.iso|All files (*.*)|*.*'
            if ($pick.ShowDialog() -ne 'OK') { Stop-Setup 'No disc image picked.' 'Run the setup again and pick your Dark Cloud disc image (.iso), or put it beside the setup.' }
            $script:iso = $pick.FileName
        }
        if (-not (Test-Path -LiteralPath $script:iso -PathType Leaf)) { Stop-Setup "There is no file $($script:iso)." }
        $disc = Read-DiscId $script:iso
        if (-not $disc) { Stop-Setup "$($script:iso) is not a PlayStation 2 disc image (no ISO 9660 SYSTEM.CNF)." 'Use an .iso made from your own Dark Cloud disc.' }
        Say "   $($script:iso): $($disc.Boot), version $($disc.Version), $($disc.Mode)"
        if ($disc.Boot -ne 'SCUS_971.11') {
            $what = if ($disc.Boot -match '^SCES_') { 'the PAL (European) Dark Cloud disc' } elseif ($disc.Boot -match '^SCPS_|^SLPS_') { 'a Japanese disc' } else { "not Dark Cloud's US disc ($($disc.Boot))" }
            Stop-Setup "This disc image is $what." 'Dark Cloud Co-op plays the NTSC (US) Dark Cloud disc, version 1.02, only for now: PAL is not tested yet.'
        }
        if ($disc.Version -ne '1.02') { Stop-Setup "This is the US disc, version $($disc.Version)." 'Dark Cloud Co-op plays version 1.02 of the US disc only (the version Chronicle decompiles).' }
    }
}

# 3. Tools. Each one: this PC's own when it has one new enough (unless DCO_SETUP_PORTABLE=1), else the portable one.
$tools = Join-Path $Here 'tools'
$downloads = Join-Path $Here 'downloads'
$portable = $env:DCO_SETUP_PORTABLE -eq '1'
$kTools = @(
    @{ Name = 'git'; Exe = 'git.exe'; Url = 'https://github.com/git-for-windows/git/releases/download/v2.47.1.windows.1/MinGit-2.47.1-64-bit.zip'; Sha = '50B04B55425B5C465D076CDB184F63A0CD0F86F6EC8BB4D5860114A713D2C29A'; Mb = 47; Bin = 'cmd' },
    @{ Name = 'pwsh'; Exe = 'pwsh.exe'; Url = 'https://github.com/PowerShell/PowerShell/releases/download/v7.4.6/PowerShell-7.4.6-win-x64.zip'; Sha = 'ED49CE5ADB2162CC4A835D740486BE729BA904627CCA71FCB6C2B95BE11B993D'; Mb = 111; Bin = '.' },
    @{ Name = 'cmake'; Exe = 'cmake.exe'; Url = 'https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip'; Sha = 'D163CD3AB4959B0A53FA8988F2DDBD2E6C501658201E6A154386BAD9DBE4F836'; Mb = 46; Bin = 'cmake-3.31.6-windows-x86_64\bin' },
    @{ Name = 'python'; Exe = 'python.exe'; Url = 'https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip'; Sha = '8D3F33BE9EB810F23C102F08475AF2854E50484B8E4E06275E937BE61CE3D2FB'; Mb = 11; Bin = '.' },
    @{ Name = '7z'; Exe = '7z.exe'; Url = 'https://github.com/ip7z/7zip/releases/download/24.08/7z2408-extra.7z'; Sha = 'DA995D41F5CA3A573B874644237E089A7F063C5DD29C6EFF4D0E2EA60ED902A9'; Mb = 2; Bin = '.' }
)
$k7zr = @{ Url = 'https://github.com/ip7z/7zip/releases/download/24.08/7zr.exe'; Sha = '1B16C41AE39B679384B06F1492B587B650716430FF9C2E079DCA2AD1F62C952D' }

# This PC's own tool, or $null: on PATH, new enough where it matters (PowerShell 7, CMake 3.28).
function Find-OwnTool([hashtable]$T) {
    if ($portable) { return $null }
    $c = Get-Command $T.Exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $c -and $T.Name -eq '7z' -and (Test-Path 'C:\Program Files\7-Zip\7z.exe')) { return 'C:\Program Files\7-Zip' }
    if (-not $c -and $T.Name -eq 'python') { $c = Get-Command py.exe -ErrorAction SilentlyContinue | Select-Object -First 1 }
    if (-not $c) { return $null }
    try {
        if ($T.Name -eq 'pwsh') { $v = & $c.Source -NoProfile -Command '$PSVersionTable.PSVersion.Major'; if ([int]"$v" -lt 7) { return $null } }
        if ($T.Name -eq 'cmake') { $v = "$(& $c.Source --version)"; if ($v -notmatch 'version (\d+)\.(\d+)' -or ([int]$Matches[1] * 100 + [int]$Matches[2]) -lt 328) { return $null } }
        if ($T.Name -eq 'python') { $v = "$(& $c.Source -c 'import sys; print(sys.version_info[0])' 2>$null)"; if ($v.Trim() -ne '3') { return $null } }
    } catch { return $null }
    return Split-Path $c.Source
}

function Get-Download([string]$Url, [string]$Sha, [int]$Mb = 0) {
    New-Item -ItemType Directory -Force $downloads | Out-Null
    $file = Join-Path $downloads ([IO.Path]::GetFileName($Url))
    if ((Test-Path -LiteralPath $file) -and (Get-Sha256 $file) -eq $Sha) { return $file }
    Say "   downloading $Url"
    $name = [IO.Path]::GetFileName($Url)
    $watch = { param($o, $secs)
               $got = 0; if (Test-Path -LiteralPath $file) { $got = [math]::Round((Get-Item -LiteralPath $file).Length / 1MB) }
               if ($Mb -gt 0) { "Downloading ${name}: $got / about $Mb MB"; [int](100 * $got / $Mb) } else { "Downloading ${name}: $got MB"; -1 } }.GetNewClosure()
    Run-Watched (Join-Path $env:SystemRoot 'System32\curl.exe') @('-fL', '--retry', '3', '-sS', '-o', $file, $Url) "the download of $Url" 'Downloading' $watch | Out-Null
    $got = (Get-Sha256 $file)
    if ($got -ne $Sha) { Remove-Item -LiteralPath $file; throw "$Url is not the file the setup expects (SHA-256 $got, not $Sha): try again later" }
    return $file
}

# Chronicle's toolchain as its build.bat keeps it (<Chronicle>\.kazarov\windows\deps), less Rust: what the mod's build needs.
$kToolchain = @('deps\llvm\llvm-mingw-20260922-ucrt-x86_64\bin\clang++.exe', 'deps\ninja\ninja.exe', 'deps\glslang\bin\glslang.exe',
                'deps\sdl\SDL3-3.4.18\x86_64-w64-mingw32\lib\cmake\SDL3', 'deps\ffmpeg\kz-ffmpeg-9.0.2\kz-ffmpeg.json',
                'deps\vulkan\Vulkan-Headers-vulkan-sdk-1.4.363.0\include')
$needToolchain = @($kToolchain | Where-Object { -not (Test-Path -LiteralPath (Join-Path $deps $_)) }).Count -gt 0
$have = @{}
foreach ($t in $kTools) { $have[$t.Name] = Find-OwnTool $t }
$need = @($kTools | Where-Object { -not $have[$_.Name] -and -not (Test-Path -LiteralPath (Join-Path $tools "$($_.Name)\$($_.Bin)\$($_.Exe)")) })
$drive = Get-PSDrive -Name $chronicle.Substring(0, 1) -ErrorAction SilentlyContinue
$freeGb = if ($drive) { [math]::Round($drive.Free / 1GB, 1) } else { 0 }
$needGb = 1.5 + $(if ($needToolchain) { 1 } else { 0 }) + $(if ($needData) { 5.5 } else { 0 })
Say ''
if ($need -or $cloneChronicle -or $needToolchain) {
    Say 'The setup will download (nothing is installed on Windows):'
    foreach ($t in $need) { Say "   $($t.Name) (portable, about $($t.Mb) MB), into $tools" }
    if ($cloneChronicle) { Say "   Chronicle (the decompilation the mod builds on) at commit $($pin.Substring(0, 8)), about 10 MB, into $chronicle" }
    if ($needToolchain) { Say "   Chronicle's pinned compiler toolchain (llvm-mingw, Ninja, SDL3, FFmpeg, glslang, Vulkan headers; no Rust), about 210 MB, into $deps" }
} else { Say 'Nothing to download: this PC has the tools, and your Chronicle has its toolchain.' }
Say "The build needs about $needGb GB of free disk while it runs$(if ($needData) { ', with the game data from your disc (5.4 GB)' }); afterwards the mod itself is about 25 MB. $freeGb GB is free."
if ($drive -and $drive.Free -lt $needGb * 1GB) { Stop-Setup "Not enough free disk space on $($chronicle.Substring(0, 2)) ($freeGb GB; about $needGb GB needed)." }
if (-not (Ask 'Go ahead?' $true)) { Stop-Setup 'Nothing was downloaded (you said no).' }

Step '3/8 Tools' {
    foreach ($t in $kTools) {
        $dir = Join-Path $tools $t.Name
        if ($have[$t.Name]) { Say "   $($t.Name): this PC's ($($have[$t.Name]))"; continue }
        $bin = Join-Path $dir $t.Bin
        if (Test-Path -LiteralPath (Join-Path $bin $t.Exe)) { $have[$t.Name] = $bin; Say "   $($t.Name): portable, already here"; continue }
        $file = Get-Download $t.Url $t.Sha $t.Mb
        New-Item -ItemType Directory -Force $dir | Out-Null
        if ($t.Name -eq '7z') {
            $zr = Get-Download $k7zr.Url $k7zr.Sha
            Run $zr @('x', '-y', "-o$dir", $file, 'x64\7za.exe', 'x64\7za.dll') 'the 7-Zip extraction' | Out-Null
            Copy-Item -LiteralPath (Join-Path $dir 'x64\7za.exe') -Destination (Join-Path $dir '7z.exe')
            Copy-Item -LiteralPath (Join-Path $dir 'x64\7za.dll') -Destination (Join-Path $dir '7za.dll')
        } else {
            Run (Join-Path $env:SystemRoot 'System32\tar.exe') @('-xf', $file, '-C', $dir) "the extraction of $($t.Name)" | Out-Null
        }
        if ($t.Name -eq 'python') {
            # The embeddable Python's ._pth file shuts out PYTHONPATH, which Kazarov's build scripts use: without it
            # Python finds its standard library (python312.zip) beside itself and honours PYTHONPATH.
            Get-ChildItem -LiteralPath $dir -Filter 'python*._pth' | Remove-Item
            Copy-Item -LiteralPath (Join-Path $dir 'python.exe') -Destination (Join-Path $dir 'python3.exe')
        }
        if (-not (Test-Path -LiteralPath (Join-Path $bin $t.Exe))) { throw "no $($t.Exe) in $bin after extracting $file" }
        $have[$t.Name] = $bin
        Say "   $($t.Name): portable, $bin"
    }
    # The tools first on PATH for everything the setup runs (Chronicle's scripts and the build find them there).
    $env:PATH = (($kTools | ForEach-Object { $have[$_.Name] }) -join ';') + ";$env:PATH"
}
$git = Join-Path $have.git 'git.exe'
$pwsh = Join-Path $have.pwsh 'pwsh.exe'

# 4. Chronicle at the pin (only when there was none), its toolchain, and the mod's build in a temporary folder.
if ($cloneChronicle) {
    Step '4/8 Chronicle' {
        if (-not (Test-Path -LiteralPath (Join-Path $chronicle '.git'))) {
            New-Item -ItemType Directory -Force $chronicle | Out-Null
            Run $git @('-C', $chronicle, 'init', '-q') 'git init' | Out-Null
            Run $git @('-C', $chronicle, 'config', 'core.autocrlf', 'false') 'git config' | Out-Null
            Run $git @('-C', $chronicle, 'remote', 'add', 'origin', $release['chronicle-url']) 'git remote' | Out-Null
        }
        if (-not (Test-Commit $chronicle $pin)) {
            Say "   fetching Chronicle $($pin.Substring(0, 8)) from $($release['chronicle-url'])"
            Run $git @('-C', $chronicle, 'fetch', '-q', '--depth', '1', 'origin', $pin) 'fetching Chronicle (is the internet reachable?)' | Out-Null
        }
        Run $git @('-C', $chronicle, '-c', 'advice.detachedHead=false', 'checkout', '-q', '--force', $pin) 'checking Chronicle out' | Out-Null
        # (this setup's own Chronicle: its git status stays clean with the mod inside)
        $exclude = Join-Path $chronicle '.git\info\exclude'
        if (-not (Select-String -LiteralPath $exclude -Pattern '^/mods/$' -Quiet -ErrorAction SilentlyContinue)) { Add-Content -LiteralPath $exclude -Value '/mods/' }
    }
}

# Chronicle's acquire.ps1 ends with acquire-rust.ps1, the Rust toolchain that only dcdata needs: the setup runs it without
# that step, as acquire-dco.ps1 in the temporary folder (beside a copy of its common.ps1), and only when acquire.ps1 is the
# pinned file it knows by SHA-256, so a changed acquire.ps1 stops here instead of downloading what the setup never checked.
$kAcquireSha = '354A7F56063ECEB77E6D453F24956E824A59A71D854BAE3EC13FCCA543FB3300'
$kAcquireRust = "& `"`$PSScriptRoot/acquire-rust.ps1`" -Root `$root`nif (`$LASTEXITCODE) { exit `$LASTEXITCODE }`n"
$peak = 0
# release6 (Jep 11-10: a tester's Windows Defender quarantined darkcloud.exe as it was linked, and the setup sat silent):
# every exe and DLL of a game\ folder there, readable and not empty, else the setup stops saying what to do. It never adds
# an exclusion itself (that needs admin and changes Windows). Test switch DCO_SETUP_TEST_REMOVE_EXE=1: game\darkcloud.exe
# deleted after the build, as an antivirus would (row l-setup-defender); DCO_SETUP_MUTATE=no_game_check (its twin): no check.
$avFolder = Join-Path $chronicle 'mods'
function Get-MissingGameFile([string]$Game) {
    if ($env:DCO_SETUP_MUTATE -eq 'no_game_check') { return '' }
    $exe = Join-Path $Game 'darkcloud.exe'
    $files = @($exe) + @(Get-ChildItem -LiteralPath $Game -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object FullName)
    foreach ($f in ($files | Select-Object -Unique)) {
        try {
            $fs = [IO.File]::Open($f, 'Open', 'Read', 'ReadWrite')
            try { if ($fs.Length -le 0) { return "$f is empty" }; $null = $fs.ReadByte() } finally { $fs.Close() }
        } catch { return "$f is missing or cannot be read ($($_.Exception.InnerException.Message))" }
    }
    return ''
}
function Stop-Antivirus([string]$Why) {
    Stop-Setup ("Windows Defender (or your antivirus) removed the game while it was being built ($Why). This is a false alarm on a freshly built, unsigned program.") (
        "   Add this folder as an exclusion: Windows Security > Virus & threat protection > Manage settings > Exclusions > Add an exclusion > Folder > $avFolder`n" +
        "   Then run the setup again; it carries on from here.")
}
Say ''
Say 'Some antivirus programs flag freshly built games; if yours does, the setup will tell you what to do.'
Step '5/8 The build' {
    New-Item -ItemType Directory -Force $work | Out-Null
    if ($needToolchain) {
        Say "   Chronicle's toolchain into $deps"
        $acquire = Join-Path $chronicle 'tools\windows\acquire.ps1'
        $got = Get-Sha256 $acquire
        if ($got -ne $kAcquireSha) { throw "Chronicle's tools\windows\acquire.ps1 is not the one this setup knows (SHA-256 $got)" }
        $text = [IO.File]::ReadAllText($acquire)
        if (-not $text.EndsWith($kAcquireRust)) { throw "Chronicle's acquire.ps1 does not end with its Rust step" }
        $scripts = Join-Path $work 'acquire'
        New-Item -ItemType Directory -Force $scripts | Out-Null
        Copy-Item -LiteralPath (Join-Path $chronicle 'tools\windows\common.ps1') -Destination $scripts -Force
        $noRust = Join-Path $scripts 'acquire-dco.ps1'
        [IO.File]::WriteAllText($noRust, $text.Substring(0, $text.Length - $kAcquireRust.Length), (New-Object Text.UTF8Encoding $false))
        $before = Get-FolderBytes $deps; $script:depsTick = $null; $script:depsMb = 0
        $watch = { param($o, $secs)
                   # (the folder walked every 3 s at most: thousands of files once the compiler is unpacked)
                   if (-not $script:depsTick -or ((Get-Date) - $script:depsTick).TotalSeconds -ge 3) { $script:depsMb = [math]::Round(((Get-FolderBytes $deps) - $before) / 1MB); $script:depsTick = Get-Date }
                   $last = Get-LastLine $o
                   "Chronicle's toolchain: $($script:depsMb) MB so far$(if ($last) { " ($($last.Substring(0, [Math]::Min(60, $last.Length))))" })"; -1 }
        Run-Watched $pwsh @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $noRust, '-Root', $deps) "Chronicle's toolchain download" "Chronicle's toolchain (about 210 MB)" $watch | Out-Null
    }
    # The mod's source at the release's commit, in the temporary folder (<Chronicle>\mods\Dark Cloud Co-op.build).
    $from = if ($env:DCO_SETUP_MOD_REPO) { $env:DCO_SETUP_MOD_REPO } elseif (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'mod.bundle')) { Join-Path $PSScriptRoot 'mod.bundle' } else { $release['mod-url'] }
    if (-not $from) { throw 'the release has no setup\mod.bundle and no mod-url' }
    if (-not (Test-Path -LiteralPath (Join-Path $work '.git'))) {
        Run $git @('-C', $work, 'init', '-q') 'git init' | Out-Null
        Run $git @('-C', $work, 'config', 'core.autocrlf', 'false') 'git config' | Out-Null
    }
    if (-not (Test-Commit $work $release['mod-commit'])) {
        Say "   the mod's source, commit $($release['mod-commit'].Substring(0, 8)), from $from"
        $what = if ($from -like '*.bundle') { 'refs/heads/release' } else { $release['mod-commit'] }   # (the bundle's one branch)
        Run $git @('-C', $work, 'fetch', '-q', $from, $what) "fetching the mod's source" | Out-Null
    }
    Run $git @('-C', $work, '-c', 'advice.detachedHead=false', 'checkout', '-q', '--force', $release['mod-commit']) 'checking the mod out' | Out-Null
    $modPin = (Get-Content -LiteralPath (Join-Path $work 'chronicle-pin.txt') | Where-Object { $_ -match '^upstream\s' }) -replace '^upstream\s+', ''
    if ($modPin.Trim() -ne $pin) { throw "the mod is pinned to Chronicle $modPin, the release says $pin" }
    # build.ps1 -NoDcdata keeps the game\dcdata.exe it finds there: the release's own.
    New-Item -ItemType Directory -Force (Join-Path $work 'game') | Out-Null
    Copy-Item -LiteralPath $dcdata -Destination (Join-Path $work 'game\dcdata.exe') -Force
    $jobs = if ($env:DCO_SETUP_JOBS) { [int]$env:DCO_SETUP_JOBS } else { [Math]::Max(2, [Environment]::ProcessorCount) }
    $buildLog = Join-Path $work 'out\build\build.log'
    $script:lastStep = ''; $script:lastStepAt = Get-Date
    $watch = { param($o, $secs)
               # (ninja writes "[N/M] ..." per step into out\build\build.log; before it come the stage and CMake's configure)
               $n = if (Test-Path -LiteralPath $buildLog) { Get-LastLine $buildLog '^\[\d+/\d+\]' } else { '' }
               if ($n -ne $script:lastStep) { $script:lastStep = $n; $script:lastStepAt = Get-Date }
               # release6: no new ninja step for 3 minutes is said, never a silent wait (the link of a 200 MB exe while an
               # antivirus scans it is the usual one)
               $stuck = ((Get-Date) - $script:lastStepAt).TotalMinutes
               $wait = if ($stuck -ge 3 -and $n -match 'Linking') { " - still linking after $([int]$stuck) min: your antivirus may be scanning it" }
                       elseif ($stuck -ge 3) { " - no new step for $([int]$stuck) min, still working" } else { '' }
               if ($n -match '^\[(\d+)/(\d+)\]') { $a = [int]$Matches[1]; $b = [int]$Matches[2]; "Building the game: $a / $b ($([int](100 * $a / $b))%)$wait"; [int](100 * $a / $b) }
               else { $l = Get-LastLine $o; "Preparing the build$(if ($l) { ": $($l.Substring(0, [Math]::Min(60, $l.Length)))" })$wait"; -1 } }
    try {
        $lines = Run-Watched $pwsh @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $work 'build\build.ps1'), '-Root', $deps, '-Jobs', "$jobs", '-NoDcdata') "the build (its log: $work\out\build\build.log)" 'Building Dark Cloud Co-op' $watch
    } catch {
        if (Test-Path -LiteralPath $buildLog) { Add-Content -LiteralPath $Log -Value (Get-Content -LiteralPath $buildLog) }
        # (a link that cannot write or find its output, or a strip or copy denied: the antivirus took the exe)
        $said = "$($_.Exception.Message)`n$(if (Test-Path -LiteralPath $buildLog) { Get-Content -LiteralPath $buildLog -Tail 30 | Out-String })"
        if ($said -match '(?i)access is denied|permission denied|operation did not complete successfully because the file contains a virus|cannot open output file|darkcloud\.exe.*(no such file|cannot find)|could not find .*darkcloud\.exe') {
            Stop-Antivirus 'the build could not write or find darkcloud.exe'
        }
        throw
    }
    if (Test-Path -LiteralPath $buildLog) { Add-Content -LiteralPath $Log -Value (Get-Content -LiteralPath $buildLog) }   # (ninja's whole output, in the log)
    if ($env:DCO_SETUP_TEST_REMOVE_EXE -eq '1') { [IO.File]::Delete((Join-Path $work 'game\darkcloud.exe')); Add-Content -LiteralPath $Log -Value 'test: game\darkcloud.exe deleted (DCO_SETUP_TEST_REMOVE_EXE)' }
    $missing = Get-MissingGameFile (Join-Path $work 'game')
    if ($missing) { Stop-Antivirus $missing }
    $lines | Where-Object { $_ -match '^(version:|tree:|built )' } | ForEach-Object { Say "   $_" }
    # release4: the build's content version (what the link compares; a README-only commit keeps it), for VERSION.txt
    $script:buildVersion = "$(@($lines | Where-Object { $_ -match '^version: ' }) | Select-Object -First 1)" -replace '^version: ', ''
    $script:peak = Get-FolderBytes $work
}

# 5. The game data, only when Chronicle has none: extracted beside data\ first, so a stopped extraction leaves no half data\.
if ($needData) {
    Step '6/8 The game data from your disc' {
        if (Test-Path -LiteralPath $dataPart) { Remove-OwnFolder $dataPart }
        New-Item -ItemType Directory -Force (Split-Path $dataPart) | Out-Null
        $script:dataTick = $null; $script:dataMb = 0
        $watch = { param($o, $secs)
                   # (the NTSC disc extracts to about 5.4 GB: the folder's size is the bar; walked every 3 s at most)
                   if (-not $script:dataTick -or ((Get-Date) - $script:dataTick).TotalSeconds -ge 3) { $script:dataMb = [math]::Round((Get-FolderBytes $dataPart) / 1MB); $script:dataTick = Get-Date }
                   "Extracting the game data: $($script:dataMb) / about 5400 MB"; [int](100 * $script:dataMb / 5400) }
        Run-Watched (Join-Path $work 'game\dcdata.exe') @('extract', $iso, $dataPart) 'the extraction (is the disc image whole?)' 'The game data from your disc' $watch | Out-Null
        $why = Get-DataMismatch $dataPart
        if ($why) { Remove-OwnFolder $dataPart; throw "the extracted data is not what this release expects: $why" }
        Rename-Item -LiteralPath $dataPart -NewName 'data'
        Say "   $data"
    }
}

# 6. The mod folder: what a player needs, never the build or the source.
Step '7/8 The mod folder' {
    New-Item -ItemType Directory -Force $mod, (Join-Path $mod 'launcher') | Out-Null
    $game = Join-Path $mod 'game'
    $steamDll = Join-Path $game 'steam_api64.dll'
    $keep = if (Test-Path -LiteralPath $steamDll) { [IO.File]::ReadAllBytes($steamDll) } else { $null }
    Remove-OwnFolder $game
    Copy-Item -Recurse -LiteralPath (Join-Path $work 'game') -Destination $game
    Remove-Item -LiteralPath (Join-Path $game 'dcdata.exe')   # (the data is extracted; the launcher never runs it)
    if ($keep) { [IO.File]::WriteAllBytes($steamDll, $keep) }
    foreach ($f in 'Play Dark Cloud Co-op.exe', 'README.md', 'Set up Steam.bat') { Copy-Item -LiteralPath (Join-Path $work $f) -Destination $mod -Force }
    Copy-Item -LiteralPath (Join-Path $work 'launcher\steam-setup.ps1') -Destination (Join-Path $mod 'launcher') -Force
    $missing = Get-MissingGameFile $game   # (release6: an antivirus may take it from the mod folder too)
    if ($missing) { Stop-Antivirus $missing }
    $sha = Get-Sha256 (Join-Path $game 'darkcloud.exe')
    $script:exeSha = $sha
    $pinLines = Get-Content -LiteralPath (Join-Path $work 'chronicle-pin.txt') | Where-Object { $_ -match '^(upstream|kazarov)\s' }
    Set-Content -LiteralPath (Join-Path $mod 'VERSION.txt') -Value @(
        "Dark Cloud Co-op $($release.version)", "mod $($release['mod-commit'])", "build version $script:buildVersion", $pinLines, "game\darkcloud.exe SHA-256 $sha",
        "game data: $data ($($release['data-release']))",
        'Both players need the same release (the link checks it), on Chronicle at the same commit, with their own NTSC (US 1.02) disc.')
}

# 7. The temporary build folder, deleted.
Step '8/8 Cleaning up' {
    Remove-OwnFolder $work
    Say "   deleted $work (the build's temporary files, $(Format-Mb $peak))"
}

$total = [int]((Get-Date) - $t0).TotalSeconds
Say ''
Say "Done in $([math]::Floor($total / 60)) min $($total % 60) s ($($script:steps -join ', '))."
Say "The mod: $mod, $(Format-Mb (Get-FolderBytes $mod)). Temporary build files at most $(Format-Mb $peak), now deleted."
Say "game\darkcloud.exe SHA-256 $exeSha"
if ($release['exe-sha256']) {
    if ($release['exe-sha256'] -eq $exeSha) { Say 'That is the release''s own exe, byte for byte: you can link with anyone on this release.' }
    else { Say "NOTE: the release's exe is $($release['exe-sha256']); this build differs (please report it with setup-log.txt)." }
}
Say ''
Say "Play: $mod\Play Dark Cloud Co-op.exe"
Say 'For a friend: they run this same setup on their own Chronicle, with their own disc.'
if (Ask 'Set up Steam joins now (optional; it asks before each change)?' $false) { & (Join-Path $mod 'Set up Steam.bat') }
exit 0
