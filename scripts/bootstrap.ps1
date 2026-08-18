<#
.SYNOPSIS
    Verifies that this machine can build Recto, and says exactly what to run if it cannot.

.DESCRIPTION
    Run it normally (NOT dot-sourced):

        .\scripts\bootstrap.ps1

    This script CHECKS and REPORTS. It deliberately installs nothing and downloads nothing:
    cloning vcpkg or fetching a prebuilt binary is the developer's action, not a script's
    side effect, and a bootstrap that silently mutates the machine is one that cannot be
    trusted on a build box.

    Its contract is the one the testing guide's case M-04 asserts: when something is
    missing, say WHAT is missing, WHERE it should live, and WHAT COMMAND fixes it.

    Exit code 0 = ready to build. Non-zero = at least one blocking problem, count in output.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'
$problems = 0
$warnings = 0

function Test-Item {
    param(
        [string]$Name,
        [scriptblock]$Check,
        [string]$Detail,
        [string]$Fix,
        [switch]$WarnOnly
    )
    $ok = $false
    try { $ok = [bool](& $Check) } catch { $ok = $false }

    if ($ok) {
        Write-Host ("  [ OK ] {0}" -f $Name) -ForegroundColor Green
        if ($Detail) { Write-Host ("         {0}" -f $Detail) -ForegroundColor DarkGray }
    } elseif ($WarnOnly) {
        Write-Host ("  [WARN] {0}" -f $Name) -ForegroundColor Yellow
        if ($Fix) { Write-Host ("         {0}" -f $Fix) -ForegroundColor Yellow }
        $script:warnings++
    } else {
        Write-Host ("  [FAIL] {0}" -f $Name) -ForegroundColor Red
        if ($Fix) { Write-Host ("         {0}" -f $Fix) -ForegroundColor Yellow }
        $script:problems++
    }
}

$qtPrefix  = if ($env:RECTO_QT_PREFIX)  { $env:RECTO_QT_PREFIX }  else { 'C:\Qt\6.11.1\mingw_64' }
$mingwRoot = if ($env:RECTO_MINGW_ROOT) { $env:RECTO_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }

Write-Host ''
Write-Host 'Recto build prerequisites' -ForegroundColor Cyan
Write-Host '-------------------------'

# --- Qt and the pinned toolchain -------------------------------------------
Write-Host 'Qt installation (IS the build environment -- Tech-Stack-Plan.md 6.3)'

Test-Item -Name 'Qt 6.11.1 for MinGW' `
    -Check { Test-Path (Join-Path $qtPrefix 'lib\cmake\Qt6') } `
    -Detail $qtPrefix `
    -Fix "Not found at '$qtPrefix'. Install Qt 6.11.1 (MinGW 64-bit) with the Qt Maintenance Tool, or set RECTO_QT_PREFIX."

Test-Item -Name 'MinGW GCC 13.1.0 (Qt''s own bundled compiler)' `
    -Check { Test-Path (Join-Path $mingwRoot 'bin\g++.exe') } `
    -Detail $mingwRoot `
    -Fix "Not found at '$mingwRoot'. Install the 'MinGW 13.1.0 64-bit' tool package in the Qt Maintenance Tool. An MSYS2 or MinGW-Builds GCC is NOT a substitute -- see Tech-Stack-Plan.md 3.3 (A7)."

Test-Item -Name 'CMake (from the Qt install)' `
    -Check { Test-Path 'C:\Qt\Tools\CMake_64\bin\cmake.exe' } `
    -Fix "Not found. Install the 'CMake' tool package in the Qt Maintenance Tool."

Test-Item -Name 'Ninja (from the Qt install)' `
    -Check { Test-Path 'C:\Qt\Tools\Ninja\ninja.exe' } `
    -Fix "Not found. Install the 'Ninja' tool package in the Qt Maintenance Tool."

Test-Item -Name 'Binutils for the PDFium import library (gendef, dlltool)' `
    -Check { (Test-Path (Join-Path $mingwRoot 'bin\gendef.exe')) -and (Test-Path (Join-Path $mingwRoot 'bin\dlltool.exe')) } `
    -Fix 'Missing from the MinGW tool package; without them the PDFium DLL cannot be linked (risk A6).'

# --- The compiler actually resolves to the pinned one ----------------------
$gccOnPath = (Get-Command g++ -ErrorAction SilentlyContinue)
if ($gccOnPath) {
    $resolved = (Resolve-Path $gccOnPath.Source).Path
    $expected = (Join-Path $mingwRoot 'bin\g++.exe')
    Test-Item -Name 'g++ on PATH is the pinned one' `
        -Check { $resolved -ieq $expected } `
        -Detail $resolved `
        -Fix "PATH resolves g++ to '$resolved' but the pin is '$expected'. Dot-source scripts\env.ps1, which prepends the pinned toolchain." `
        -WarnOnly
} else {
    Write-Host '  [INFO] g++ is not on PATH yet -- dot-source scripts\env.ps1 before building.' -ForegroundColor DarkGray
}

