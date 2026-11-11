# Recipes: the mod's edits to Chronicle's files without Chronicle's own text in them.
#
# A recipe is a -U0 diff whose removed lines are replaced by a hash of the line (SHA-256 of its bytes, first
# 16 hex digits) and which has no context lines. Only the mod's own lines appear in full. build.ps1 applies it
# to the user's own copy of Chronicle at the pinned commit: the base file must have the `from` blob hash,
# every removed line must hash as recorded, and the result must have the `to` blob hash, so a recipe either
# rebuilds coop's file byte for byte or stops the build.
#
#   # comment
#   target port/src/gameloop.cpp     the file the recipe writes
#   base port/src/gameloop.cpp       the upstream file it starts from (another one for a derived file)
#   from <git blob sha1>             the base file's blob
#   to <git blob sha1>               the result's blob
#   @@ -a,b +c,d                     as in a diff: old lines a..a+b-1 go (b = 0: insert after line a)
#   - <hash>                         a removed line, by hash
#   +text                            an added line, as it is
#   =N <hash>                        an added line that is upstream's line N of the base file, by hash
#                                    (lines of 15 characters or more; shorter ones, braces and the like, are inline)
#   ~N <hash>|<lead>|<trail>         upstream's line N with its outer whitespace replaced by lead and trail
#                                    (spaces and tabs only; for a line the mod re-indents)
#   \                                the line before has no newline at its end
#
# Text is handled as Latin-1 throughout, so every byte goes through unchanged.

Set-StrictMode -Version 2.0
$script:Latin1 = [Text.Encoding]::GetEncoding(28591)

function Get-LineHash([string]$Line) {
    $sha = [Security.Cryptography.SHA256]::Create()
    ([BitConverter]::ToString($sha.ComputeHash($script:Latin1.GetBytes($Line))) -replace '-', '').Substring(0, 16).ToLower()
}

function Get-BlobSha([byte[]]$Bytes) {
    $head = $script:Latin1.GetBytes("blob $($Bytes.Length)`0")
    $sha = [Security.Cryptography.SHA1]::Create()
    $all = New-Object byte[] ($head.Length + $Bytes.Length)
    [Array]::Copy($head, $all, $head.Length); [Array]::Copy($Bytes, 0, $all, $head.Length, $Bytes.Length)
    ([BitConverter]::ToString($sha.ComputeHash($all)) -replace '-', '').ToLower()
}

# Lines with their terminators ("abc`n"; the last may have none).
function Split-Lines([string]$Text) {
    if ($Text.Length -eq 0) { return , @() }
    return , @([regex]::Split($Text, '(?<=\n)') | Where-Object { $_ -ne '' })
}

