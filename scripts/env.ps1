<#
.SYNOPSIS
    Puts RawPage's pinned build environment on PATH for the current shell.

.DESCRIPTION
    DOT-SOURCE THIS SCRIPT -- it modifies the calling session:

        . .\scripts\env.ps1

    Running it normally (.\scripts\env.ps1) changes a child process that immediately exits,
    which does nothing. The script detects that and says so.

    Everything here comes from the Qt installation, which IS the build environment
    (Tech-Stack-Plan.md 6.3): CMake, Ninja, the pinned GCC, windeployqt, and the binutils
    (gendef/dlltool for PDFium's import library, gdb/addr2line for DWARF symbolization).
    A build box needs Qt and vcpkg, nothing else.

    It also sets VCPKG_ROOT. That is deliberate: setx writes the registry but never reaches
    an already-running process, so depending on the machine's environment makes the build
    work or not work for reasons invisible in the repository. Setting it here is what makes
    the clean-machine case (testing guide M-01) pass.

.PARAMETER Quiet
    Suppress the version summary.
#>
[CmdletBinding()]
param(
    [switch]$Quiet
)

# --- Dot-source check -------------------------------------------------------
# $MyInvocation.InvocationName is '.' only when dot-sourced.
if ($MyInvocation.InvocationName -ne '.') {
    Write-Warning @'
env.ps1 was run, not dot-sourced, so it changed a child process and did nothing here.
Run it as:    . .\scripts\env.ps1
'@
    return
}

$ErrorActionPreference = 'Stop'

# --- The pin ----------------------------------------------------------------
# Overridable, so a machine with Qt elsewhere can set these before dot-sourcing.
# The values below are the pin asserted by cmake/toolchain-mingw.cmake; changing them
# here does not loosen that assertion, it only tells the script where to look.
if (-not $env:RAWPAGE_QT_PREFIX)  { $env:RAWPAGE_QT_PREFIX  = 'C:\Qt\6.11.1\mingw_64' }
if (-not $env:RAWPAGE_MINGW_ROOT) { $env:RAWPAGE_MINGW_ROOT = 'C:\Qt\Tools\mingw1310_64' }

$cmakeBin = 'C:\Qt\Tools\CMake_64\bin'
$ninjaBin = 'C:\Qt\Tools\Ninja'
$mingwBin = Join-Path $env:RAWPAGE_MINGW_ROOT 'bin'
$qtBin    = Join-Path $env:RAWPAGE_QT_PREFIX  'bin'

# --- Verify before touching PATH -------------------------------------------
$missing = @()
foreach ($entry in @(
    @{ Path = $cmakeBin; What = 'CMake (C:\Qt\Tools\CMake_64)' },
    @{ Path = $ninjaBin; What = 'Ninja (C:\Qt\Tools\Ninja)' },
    @{ Path = $mingwBin; What = "MinGW GCC (`$env:RAWPAGE_MINGW_ROOT = $env:RAWPAGE_MINGW_ROOT)" },
    @{ Path = $qtBin;    What = "Qt (`$env:RAWPAGE_QT_PREFIX = $env:RAWPAGE_QT_PREFIX)" }
)) {
    if (-not (Test-Path $entry.Path)) { $missing += $entry.What }
}

if ($missing.Count -gt 0) {
    Write-Host 'RawPage build environment NOT configured. Missing:' -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ''
    Write-Host 'Install Qt 6.11.1 for MinGW plus the mingw1310_64, CMake, and Ninja tool' -ForegroundColor Yellow
    Write-Host 'packages, or point RAWPAGE_QT_PREFIX / RAWPAGE_MINGW_ROOT at an existing install' -ForegroundColor Yellow
    Write-Host 'before dot-sourcing this script. See README.md -> Prerequisites.' -ForegroundColor Yellow
    return
}

# --- Prepend to PATH, idempotently -----------------------------------------
# Order matters: the pinned GCC must win over any other compiler already on PATH
# (an MSYS2 install is the realistic collision). Prepending in reverse puts
# $mingwBin first once the loop finishes.
foreach ($dir in @($qtBin, $ninjaBin, $cmakeBin, $mingwBin)) {
    $parts = $env:PATH -split ';' | Where-Object { $_ -ne '' }
    if ($parts -notcontains $dir) {
        $env:PATH = "$dir;$env:PATH"
    }
}

# --- vcpkg ------------------------------------------------------------------
# Prefer an already-set value; then the user registry (which setx wrote but this
# process never saw); then the documented default.
if (-not $env:VCPKG_ROOT) {
    $fromRegistry = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
    if ($fromRegistry) {
        $env:VCPKG_ROOT = $fromRegistry
    } else {
        $env:VCPKG_ROOT = Join-Path $env:USERPROFILE 'vcpkg'
    }
}

if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
    Write-Warning "VCPKG_ROOT = '$env:VCPKG_ROOT' but no vcpkg.exe is there. Run scripts\bootstrap.ps1 for the fix."
}

# --- Summary ----------------------------------------------------------------
if (-not $Quiet) {
    $cmakeVersion = (& cmake --version   | Select-Object -First 1) -replace '^cmake version\s*', ''
    $ninjaVersion = (& ninja --version)
    $gccVersion   = (& g++ -dumpversion)
    $gccTriple    = (& g++ -dumpmachine)

    Write-Host 'RawPage build environment ready' -ForegroundColor Green
    Write-Host ("  cmake      {0}" -f $cmakeVersion)
    Write-Host ("  ninja      {0}" -f $ninjaVersion)
    Write-Host ("  g++        {0}  ({1})" -f $gccVersion, $gccTriple)
    Write-Host ("  Qt prefix  {0}" -f $env:RAWPAGE_QT_PREFIX)
    Write-Host ("  VCPKG_ROOT {0}" -f $env:VCPKG_ROOT)
}
