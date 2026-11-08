# PGO-1: profile-guided Release build (MSVC) of the standalone and the plugin, for a release.
#
#   powershell -File tools\pgo_build.ps1 [-Tree <source tree>] [-RomDir <folder with flash.bin>] [-PluginSeconds 30]
#
# 1. configures <Tree>\build and <Tree>\build_vst with MS2K_PGO=GEN and builds them (instrumented),
# 2. trains: the standalone headless (a chord unpaced, a note with CCs paced, a long unpaced chord + CC run)
#    and the plugin in MS2000R_HostTest (48 kHz / 128, PluginSeconds of audio),
# 3. moves the .pgc files beside the .pgd (<build>\pgo) and rebuilds both with MS2K_PGO=USE.
# The build folders must already be configured (generator, toolchain, CMAKE_PREFIX_PATH for the GUI) - this
# script only changes MS2K_PGO. They are left at USE: the outputs are the release binaries. Set MS2K_PGO=OFF
# again for development builds - the profile is stale after any source change.
# The training needs the firmware (flash.bin, boot-362.bin) in RomDir; nothing in RomDir is written
# (MS2K_FLASHSTATE=off). The instrumented plugin runs ~30x slower than real time: 30 s of audio take ~4 min.
param(
    [string]$Tree = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$RomDir = $(if ($env:MS2K_HOME) { $env:MS2K_HOME } else { 'F:\MS2000' }),
    [int]$PluginSeconds = 30
)
# not 'Stop': Windows PowerShell 5.1 turns a native tool's stderr line (a CMake warning) into a terminating error;
# every native step is checked by its exit code instead
$ErrorActionPreference = 'Continue'
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }
if (-not (Test-Path (Join-Path $RomDir 'flash.bin'))) { throw "no flash.bin in RomDir '$RomDir'" }
$builds = @('build', 'build_vst') | Where-Object { Test-Path (Join-Path $Tree "$_\CMakeCache.txt") }
if ($builds.Count -eq 0) { throw "no configured build folder in $Tree (build, build_vst)" }
$logDir = Join-Path $Tree '_pgo_logs'; New-Item -ItemType Directory -Force $logDir | Out-Null

function Build-All([string]$mode) {
    foreach ($b in $builds) {
        $bd = Join-Path $Tree $b
        & $cmake -S $Tree -B $bd "-DMS2K_PGO=$mode" *> (Join-Path $logDir "cfg_${b}_$mode.txt")
        if ($LASTEXITCODE) { throw "configure $b ($mode) failed - see $logDir" }
        & $cmake --build $bd --config Release --parallel *> (Join-Path $logDir "build_${b}_$mode.txt")
        if ($LASTEXITCODE) { throw "build $b ($mode) failed - see $logDir" }
        "[PGO] $b built ($mode)"
    }
}

# the PGO runtime (pgort140.dll) sits beside the compiler the build folder uses
$cxx = Get-ChildItem (Join-Path $Tree "$($builds[0])\CMakeFiles") -Recurse -Filter CMakeCXXCompiler.cmake | Select-Object -First 1
$cl = (Select-String $cxx.FullName -Pattern 'set\(CMAKE_CXX_COMPILER "([^"]+)"').Matches[0].Groups[1].Value
$env:PATH = (Split-Path $cl) + ';' + $env:PATH
if (-not (Test-Path (Join-Path (Split-Path $cl) 'pgort140.dll'))) { throw "pgort140.dll not beside $cl" }

foreach ($b in $builds) { Remove-Item (Join-Path $Tree "$b\pgo\*") -Force -ErrorAction SilentlyContinue }
Build-All 'GEN'

# ---- training ----
Remove-Item Env:\MS2K_* -ErrorAction SilentlyContinue
$env:MS2K_FLASHSTATE = 'off'; $env:MS2K_MODEL = 'R'; $env:MS2K_HOME = $RomDir
Push-Location $RomDir
try {
    if ($builds -contains 'build') {
        $exe = Join-Path $Tree 'build\Release\ms2000_emulator.exe'
        $runs = @(
            @{ pace = '1'; dur = 20; midi = '4:903C64;4.1:904064;4.2:904364;12:803C40;12:804040;12:804340' },
            @{ pace = '';  dur = 13; midi = '4:903C64;5:B00A00;7:B00A40;9:B00A7F;11:803C40' },
            @{ pace = '1'; dur = 75; midi = '4:903C64;4.1:904064;4.2:904364;12:803C40;12:804040;12:804340;14:903064;15:B00A20;16:B04A10;17:B04A7F;20:803040' })
        $i = 0
        foreach ($r in $runs) {
            $i++
            if ($r.pace) { $env:MS2K_NOPACE = '1' } else { Remove-Item Env:\MS2K_NOPACE -ErrorAction SilentlyContinue }
            $env:MS2K_MIDIIN = $r.midi
            & $exe --headless --quiet-boot --duration $r.dur *> (Join-Path $logDir "train_sa$i.txt")
            "[PGO] standalone training run $i exit $LASTEXITCODE"
        }
        Remove-Item Env:\MS2K_NOPACE, Env:\MS2K_MIDIIN -ErrorAction SilentlyContinue
    }
    if ($builds -contains 'build_vst') {
        $ht = Join-Path $Tree 'build_vst\source\plugin\MS2000R_HostTest_artefacts\Release\MS2000R_HostTest.exe'
        $vst = Join-Path $Tree 'build_vst\source\plugin\MS2000R_VST_artefacts\Release\VST3\MS2000R.vst3'
        & $ht $vst (Join-Path $logDir 'train_vst.wav') 48000 128 $PluginSeconds *> (Join-Path $logDir 'train_vst.txt')
        "[PGO] plugin training exit $LASTEXITCODE"
    }
} finally { Pop-Location }

# ---- the counts beside the .pgd, then the optimised build ----
$pgcFrom = @{ 'build' = 'build\Release'; 'build_vst' = 'build_vst\source\plugin\MS2000R_VST_artefacts\Release\VST3\MS2000R.vst3\Contents\x86_64-win' }
foreach ($b in $builds) {
    $pgc = Get-ChildItem (Join-Path $Tree $pgcFrom[$b]) -Filter *.pgc
    if ($pgc.Count -eq 0) { throw "no .pgc files from the $b training" }
    $pgc | Move-Item -Destination (Join-Path $Tree "$b\pgo") -Force
    "[PGO] $b : $($pgc.Count) .pgc"
}
Build-All 'USE'
foreach ($b in $builds) {
    Select-String (Join-Path $logDir "build_${b}_USE.txt") -Pattern 'optimized using profile data' | ForEach-Object { "[PGO] $b : $($_.Line.Trim())" }
}
"[PGO] done - the build folders are at MS2K_PGO=USE; logs in $logDir"