# From a -U0 diff (Latin-1 text) of base -> result, the recipe text.
# -Index (the public form, sync-from-coop.ps1 -Public): upstream's lines across all its files, trimmed text ->
# @{ Path; N; Text }; an added line found there (25 characters or more, not an #include) is named by file and number.
function New-Recipe([string]$Diff, [string]$Target, [string]$Base, [byte[]]$BaseBytes, [byte[]]$ResultBytes, [hashtable]$Index = $null) {
    $out = [Text.StringBuilder]::new()
    [void]$out.Append("# $Target from Chronicle's $Base (build\sync-from-coop.ps1). Chronicle's lines appear only as hashes.`n")
    [void]$out.Append("target $Target`nbase $Base`nfrom $(Get-BlobSha $BaseBytes)`nto $(Get-BlobSha $ResultBytes)`n")
    $inHunks = $false
    $stats = @{ hunks = 0; added = 0; removed = 0; copied = 0 }
    # Upstream's own lines, by text: an added line that is one of them is named by its number, not written out.
    # Ordinal (case-sensitive) tables: @{} ignores case, and a line differing only in case would be copied wrong.
    $where = [Collections.Hashtable]::new([StringComparer]::Ordinal)
    $trimmed = [Collections.Hashtable]::new([StringComparer]::Ordinal)
    $baseLines = Split-Lines ($script:Latin1.GetString($BaseBytes))
    $n = 0
    foreach ($l in (Split-Lines ($script:Latin1.GetString($BaseBytes)))) {
        $n++
        $t = $l.TrimEnd("`n")
        if ($t.Trim().Length -ge 15 -and -not $where.ContainsKey($t)) { $where[$t] = $n }
        if ($t.Trim().Length -ge 15 -and -not $trimmed.ContainsKey($t.Trim())) { $trimmed[$t.Trim()] = $n }
    }
    foreach ($raw in ($Diff -split "`n")) {
        if ($raw -match '^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@') {
            $inHunks = $true; $stats.hunks++
            $b = if ($Matches[2]) { $Matches[2] } else { '1' }; $d = if ($Matches[4]) { $Matches[4] } else { '1' }
            [void]$out.Append("@@ -$($Matches[1]),$b +$($Matches[3]),$d`n")
            continue
        }
        if (-not $inHunks) { continue }
        if ($raw.StartsWith('-')) { [void]$out.Append("- $(Get-LineHash $raw.Substring(1))`n"); $stats.removed++ }
        elseif ($raw.StartsWith('+') -and $where.ContainsKey($raw.Substring(1))) {
            $t = $raw.Substring(1); [void]$out.Append("=$($where[$t]) $(Get-LineHash $t)`n"); $stats.copied++
        }
        elseif ($raw.StartsWith('+') -and $raw.Substring(1).Trim().Length -ge 15 -and $trimmed.ContainsKey($raw.Substring(1).Trim()) -and
                $raw.Substring(1) -match '^([ \t]*)(.*?)([ \t]*)$' -and $Matches[2] -eq $raw.Substring(1).Trim()) {
            $lead = $Matches[1]; $trail = $Matches[3]; $k = $trimmed[$raw.Substring(1).Trim()]
            $bt = $baseLines[$k - 1].TrimEnd("`n")
            [void]$out.Append("~$k $(Get-LineHash $bt)|$($lead -replace "`t", '\t')|$($trail -replace "`t", '\t')`n"); $stats.copied++
        }
        elseif ($raw.StartsWith('+') -and $null -ne $Index -and $raw.Substring(1).Trim().Length -ge 25 -and -not $raw.Substring(1).Trim().StartsWith('#include') -and
                $Index.ContainsKey($raw.Substring(1).Trim()) -and $raw.Substring(1) -match '^([ \t]*)(.*?)([ \t]*)$' -and $Matches[2] -eq $raw.Substring(1).Trim()) {
            $lead = $Matches[1]; $trail = $Matches[3]; $hit = $Index[$raw.Substring(1).Trim()]
            [void]$out.Append("@$($hit.Path):$($hit.N) $(Get-LineHash $hit.Text)|$($lead -replace "`t", '\t')|$($trail -replace "`t", '\t')`n"); $stats.copied++
        }
        elseif ($raw.StartsWith('+')) { [void]$out.Append("$raw`n"); $stats.added++ }
        elseif ($raw.StartsWith('\')) { [void]$out.Append("\`n") }
        elseif ($raw -eq '') { continue }
        else { throw "unexpected diff line in $Target`: $raw" }
    }
    return [pscustomobject]@{ Text = $out.ToString(); Hunks = $stats.hunks; Added = $stats.added; Removed = $stats.removed; Copied = $stats.copied }
}

# Applies a recipe to the base file's bytes; returns the result's bytes, or throws.
# -Load: a scriptblock giving an upstream file's bytes by repo path, for "@path:N" lines.
function Invoke-Recipe([string]$RecipeText, [byte[]]$BaseBytes, [scriptblock]$Load = $null) {
    $meta = @{}
    $hunks = [System.Collections.Generic.List[object]]::new()
    $cur = $null
    $orig = Split-Lines ($script:Latin1.GetString($BaseBytes))   # (no @(): it would wrap the array in another)
    $other = @{}
    foreach ($line in ($RecipeText -split "`n")) {
        if ($null -ne $cur -and $line -match '^@(.+):(\d+) ([0-9a-f]{16})\|([^|]*)\|([^|]*)$') {
            $path = $Matches[1]; $n = [int]$Matches[2]; $hash = $Matches[3]; $lead = $Matches[4] -replace '\\t', "`t"; $trail = $Matches[5] -replace '\\t', "`t"
            if (-not $other.ContainsKey($path)) {
                if ($null -eq $Load) { throw "$($meta['target']): the recipe copies from $path, but no -Load" }
                $other[$path] = Split-Lines ($script:Latin1.GetString((& $Load $path)))
            }
            $src = $other[$path]
            $t = if ($n -ge 1 -and $n -le $src.Count) { $src[$n - 1].TrimEnd("`n") } else { $null }
            if ($null -eq $t -or (Get-LineHash $t) -ne $hash) { throw "$($meta['target']): line $n of $path is not the one the recipe copies" }
            $cur.Added.Add($lead + $t.Trim() + $trail + "`n"); $cur.LastAdded = $true
            continue
        }
        if ($null -ne $cur -and $line -match '^=(\d+) ([0-9a-f]{16})$') {
            $n = [int]$Matches[1]
            $t = if ($n -ge 1 -and $n -le $orig.Count) { $orig[$n - 1].TrimEnd("`n") } else { $null }
            if ($null -eq $t -or (Get-LineHash $t) -ne $Matches[2]) { throw "$($meta['target']): line $n of $($meta['base']) is not the one the recipe copies" }
            $cur.Added.Add($t + "`n"); $cur.LastAdded = $true
            continue
        }
        if ($null -ne $cur -and $line -match '^~(\d+) ([0-9a-f]{16})\|([^|]*)\|([^|]*)$') {
            $n = [int]$Matches[1]; $lead = $Matches[3] -replace '\\t', "`t"; $trail = $Matches[4] -replace '\\t', "`t"
            $t = if ($n -ge 1 -and $n -le $orig.Count) { $orig[$n - 1].TrimEnd("`n") } else { $null }
            if ($null -eq $t -or (Get-LineHash $t) -ne $Matches[2]) { throw "$($meta['target']): line $n of $($meta['base']) is not the one the recipe copies" }
            $cur.Added.Add($lead + $t.Trim() + $trail + "`n"); $cur.LastAdded = $true
            continue
        }
        if ($null -eq $cur -and $line -match '^(target|base|from|to) (.+)$') { $meta[$Matches[1]] = $Matches[2]; continue }
        if ($line -match '^@@ -(\d+),(\d+) \+(\d+),(\d+)$') {
            $cur = [pscustomobject]@{ A = [int]$Matches[1]; B = [int]$Matches[2]; Removed = [System.Collections.Generic.List[string]]::new(); Added = [System.Collections.Generic.List[string]]::new(); LastAdded = $false }
            $hunks.Add($cur); continue
        }
        if ($null -eq $cur) { continue }
        if ($line.StartsWith('- ')) { $cur.Removed.Add($line.Substring(2)); $cur.LastAdded = $false }
        elseif ($line.StartsWith('+')) { $cur.Added.Add($line.Substring(1) + "`n"); $cur.LastAdded = $true }
        elseif ($line -eq '\') { if ($cur.LastAdded) { $cur.Added[$cur.Added.Count - 1] = $cur.Added[$cur.Added.Count - 1].TrimEnd("`n") } }
    }
    $from = Get-BlobSha $BaseBytes
    if ($from -ne $meta['from']) { throw "$($meta['target']): $($meta['base']) is blob $from, the recipe needs $($meta['from'])" }
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($l in (Split-Lines ($script:Latin1.GetString($BaseBytes)))) { $lines.Add($l) }
    for ($h = $hunks.Count - 1; $h -ge 0; $h--) {
        $k = $hunks[$h]
        $start = if ($k.B -gt 0) { $k.A - 1 } else { $k.A }
        for ($i = 0; $i -lt $k.B; $i++) {
            $old = $lines[$start + $i].TrimEnd("`n")
            if ((Get-LineHash $old) -ne $k.Removed[$i]) { throw "$($meta['target']): line $($start + $i + 1) of $($meta['base']) is not the one the recipe removes" }
        }
        if ($k.B -gt 0) { $lines.RemoveRange($start, $k.B) }
        $lines.InsertRange($start, [string[]]$k.Added.ToArray())
    }
    $bytes = $script:Latin1.GetBytes(($lines -join ''))
    $to = Get-BlobSha $bytes
    if ($to -ne $meta['to']) { throw "$($meta['target']): the result is blob $to, the recipe expects $($meta['to'])" }
    return , $bytes
}

function Get-RecipeMeta([string]$RecipeText) {
    $meta = @{}
    foreach ($line in ($RecipeText -split "`n")) { if ($line -match '^(target|base|from|to) (.+)$') { $meta[$Matches[1]] = $Matches[2] } elseif ($line.StartsWith('@@')) { break } }
    return $meta
}

Export-ModuleMember -Function New-Recipe, Invoke-Recipe, Get-RecipeMeta, Get-BlobSha, Get-LineHash