# --- vcpkg ------------------------------------------------------------------
Write-Host ''
Write-Host 'vcpkg (manifest mode, x64-mingw-dynamic)'

$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot) { $vcpkgRoot = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User') }
$defaultVcpkg = Join-Path $env:USERPROFILE 'vcpkg'
if (-not $vcpkgRoot) { $vcpkgRoot = $defaultVcpkg }

Test-Item -Name 'VCPKG_ROOT resolves to a directory' `
    -Check { Test-Path $vcpkgRoot } `
    -Detail $vcpkgRoot `
    -Fix "'$vcpkgRoot' does not exist. Clone it:`n           git clone https://github.com/microsoft/vcpkg `"$defaultVcpkg`""

Test-Item -Name 'vcpkg is bootstrapped' `
    -Check { Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe') } `
    -Fix "No vcpkg.exe under '$vcpkgRoot'. Run:`n           $vcpkgRoot\bootstrap-vcpkg.bat"

Test-Item -Name 'x64-mingw-dynamic triplet available' `
    -Check { Test-Path (Join-Path $vcpkgRoot 'triplets\community\x64-mingw-dynamic.cmake') } `
    -Detail 'community triplet -- which is exactly why risk A5 is a question, not an assumption' `
    -Fix "Triplet missing from '$vcpkgRoot'. Update the clone: git -C `"$vcpkgRoot`" pull"

# VCPKG_ROOT set in the process vs only in the registry: the classic "I set it but it
# did not take" case. Not fatal -- env.ps1 sets it -- but worth naming.
if (-not $env:VCPKG_ROOT) {
    Write-Host '  [INFO] VCPKG_ROOT is not set in THIS process (setx only reaches newly launched' -ForegroundColor DarkGray
    Write-Host '         shells). scripts\env.ps1 sets it, so the build does not depend on it.' -ForegroundColor DarkGray
}

# --- Baseline pin -----------------------------------------------------------
$repoRoot   = Split-Path -Parent $PSScriptRoot
$vcpkgConfig = Join-Path $repoRoot 'vcpkg-configuration.json'
# Only meaningful against a clone that exists. Without this the check reported
# 'the clone is at .' -- an empty revision rendered as a bare full stop, dressed as a
# warning, directly under three FAILs that already said the directory is missing (I-14).
if (-not (Test-Path (Join-Path $vcpkgRoot '.git'))) {
    Write-Host '  [INFO] Baseline not checked -- no vcpkg clone to compare against.' -ForegroundColor DarkGray
} elseif (Test-Path $vcpkgConfig) {
    try {
        $pinned = (Get-Content $vcpkgConfig -Raw | ConvertFrom-Json).'default-registry'.baseline
        $actual = (& git -C $vcpkgRoot rev-parse HEAD 2>$null)
        Test-Item -Name 'vcpkg baseline matches the pin' `
            -Check { $pinned -and $actual -and ($pinned -eq $actual) } `
            -Detail "pinned $pinned" `
            -Fix "vcpkg-configuration.json pins $pinned but the clone is at $actual. This is NOT an error -- manifest mode fetches the pinned baseline regardless. Only update the pin deliberately." `
            -WarnOnly
    } catch {
        Write-Host '  [WARN] vcpkg-configuration.json present but unreadable.' -ForegroundColor Yellow
        $warnings++
    }
} else {
    Write-Host '  [INFO] No vcpkg-configuration.json yet -- Stage 0 task T5/T6 creates it.' -ForegroundColor DarkGray
}

# --- Git --------------------------------------------------------------------
Write-Host ''
Write-Host 'Repository'

Test-Item -Name 'git available' `
    -Check { [bool](Get-Command git -ErrorAction SilentlyContinue) } `
    -Fix 'Install Git for Windows.'

Test-Item -Name '.gitattributes present' `
    -Check { Test-Path (Join-Path $repoRoot '.gitattributes') } `
    -Fix 'Missing. Without it, checkout can inject CRLF into corpus PDFs and shift every xref offset. See the file header for why this is not cosmetic.'

# --- Verdict ----------------------------------------------------------------
Write-Host ''
if ($problems -eq 0) {
    if ($warnings -gt 0) {
        Write-Host "Ready to build, with $warnings warning(s) above." -ForegroundColor Yellow
    } else {
        Write-Host 'Ready to build.' -ForegroundColor Green
    }
    Write-Host 'Next:  . .\scripts\env.ps1'
    exit 0
} else {
    Write-Host "$problems blocking problem(s) above. Fix them, then re-run this script." -ForegroundColor Red
    exit 1
}
