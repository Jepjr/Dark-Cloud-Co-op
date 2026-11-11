# coop-buildid (Jep, 2026-11-03: the link only has to prove both copies were made from the same version of the mod and of
# the decompile, never where or when) and release4 (Jep 11-10: files that do not compose the build must not change the
# exe): the version a build is made from, written into its staged tree as port\src\coop\build_version.hpp, which
# src\coop\netplay_hello.cpp reads. It holds the CONTENT version of the mod (Get-ContentVersion: a SHA-256 over exactly
# the files the build reads) and chronicle-pin.txt's upstream and Kazarov commits. No commit, no paths, no times: a commit
# that touches only the README, docs\, release\, launcher\ or debug\ builds the same exe, byte for byte.
#
# The files that compose darkcloud.exe, from what build\build.ps1 reads:
#   src\           src\coop -> port\src\coop and src\added -> the tree (step 2); src\helper builds dcsteam.exe beside it
#   recipes\       the edits to Chronicle's files (step 3)
#   build\         build.ps1, Recipe.psm1, this file, reproducible.cmake, snapregions.py + snapshot-allow.txt (the state
#                  set the game checks), pinplan.py (the arena pin plan)
#   chronicle-pin.txt  the Chronicle and Kazarov commits (the tree, the toolchain names are in build.ps1)
# Not in it: README.md, LICENSE.md, NOTICE.md, docs\, release\, debug\, launcher\ (build.ps1 builds the launcher from it,
# a separate exe: darkcloud.exe never reads it), game\dcdata.exe (kept as shipped with -NoDcdata), Set up Steam.bat.
# src\coop\build_version.hpp itself is left out (the build writes it), as are git-ignored files (__pycache__).
$script:ContentPaths = @('src', 'recipes', 'build', 'chronicle-pin.txt')
$script:ContentSkip = @('src/coop/build_version.hpp')

# The git blob id of some bytes (SHA-1 of "blob <size>\0" + bytes): the same id git gives the file, without git.
function Get-BlobId([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA1]::Create()
    try {
        $head = [Text.Encoding]::ASCII.GetBytes("blob $($Bytes.Length)`0")
        $all = New-Object byte[] ($head.Length + $Bytes.Length)
        [Array]::Copy($head, $all, $head.Length); [Array]::Copy($Bytes, 0, $all, $head.Length, $Bytes.Length)
        -join ($sha.ComputeHash($all) | ForEach-Object { $_.ToString('x2') })
    } finally { $sha.Dispose() }
}

# The content version: SHA-256 (64 lowercase hex) of "dco-content 1\n" and one "<blob id> <path>\n" line per file, paths
# relative to the mod folder with forward slashes, sorted ordinally. -Commit: the files of that commit (git ls-tree) instead
# of the working tree (git ls-files: tracked and untracked, git-ignored left out; without git, every file on disk).
# debug\qa\py\conftest.py's content_version() is the same function in Python.
# DCO_BUILDVERSION_MUTATE=all_files (build-version-selftest~twin): every file of the mod, as a commit id covered it.
function Get-ContentVersion([string]$Mod, [string]$Commit = '') {
    $Mod = [IO.Path]::GetFullPath($Mod)
    $script:ContentPaths = if ($env:DCO_BUILDVERSION_MUTATE -eq 'all_files') { @('.') } else { @('src', 'recipes', 'build', 'chronicle-pin.txt') }
    $entries = [System.Collections.Generic.List[string]]::new()
    if ($Commit) {
        $out = & git.exe -C $Mod ls-tree -r -z $Commit -- @($script:ContentPaths) 2>$null
        if ($LASTEXITCODE) { throw "git ls-tree $Commit failed in $Mod" }
        foreach ($rec in ("$out" -split "`0")) {
            if ($rec -notmatch '^\d+ blob ([0-9a-f]{40})\t(.+)$') { continue }
            if ($script:ContentSkip -contains $Matches[2]) { continue }
            $entries.Add("$($Matches[1]) $($Matches[2])")
        }
    } else {
        $out = & git.exe -C $Mod ls-files -z -co --exclude-standard -- @($script:ContentPaths) 2>$null
        $paths = if ($LASTEXITCODE) {
            # (no git checkout: every file on disk under the paths)
            foreach ($p in $script:ContentPaths) {
                $full = Join-Path $Mod $p
                if (Test-Path -LiteralPath $full -PathType Leaf) { $p }
                elseif (Test-Path -LiteralPath $full) {
                    Get-ChildItem -LiteralPath $full -Recurse -File -Force | Where-Object { $_.FullName -notmatch '\\__pycache__\\' } |
                        ForEach-Object { $_.FullName.Substring($Mod.Length).TrimStart('\') -replace '\\', '/' }
                }
            }
        } else { @("$out" -split "`0" | Where-Object { $_ }) }
        foreach ($p in (@($paths) | Select-Object -Unique)) {
            if ($script:ContentSkip -contains $p) { continue }
            $file = Join-Path $Mod ($p -replace '/', '\')
            if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }   # (tracked, deleted on disk: the build never sees it)
            $entries.Add("$(Get-BlobId ([IO.File]::ReadAllBytes($file))) $p")
        }
    }
    # (by path, ordinally: the 40-hex id and a space come first in each line)
    $entries.Sort([Comparison[string]] { param($a, $b) [string]::CompareOrdinal($a.Substring(41), $b.Substring(41)) })
    $canon = "dco-content 1`n" + (($entries | ForEach-Object { "$_`n" }) -join '')
    $sha = [Security.Cryptography.SHA256]::Create()
    try { -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($canon)) | ForEach-Object { $_.ToString('x2') }) } finally { $sha.Dispose() }
}

function Get-BuildVersion([string]$Mod, [hashtable]$Pin) {
    return [pscustomobject]@{ Content = (Get-ContentVersion $Mod); Chronicle = "$($Pin.upstream)"; Kazarov = "$($Pin.kazarov)" }
}

# -Flavor: a build other than build\build.ps1's (debug\build\build.ps1's dev build: 'dev' and its dev-include.cmake's hash),
# in the link's id alone, so a dev exe never links a release exe of the same content.
function Write-BuildVersion([string]$Mod, [hashtable]$Pin, [string]$Stage, [string]$Flavor = '') {
    $v = Get-BuildVersion $Mod $Pin
    $text = "// Written by build\BuildVersion.ps1 for each build (never edited, never committed): the version this exe is made from.`n" +
            "#pragma once`n" +
            "#define DCO_VERSION_CONTENT `"$($v.Content)`"`n" +
            "#define DCO_VERSION_CHRONICLE `"$($v.Chronicle)`"`n" +
            "#define DCO_VERSION_KAZAROV `"$($v.Kazarov)`"`n" +
            "#define DCO_VERSION_FLAVOR `"$Flavor`"`n"
    $dest = Join-Path $Stage 'port\src\coop\build_version.hpp'
    New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
    [IO.File]::WriteAllText($dest, $text, [Text.UTF8Encoding]::new($false))
    Write-Host "version: content $($v.Content.Substring(0, 16)) + Chronicle $($v.Chronicle.Substring(0, 8)) / Kazarov $($v.Kazarov.Substring(0, 8))$(if ($Flavor) { " ($Flavor)" })"
}
