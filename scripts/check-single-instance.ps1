<#
.SYNOPSIS
    Guide case A-61, end to end: `general/singleInstance=false` gives two processes, and the
    control shows that `true` gives one.

.DESCRIPTION
    WHY THIS IS A SCRIPT AND NOT A CTEST CASE.

    A-61 is about two PROCESSES. The decision it tests -- forward, or run standalone -- is made from
    `general/singleInstance`, which lives in the settings file, and there is no way to point a child
    `rawpage.exe` at a different one. Measured at T15: Qt resolves QStandardPaths::AppDataLocation
    through SHGetKnownFolderPath, so setting the APPDATA environment variable for the child does not
    move the file. `CompositionRoot::Options::settingsPath` exists but `main.cpp` exposes no switch
    for it, and inventing a CLI verb at T15 would add to the vocabulary Features.md 9.6 owns for
    Stage 14.

    So this harness edits the real settings file and puts it back. That is exactly why it is not a
    ctest case: a suite that rewrote the settings of whoever ran it would be a worse defect than the
    one it is checking. The restore is in a `finally`, and the original bytes are copied aside
    first -- including the case where there is no original, which is restored as "delete it again".

    The policy half of A-61 -- that `enabled=false` never touches the bus at all -- IS a ctest case:
    `unit.A-61*` in tests/unit/tst_singleinstance.cpp. This script is the half that needs two real
    processes and a real mutex.

    BOTH DIRECTIONS ARE RUN. A run that only checked `false` would pass against a build whose
    single-instance code had been deleted entirely, which is convention 7 ("every negative control
    must actually exercise the thing") applied to a harness rather than to a fixture.

.PARAMETER Exe
    The rawpage.exe to test. Defaults to the x64-release build.

.PARAMETER SettleSeconds
    How long a launched process is given to reach its decision before it is inspected. A secondary
    forwards and exits within milliseconds; this is slack, not a race.

.EXAMPLE
    . .\scripts\env.ps1
    .\scripts\check-single-instance.ps1
#>

[CmdletBinding()]
param(
    [string] $Exe = "$PSScriptRoot\..\build\x64-release\bin\rawpage.exe",
    [double] $SettleSeconds = 3.0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- Preconditions -----------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "rawpage.exe not found at $Exe -- build it first: cmake --build --preset x64-release"
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path

# The same path core::Settings::defaultFilePath() resolves, and derived the same way rather than
# hardcoded: %APPDATA%\RawPage\settings.ini, with no organization segment because app/ leaves the
# organization name unset (issue I-13).
$settingsPath = Join-Path $env:APPDATA 'RawPage\settings.ini'
$settingsDir  = Split-Path -Parent $settingsPath

# Offscreen, so this runs on a build machine and in a terminal without stealing focus. It does not
# weaken the case: whether a process forwards is decided before any window exists, and the thing
# being counted is PROCESSES.
$env:QT_QPA_PLATFORM = 'offscreen'

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("rawpage-a61-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

$backup      = Join-Path $scratch 'settings.ini.bak'
$hadOriginal = Test-Path -LiteralPath $settingsPath

$failures = New-Object System.Collections.Generic.List[string]
$launched = New-Object System.Collections.Generic.List[System.Diagnostics.Process]

function Write-Setting([bool] $enabled) {
    New-Item -ItemType Directory -Force -Path $settingsDir | Out-Null
    # Written by hand rather than through the product, so the harness does not depend on the code it
    # is testing to set up its own precondition. `[general]` is deliberate: convention 4 records
    # that QSettings treats it as its own reserved fallback section, and core::Settings handles that
    # fold -- so this is also the shape a user's hand edit would take.
    $value = if ($enabled) { 'true' } else { 'false' }
    Set-Content -LiteralPath $settingsPath -Encoding UTF8 -Value @(
        '[general]',
        "singleInstance=$value"
    )
}

# Launch, wait out the settle interval, and report whether the process is still alive. A primary
# runs an event loop and stays; a secondary forwards and exits 0.
function Start-Instance([string] $tag) {
    $out = Join-Path $scratch "$tag.out.txt"
    $err = Join-Path $scratch "$tag.err.txt"
    $p = Start-Process -FilePath $Exe -PassThru -NoNewWindow `
                       -RedirectStandardOutput $out -RedirectStandardError $err
    $launched.Add($p) | Out-Null
    [pscustomobject]@{ Process = $p; Tag = $tag; Out = $out; Err = $err }
}

function Get-InstanceLog($inst) {
    # The console sink writes to stderr; read both anyway, because a sink that moved is a finding
    # rather than a reason for this harness to see nothing.
    $text = ''
    foreach ($f in @($inst.Err, $inst.Out)) {
        if (Test-Path -LiteralPath $f) { $text += (Get-Content -LiteralPath $f -Raw -ErrorAction SilentlyContinue) }
    }
    return $text
}

function Assert([bool] $condition, [string] $what) {
    if ($condition) {
        Write-Host "  PASS  $what" -ForegroundColor Green
    } else {
        Write-Host "  FAIL  $what" -ForegroundColor Red
        $failures.Add($what) | Out-Null
    }
}

try {
    if ($hadOriginal) {
        Copy-Item -LiteralPath $settingsPath -Destination $backup -Force
        Write-Host "Saved your settings.ini to $backup" -ForegroundColor DarkGray
    } else {
        Write-Host "No settings.ini existed; it will be deleted again on the way out." -ForegroundColor DarkGray
    }

    # =========================================================================================
    # A-61 -- singleInstance = false. Two processes, neither forwarding.
    # =========================================================================================
    Write-Host ''
    Write-Host 'A-61  general/singleInstance = false' -ForegroundColor Cyan
    Write-Setting $false

    $a = Start-Instance 'a61-first'
    Start-Sleep -Seconds $SettleSeconds
    $b = Start-Instance 'a61-second'
    Start-Sleep -Seconds $SettleSeconds

    $a.Process.Refresh(); $b.Process.Refresh()
    $aAlive = -not $a.Process.HasExited
    $bAlive = -not $b.Process.HasExited

    Assert $aAlive 'the first process is still running'
    Assert $bAlive 'the second process is still running -- it did NOT forward and exit'

    $aLog = Get-InstanceLog $a
    $bLog = Get-InstanceLog $b
    Assert ($aLog -match 'single instance: disabled by general/singleInstance') `
           'the first process logged that single-instance is disabled'
    Assert ($bLog -match 'single instance: disabled by general/singleInstance') `
           'the second process logged that single-instance is disabled'
    Assert (-not ($aLog -match '-- secondary')) 'neither process called itself a secondary'
    Assert (-not ($bLog -match '-- secondary')) 'the second process is not a secondary'

    foreach ($p in @($a, $b)) { if (-not $p.Process.HasExited) { $p.Process.Kill() } }
    Start-Sleep -Milliseconds 500

    # =========================================================================================
    # THE CONTROL -- singleInstance = true. One process; the second forwards and exits 0.
    # =========================================================================================
    Write-Host ''
    Write-Host 'A-61 control  general/singleInstance = true' -ForegroundColor Cyan
    Write-Setting $true

    $c = Start-Instance 'a61-ctl-first'
    Start-Sleep -Seconds $SettleSeconds
    $d = Start-Instance 'a61-ctl-second'
    $exited = $d.Process.WaitForExit([int]($SettleSeconds * 1000))

    $c.Process.Refresh()
    Assert (-not $c.Process.HasExited) 'the first process is still running -- it is the primary'
    Assert $exited                     'the second process exited rather than opening its own window'
    if ($exited) {
        Assert ($d.Process.ExitCode -eq 0) "the second process exited 0 (got $($d.Process.ExitCode))"
    }

    $cLog = Get-InstanceLog $c
    $dLog = Get-InstanceLog $d
    Assert ($cLog -match '-- primary')   'the first process called itself the primary'
    Assert ($dLog -match '-- secondary') 'the second process called itself a secondary'

    if (-not $c.Process.HasExited) { $c.Process.Kill() }
}
finally {
    foreach ($p in $launched) {
        try { $p.Refresh(); if (-not $p.HasExited) { $p.Kill() } } catch { }
    }

    # The restore, and it runs whatever happened above -- including Ctrl+C, which is why the whole
    # body is in a try.
    if ($hadOriginal) {
        Copy-Item -LiteralPath $backup -Destination $settingsPath -Force
        Write-Host ''
        Write-Host "Restored your settings.ini from $backup" -ForegroundColor DarkGray
    } elseif (Test-Path -LiteralPath $settingsPath) {
        Remove-Item -LiteralPath $settingsPath -Force
        Write-Host ''
        Write-Host 'Removed the settings.ini this harness created.' -ForegroundColor DarkGray
    }
}

Write-Host ''
if ($failures.Count -eq 0) {
    Write-Host 'A-61: PASS' -ForegroundColor Green
    exit 0
}
Write-Host "A-61: FAIL -- $($failures.Count) assertion(s)" -ForegroundColor Red
foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
Write-Host "Process output kept in $scratch" -ForegroundColor DarkGray
exit 1
