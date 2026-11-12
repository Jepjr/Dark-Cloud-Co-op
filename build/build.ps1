# Builds Dark Cloud Co-op from this folder and the Chronicle checkout it sits in (<checkout>\mods\Dark Cloud Co-op):
#   1. out\build\stage: Chronicle at chronicle-pin.txt's upstream commit (git archive, so your working tree is never
#      read or written) and its tools\kazarov submodule at the pinned kazarov commit
#   2. src\coop -> port\src\coop, src\added\* -> the same path in the tree
#   3. recipes\*.recipe: the mod's edits to Chronicle's files (build\Recipe.psm1). Each checks the file it starts from,
#      every line it removes and the file it makes by hash, so a recipe either makes the mod's file exactly or stops
#   4. out\build\src takes the stage, only the files whose bytes differ (ninja then rebuilds only what changed)
#   5. CMake and ninja build out\build\bin\darkcloud.exe with Chronicle's pinned Windows toolchain, then
#      darkcloud.snapregions (the state set joins copy, from the link map)
#   6. game\ (exe, DLLs, dcdata, lang, glyphs, state set, Steam Input files) and Play Dark Cloud Co-op.exe beside this
#      folder's README (neither is in git: a Release carries them built, or this script builds them)
#
#   pwsh -File build.ps1 [-Root <folder with deps\>] [-Jobs 16] [-KeepSymbols] [-NoDcdata]
# -NoDcdata: no Rust toolchain needed (dcdata.exe, the disc extractor, is the only part that links Rust); game\dcdata.exe
#        is kept as shipped instead of rebuilt.
# -Root: the folder Chronicle's tools\windows\acquire.ps1 put deps\ in. Default: this folder's out\ (acquire.ps1 -Root
#        "mods\Dark Cloud Co-op\out"), so everything the mod makes stays inside this folder.
# game\darkcloud.exe is stripped of its debug info and symbols unless -KeepSymbols: it is the file other players run, and
# the debug info holds your folder names. Nothing outside this folder is written.
param(
    [string]$Root = '',
    [int]$Jobs = 16,
    [switch]$KeepSymbols,
    [switch]$NoDcdata,
    # nlohmann_json's source folder, so CMake need not download it (default: CMake fetches Chronicle's pinned release)
    [string]$JsonSource = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$t0 = Get-Date
$Mod = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Chronicle = [IO.Path]::GetFullPath((Join-Path $Mod '..\..'))
if (-not (Test-Path -LiteralPath (Join-Path $Chronicle '.git')) -or -not (Test-Path -LiteralPath (Join-Path $Chronicle 'port'))) {
    throw "$Chronicle is not a Chronicle checkout: put this folder at <checkout>\mods\Dark Cloud Co-op"
}
if (-not $Root) { $Root = Join-Path $Mod 'out' }
$Root = [IO.Path]::GetFullPath($Root)
$llvmName = 'llvm-mingw-20260922-ucrt-x86_64'
if (-not (Test-Path -LiteralPath (Join-Path $Root "deps\llvm\$llvmName\bin\clang++.exe"))) {
    throw "no toolchain in $Root\deps: run Chronicle's tools\windows\acquire.ps1 -Root `"$Root`" first, or pass -Root"
}
# Chronicle 9418a550 builds Kazarov's Rust parts (Rust 1.99, Corrosion, cxxbridge) and links FFmpeg (Kazarov's LGPL
# kz-ffmpeg build): Chronicle's acquire.ps1 puts both in deps\ (upstream daaca7a0).
$rustName = '1.99.0-gnullvm'; $ffmpegName = 'kz-ffmpeg-9.0.2'
$needs = if ($NoDcdata) { @("deps\ffmpeg\$ffmpegName\kz-ffmpeg.json") } else { @("deps\rust\$rustName\bin\rustc.exe", "deps\rust\$rustName\bin\cxxbridge.exe", 'deps\rust\corrosion\corrosion-0.6.1\CMakeLists.txt', "deps\ffmpeg\$ffmpegName\kz-ffmpeg.json") }
if ($NoDcdata -and -not (Test-Path -LiteralPath (Join-Path $Mod 'game\dcdata.exe'))) { throw 'no game\dcdata.exe to keep: build without -NoDcdata (needs Rust)' }
foreach ($need in $needs) {
    if (-not (Test-Path -LiteralPath (Join-Path $Root $need))) { throw "no $need in $Root`: run Chronicle's tools\windows\acquire.ps1 -Root `"$Root`" again (it adds Rust and FFmpeg)" }
}
$Out = Join-Path $Mod 'out\build'
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'

$pin = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $Mod 'chronicle-pin.txt')) {
    if ($line -match '^(upstream|kazarov|kazarov-url)\s+(\S+)') { $pin[$Matches[1]] = $Matches[2] }
}

# A plain folder tree only: refuse to clear anything holding a junction or link (never follow one).
function Clear-Tree([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $links = @(Get-ChildItem -LiteralPath $Path -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue)
    if ($links) { throw "refusing to clear $Path`: it holds links ($($links[0].FullName))" }
    Remove-Item -LiteralPath $Path -Recurse -Force
}

# 1. the pinned upstream tree
$stage = Join-Path $Out 'stage'
Clear-Tree $stage
New-Item -ItemType Directory -Force $stage | Out-Null
& git.exe -C $Chronicle cat-file -e "$($pin.upstream)^{commit}" 2>$null
if ($LASTEXITCODE) { throw "the checkout does not have Chronicle commit $($pin.upstream): git fetch origin, then build again" }
& git.exe -C $Chronicle archive -o (Join-Path $Out 'upstream.tar') $pin.upstream; if ($LASTEXITCODE) { throw "git archive $($pin.upstream) failed in $Chronicle" }
& $tar -xf (Join-Path $Out 'upstream.tar') -C $stage; if ($LASTEXITCODE) { throw 'tar failed' }
$kzStage = Join-Path $stage 'tools\kazarov'
New-Item -ItemType Directory -Force $kzStage | Out-Null
# Kazarov at the pinned commit: from the checkout's tools\kazarov when it has it, else fetched once from the pin's
# kazarov-url into out\kazarov.git (a newer Chronicle names another Kazarov repository, without the commit the mod needs).
$kzRepo = Join-Path $Chronicle 'tools\kazarov'
& git.exe -C $kzRepo cat-file -e "$($pin.kazarov)^{commit}" 2>$null
if ($LASTEXITCODE) {
    $kzRepo = Join-Path $Mod 'out\kazarov.git'
    if (-not (Test-Path -LiteralPath $kzRepo)) { & git.exe init -q --bare $kzRepo }
    & git.exe -C $kzRepo cat-file -e "$($pin.kazarov)^{commit}" 2>$null
    if ($LASTEXITCODE) {
        Write-Host "fetching Kazarov $($pin.kazarov.Substring(0, 8)) from $($pin['kazarov-url'])"
        & git.exe -C $kzRepo fetch -q --depth 1 $pin['kazarov-url'] $pin.kazarov
        if ($LASTEXITCODE) { throw "could not fetch Kazarov $($pin.kazarov) from $($pin['kazarov-url'])" }
    }
}
& git.exe -C $kzRepo archive -o (Join-Path $Out 'kazarov.tar') $pin.kazarov
if ($LASTEXITCODE) { throw "git archive kazarov $($pin.kazarov) failed" }
& $tar -xf (Join-Path $Out 'kazarov.tar') -C $kzStage; if ($LASTEXITCODE) { throw 'tar failed' }
Remove-Item -LiteralPath (Join-Path $Out 'upstream.tar'), (Join-Path $Out 'kazarov.tar')

# 2. the mod's own files
function Copy-Into([string]$From, [string]$To) {
    if (-not (Test-Path -LiteralPath $From)) { return 0 }
    $n = 0
    foreach ($f in Get-ChildItem -LiteralPath $From -Recurse -File -Force) {
        $dest = Join-Path $To $f.FullName.Substring($From.Length).TrimStart('\')
        New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
        $n++
    }
    return $n
}
$nCoop = Copy-Into (Join-Path $Mod 'src\coop') (Join-Path $stage 'port\src\coop')
$nAdded = Copy-Into (Join-Path $Mod 'src\added') $stage
# coop-buildid, release4: the version the link proves (the content hash of src, recipes, build and chronicle-pin.txt, and the
# pins: never a commit, so a README-only commit builds the same exe), as port\src\coop\build_version.hpp.
. (Join-Path $PSScriptRoot 'BuildVersion.ps1')
Write-BuildVersion $Mod $pin $stage

# 3. the recipes, all worked out from the untouched upstream files first, then written
Import-Module (Join-Path $PSScriptRoot 'Recipe.psm1') -Force
$latin1 = [Text.Encoding]::GetEncoding(28591)
$recipes = @(Get-ChildItem -LiteralPath (Join-Path $Mod 'recipes') -Filter '*.recipe' | Sort-Object Name)
$results = @()
foreach ($p in $recipes) {
    $text = [IO.File]::ReadAllText($p.FullName, $latin1)
    $meta = Get-RecipeMeta $text
    if ($meta['base'] -eq '-') { $baseBytes = [byte[]]@() } else {
        $baseFile = Join-Path $stage ($meta['base'] -replace '/', '\')
        if (-not (Test-Path -LiteralPath $baseFile)) { throw "recipe $($p.Name): no $($meta['base']) in upstream $($pin.upstream)" }
        $baseBytes = [IO.File]::ReadAllBytes($baseFile)
    }
    $load = { param($path) $f = Join-Path $stage ($path -replace '/', '\'); if (-not (Test-Path -LiteralPath $f)) { throw "no $path in upstream" }; , [IO.File]::ReadAllBytes($f) }.GetNewClosure()
    $results += [pscustomobject]@{ Target = $meta['target']; Bytes = (Invoke-Recipe $text $baseBytes $load) }
}
foreach ($r in $results) {
    $dest = Join-Path $stage ($r.Target -replace '/', '\')
    New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
    [IO.File]::WriteAllBytes($dest, $r.Bytes)
}

# 4. stage -> src, only what differs
$src = Join-Path $Out 'src'
New-Item -ItemType Directory -Force $src | Out-Null
$copied = 0; $removed = 0
function Get-Rel([string]$Base, [string]$Full) { $Full.Substring($Base.Length).TrimStart('\') }
$want = @{}
foreach ($f in Get-ChildItem -LiteralPath $stage -Recurse -File -Force) {
    $rel = Get-Rel $stage $f.FullName
    $want[$rel] = $true
    $dest = Join-Path $src $rel
    $same = $false
    if (Test-Path -LiteralPath $dest) {
        $d = Get-Item -LiteralPath $dest -Force
        if ($d.Length -eq $f.Length) { $same = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash }
    }
    if (-not $same) {
        New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
        # Written now: git archive gives a file its commit's time, which can be older than its object file.
        (Get-Item -LiteralPath $dest -Force).LastWriteTimeUtc = [DateTime]::UtcNow
        $copied++
    }
}
foreach ($f in @(Get-ChildItem -LiteralPath $src -Recurse -File -Force)) {
    if (-not $want.ContainsKey((Get-Rel $src $f.FullName))) { Remove-Item -LiteralPath $f.FullName -Force; $removed++ }
}
Write-Host "tree: Chronicle $($pin.upstream.Substring(0,8)) + $nCoop coop, $nAdded added files + $($recipes.Count) recipes -> $src ($copied written, $removed removed)"

# 5. compile
$bin = Join-Path $Out 'bin'
# CMake reads a backslash in a cache value as an escape: forward slashes only.
$deps = "$($Root -replace '\\', '/')/deps"
$llvm = "$deps/llvm/$llvmName/bin"
$cmake = $env:DCO_CMAKE
if (-not $cmake) { $c = Get-Command cmake.exe -ErrorAction SilentlyContinue; if ($c) { $cmake = $c.Source } }
if (-not $cmake) { $cmake = @(Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\Kitware.CMake*\*\bin\cmake.exe') -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object FullName)[0] }
if (-not $cmake) { throw 'no cmake.exe on PATH: install CMake 3.28+ or set DCO_CMAKE' }
$rust = "$deps/rust/$rustName/bin"
$env:CARGO_HOME = "$deps/rust/cargo-home"
$env:PATH = "$(Join-Path $Root "deps\rust\$rustName\bin");$(Join-Path $Root "deps\llvm\$llvmName\bin");$(Join-Path $Root 'deps\ninja');$env:PATH"
$json = @()
if ($JsonSource) { $json = @("-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$($JsonSource -replace '\\', '/')") }
# coop-buildid: build\reproducible.cmake keeps the folders and the link time out of the exe; a bin folder configured
# before it is configured again.
$repro = (Join-Path $PSScriptRoot 'reproducible.cmake') -replace '\\', '/'
$cache = Join-Path $bin 'CMakeCache.txt'
if ((Test-Path -LiteralPath $cache) -and -not (Select-String -LiteralPath $cache -Pattern '^CMAKE_PROJECT_INCLUDE[:=]' -Quiet)) { Remove-Item -LiteralPath (Join-Path $bin 'build.ninja') -ErrorAction SilentlyContinue }
# (a bin folder configured at a pin before Chronicle 9418a550 has no Rust or FFmpeg: configured again)
if ((Test-Path -LiteralPath $cache) -and -not (Select-String -LiteralPath $cache -Pattern '^KZ_FFMPEG_DIR[:=]' -Quiet)) { Remove-Item -LiteralPath (Join-Path $bin 'build.ninja') -ErrorAction SilentlyContinue }
# (-NoDcdata given or dropped since the last configure: configured again)
$dcdataOpt = if ($NoDcdata) { 'OFF' } else { 'ON' }
if ((Test-Path -LiteralPath $cache) -and -not (Select-String -LiteralPath $cache -Pattern "^DC_BUILD_DCDATA:BOOL=$dcdataOpt$" -Quiet)) { Remove-Item -LiteralPath (Join-Path $bin 'build.ninja') -ErrorAction SilentlyContinue }
$rustArgs = if ($NoDcdata) { @() } else {
    @("-DRust_COMPILER=$rust/rustc.exe", "-DRust_CARGO=$rust/cargo.exe", '-DRust_CARGO_TARGET=x86_64-pc-windows-gnullvm',
        "-DINSTALLED_CXXBRIDGE=$rust/cxxbridge.exe", "-DFETCHCONTENT_SOURCE_DIR_CORROSION=$deps/rust/corrosion/corrosion-0.6.1")
}
if (-not (Test-Path -LiteralPath (Join-Path $bin 'build.ninja'))) {
    # DC_FIXED_LAYOUT and DC_LINK_MAP: the exe at its preferred address, the arenas pinned and the link map written, so
    # a player joining mid-floor gets the other copy's memory at the same addresses.
    & $cmake -S ($src -replace '\\', '/') -B ($bin -replace '\\', '/') -G Ninja -DPLATFORM=PC -DCMAKE_BUILD_TYPE=RelWithDebInfo `
        "-DCMAKE_CXX_COMPILER=$llvm/clang++.exe" "-DCMAKE_MAKE_PROGRAM=$deps/ninja/ninja.exe" `
        "-DSDL3_DIR=$deps/sdl/SDL3-3.4.18/x86_64-w64-mingw32/lib/cmake/SDL3" `
        "-DVulkan_INCLUDE_DIR=$deps/vulkan/Vulkan-Headers-vulkan-sdk-1.4.363.0/include" `
        '-DVulkan_LIBRARY=C:/Windows/System32/vulkan-1.dll' "-DGLSLANG_VALIDATOR=$deps/glslang/bin/glslang.exe" `
        "-DKZ_FFMPEG_DIR=$deps/ffmpeg/$ffmpegName" @rustArgs "-DDC_BUILD_DCDATA=$dcdataOpt" `
        @json "-DCMAKE_RC_COMPILER=$llvm/windres.exe" -DDC_BUILD_TESTS=OFF -DDC_FIXED_LAYOUT=ON -DDC_LINK_MAP=ON `
        "-DCMAKE_PROJECT_INCLUDE=$repro" "-DDCO_DEPS_DIR=$deps" *> (Join-Path $Out 'configure.log')
    if ($LASTEXITCODE) { Get-Content (Join-Path $Out 'configure.log') -Tail 20; throw 'configure failed (out\build\configure.log)' }
}
& $cmake --build $bin -j $Jobs *> (Join-Path $Out 'build.log')
if ($LASTEXITCODE) { Select-String -LiteralPath (Join-Path $Out 'build.log') -Pattern 'error:|FAILED' | Select-Object -First 30 | ForEach-Object Line; throw 'build failed (out\build\build.log)' }

# The state set beside the exe, from the link map (the game refuses a stale one, so it is remade with every link).
$map = Join-Path $bin 'darkcloud.map'
if (-not (Test-Path -LiteralPath $map)) { throw "no link map at $map" }
# release7 (Jep 11-11): on a PC without py.exe, "if/else { @('-I') }" gave the STRING '-I', which @pyArgs splatted as
# characters ("python.exe - I snapregions.py": the program read from stdin, waiting forever). $pyArgs is always an
# array now, and python's stdin is empty (@() piped in), so nothing it runs can wait on input. Test switch
# DCO_BUILD_MUTATE=unwrapped_pyargs (row l-setup-nopy's twin): the old string.
$py = Get-Command py.exe -ErrorAction SilentlyContinue
[string[]]$pyArgs = @('-3', '-I')
if (-not $py) { $py = Get-Command python.exe -ErrorAction SilentlyContinue; [string[]]$pyArgs = @('-I') }
if ($env:DCO_BUILD_MUTATE -eq 'unwrapped_pyargs' -and -not (Get-Command py.exe -ErrorAction SilentlyContinue)) { Remove-Variable pyArgs; $pyArgs = if ($false) { @('-3', '-I') } else { @('-I') } }
if (-not $py) { throw 'no Python 3 (py.exe or python.exe) on PATH' }
Remove-Item -LiteralPath (Join-Path $bin 'darkcloud.snapregions') -ErrorAction SilentlyContinue   # (a stale one never passes for this link's)
@() | & $py.Source @pyArgs (Join-Path $PSScriptRoot 'snapregions.py') $map --allow (Join-Path $PSScriptRoot 'snapshot-allow.txt') `
    --cxxfilt (Join-Path $Root "deps\llvm\$llvmName\bin\llvm-cxxfilt.exe") -o (Join-Path $bin 'darkcloud.snapregions') --src (Join-Path $src 'port\src')
if ($LASTEXITCODE) { throw 'snapregions.py failed: no state set' }
if (-not (Test-Path -LiteralPath (Join-Path $bin 'darkcloud.snapregions'))) { throw "snapregions.py wrote no state set ($($py.Source) $($pyArgs -join ' '))" }
# The arenas' pin plan (pinplan.py: no two arena owners in one slot, whichever carves first)
@() | & $py.Source @pyArgs (Join-Path $PSScriptRoot 'pinplan.py') $map -o (Join-Path $bin 'darkcloud.pinplan')
if ($LASTEXITCODE) { throw 'pinplan.py failed: two arena owners share a pin slot no probe frees' }

# game\: the game the launcher plays (both PCs must run a build of the same content: the link compares the version). The
# exe there is stripped unless -KeepSymbols; out\build\bin keeps the linked one. Your steam_api64.dll there is kept.
$game = Join-Path $Mod 'game'
$steamDll = Join-Path $game 'steam_api64.dll'
$steamKept = if (Test-Path -LiteralPath $steamDll) { , [IO.File]::ReadAllBytes($steamDll) } else { $null }
$dcdataExe = Join-Path $game 'dcdata.exe'
$dcdataKept = if ($NoDcdata) { , [IO.File]::ReadAllBytes($dcdataExe) } else { $null }
Clear-Tree $game
New-Item -ItemType Directory -Force $game | Out-Null
if ($steamKept) { [IO.File]::WriteAllBytes($steamDll, $steamKept) }
if ($NoDcdata) { [IO.File]::WriteAllBytes($dcdataExe, $dcdataKept) } else {
    & (Join-Path $Root "deps\llvm\$llvmName\bin\llvm-strip.exe") --strip-all (Join-Path $bin 'dcdata.exe') -o $dcdataExe
    if ($LASTEXITCODE) { throw 'llvm-strip failed on dcdata.exe' }
}
$exe = Join-Path $game 'darkcloud.exe'
if ($KeepSymbols) { Copy-Item -LiteralPath (Join-Path $bin 'darkcloud.exe') -Destination $exe } else {
    & (Join-Path $Root "deps\llvm\$llvmName\bin\llvm-strip.exe") --strip-all (Join-Path $bin 'darkcloud.exe') -o $exe
    if ($LASTEXITCODE) { throw 'llvm-strip failed' }
}
Copy-Item -LiteralPath (Join-Path $bin 'darkcloud.snapregions') -Destination $game
Copy-Item -LiteralPath (Join-Path $bin 'darkcloud.pinplan') -Destination $game
foreach ($d in 'lang', 'glyphs') { Copy-Item -Recurse -LiteralPath (Join-Path $bin $d) -Destination (Join-Path $game $d) }
Copy-Item -Recurse -LiteralPath (Join-Path $Mod 'src\added\port\steam_input') -Destination (Join-Path $game 'steam_input')
# The runtime DLLs, from deps\ (where Chronicle's tools\windows\build.ps1 takes them), FFmpeg's among them (LGPL:
# NOTICE.md).
$runtime = Join-Path $Root "deps\llvm\$llvmName\x86_64-w64-mingw32\bin"
foreach ($d in @((Join-Path $runtime 'libc++.dll'), (Join-Path $runtime 'libunwind.dll'), (Join-Path $runtime 'libwinpthread-1.dll'),
                 (Join-Path $Root 'deps\sdl\SDL3-3.4.18\x86_64-w64-mingw32\bin\SDL3.dll')) +
               @(Get-ChildItem -LiteralPath (Join-Path $Root "deps\ffmpeg\$ffmpegName\bin") -Filter '*.dll' | ForEach-Object FullName)) {
    Copy-Item -LiteralPath $d -Destination $game -Force
}
# FFmpeg's licence and its build's configuration and version, beside the DLLs (LGPL 2.1).
New-Item -ItemType Directory -Force (Join-Path $game 'ffmpeg') | Out-Null
foreach ($f in 'COPYING.LGPLv2.1', 'kz-ffmpeg.json') { Copy-Item -LiteralPath (Join-Path $Root "deps\ffmpeg\$ffmpegName\$f") -Destination (Join-Path $game 'ffmpeg') -Force }
# The Steam helper (dcsteam.exe, src\helper): the game starts it for NAT > Steam; it alone loads steam_api64.dll.
& "$llvm/clang++.exe" -std=c++20 -O2 -static -s -I (Join-Path $Mod 'src\helper\shim') -I (Join-Path $Mod 'src') (Join-Path $Mod 'src\helper\dcsteam.cpp') `
    (Join-Path $Mod 'src\coop\netplay_steam_core.cpp') -lws2_32 '-Wl,--no-insert-timestamp' -o (Join-Path $game 'dcsteam.exe')
if ($LASTEXITCODE) { throw 'the Steam helper (dcsteam.exe) did not build' }
# The launcher, Play Dark Cloud Co-op.exe beside this README: a Windows program (no console), with Chronicle's icon
# from your checkout, statically linked so it needs none of the game's DLLs.
$launcherOut = Join-Path $Out 'launcher'
New-Item -ItemType Directory -Force $launcherOut | Out-Null
$ico = (Join-Path $Chronicle 'port\icon\chronicle.ico') -replace '\\', '/'
[IO.File]::WriteAllText((Join-Path $launcherOut 'launcher.rc'), "1 ICON `"$ico`"`n")
& "$llvm/windres.exe" (Join-Path $launcherOut 'launcher.rc') -O coff -o (Join-Path $launcherOut 'launcher.res')
if ($LASTEXITCODE) { throw 'windres failed on the launcher icon' }
$launcher = Join-Path $Mod 'Play Dark Cloud Co-op.exe'
& "$llvm/clang++.exe" -std=c++20 -O2 -municode -mwindows -static -s (Join-Path $Mod 'launcher\launcher.cpp') (Join-Path $launcherOut 'launcher.res') -lcomdlg32 '-Wl,--no-insert-timestamp' -o $launcher
if ($LASTEXITCODE) { throw 'the launcher did not build' }
$e = Get-Item -LiteralPath $exe
Write-Host "built $($e.FullName) ($($e.Length) bytes, $([int]((Get-Date) - $t0).TotalSeconds) s) SHA-256 $((Get-FileHash -LiteralPath $e.FullName -Algorithm SHA256).Hash)"
