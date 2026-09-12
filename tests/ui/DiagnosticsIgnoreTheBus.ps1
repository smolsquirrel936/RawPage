<#
.SYNOPSIS
    Guide case A-87: a diagnostic that composes and exits prints its answer WHILE A PRIMARY HOLDS
    THE SINGLE-INSTANCE BUS. Issue I-27.

.DESCRIPTION
    WHY THIS IS TWO PROCESSES AND THEREFORE A SCRIPT.

    `--dump-ui-strings`, `--print-screen-metrics` and `--list-actions` compose the whole product and
    exit without ever becoming a session. Until 2026-09-13 `app/main.cpp` claimed the single-instance
    bus first, so with a window already open each of them became the SECONDARY: it forwarded, it
    returned 0, and it printed nothing. **Exiting 0 is also what a successful diagnostic does**, so
    no caller could tell the two apart -- and `ui.screen_metrics_invariant` (A-84) failed for anyone
    who happened to have the product open, which is how this was found.

    The condition cannot be reached inside one process: a bus claim needs a claimant. Nothing here
    touches settings.ini, which is what lets this be a CTest case where `scripts/check-single-
    instance.ps1` (A-61) cannot be.

    THE CONTROL COMES FIRST AND IT IS THE POINT (convention 7). A run with no primary alive would
    pass against the very build this case exists to fail, because the diagnostics work perfectly
    when nothing holds the bus. So before asserting anything, this launches a second GUI process and
    requires it to EXIT -- that exit is the proof that the bus is genuinely claimed. If it does not
    exit, the bus is not held, the case is measuring nothing, and it SKIPS rather than passing.

    IT NEVER KILLS A WINDOW IT DID NOT OPEN. A walker may have the product open on their own
    document -- the M-05 walk is exactly what was running when I-27 was found -- so an existing
    instance is ADOPTED as the primary and left alone, and only a process this script started is
    stopped at the end. `taskkill /IM rawpage.exe` would have made this case a hazard to the person
    running it.

.PARAMETER Exe
    The rawpage.exe to drive.

.PARAMETER SettleSeconds
    How long a launched process is given to reach its decision before it is inspected. A secondary
    forwards and exits within milliseconds; this is slack, not a race.

.NOTES
    Exit 0 pass, 1 fail, 77 skip (CTest's SKIP_RETURN_CODE for this case).
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [int] $SettleSeconds = 5
)

$ErrorActionPreference = 'Stop'

# OFFSCREEN THROUGHOUT. This case is about process arbitration, not about pixels, and a real window
# would make it a nuisance to run on a desktop somebody is using.
$env:QT_QPA_PLATFORM = 'offscreen'

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Host "A-87: no such executable: $Exe"
    exit 1
}
$exePath = (Resolve-Path -LiteralPath $Exe).Path

$started = $null
$failures = @()

function Test-Diagnostic {
    param([string] $Switch, [string] $Pattern, [string] $What)

    # `Continue` FOR THE DURATION OF THE CALL. `rawpage.exe` logs to stderr, and under
    # `$ErrorActionPreference = 'Stop'` PowerShell promotes a native command's stderr into a
    # terminating NativeCommandError -- so a script asserting on stdout would die on the log line
    # before it ever read the answer.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $exePath $Switch 2>$null | Out-String
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($output -match $Pattern) {
        Write-Host "  PASS  $Switch still prints $What with a primary running"
        return $true
    }

    Write-Host "  FAIL  $Switch printed no $What -- $($output.Length) characters of output"
    return $false
}

try {
    # --- A primary, adopted or launched ---------------------------------------------------------
    $existing = @(Get-Process -Name 'rawpage' -ErrorAction SilentlyContinue)
    if ($existing.Count -gt 0) {
        Write-Host "A-87: adopting the RawPage already running (pid $($existing[0].Id)) as the primary"
    } else {
        $started = Start-Process -FilePath $exePath -PassThru
        Start-Sleep -Seconds $SettleSeconds
        if ($started.HasExited) {
            Write-Host "A-87: the primary exited immediately (code $($started.ExitCode)); nothing to test"
            exit 1
        }
        Write-Host "A-87: started a primary (pid $($started.Id))"
    }

    # --- The control, before any assertion ------------------------------------------------------
    # A third process must forward and exit. That exit is the only evidence that the bus is actually
    # held; without it every assertion below would pass on a build with no single-instance code at
    # all, and on a machine whose `general/singleInstance` is false.
    $probe = Start-Process -FilePath $exePath -PassThru
    Start-Sleep -Seconds $SettleSeconds
    if (-not $probe.HasExited) {
        Stop-Process -Id $probe.Id -ErrorAction SilentlyContinue
        Write-Host "A-87: a second GUI process did NOT forward and exit, so the bus is not claimed"
        Write-Host "      (general/singleInstance is probably false). This case measures nothing here."
        exit 77
    }
    Write-Host "  PASS  the control: a second GUI process forwarded and exited -- the bus IS claimed"

    # --- The three switches ---------------------------------------------------------------------
    # One line each, chosen to be the FIRST thing the switch prints, so a truncated or empty answer
    # cannot satisfy it.
    if (-not (Test-Diagnostic '--print-screen-metrics' '(?m)^screen\s' 'screen row')) {
        $failures += '--print-screen-metrics'
    }
    if (-not (Test-Diagnostic '--dump-ui-strings' '(?m)^# locale\s' 'locale header')) {
        $failures += '--dump-ui-strings'
    }
    if (-not (Test-Diagnostic '--list-actions' '(?m)^# actions\s' 'action census')) {
        $failures += '--list-actions'
    }
} finally {
    # ONLY WHAT THIS SCRIPT STARTED.
    if ($null -ne $started -and -not $started.HasExited) {
        Stop-Process -Id $started.Id -ErrorAction SilentlyContinue
    }
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "A-87: FAIL -- $($failures -join ', ') printed nothing while a primary held the bus."
    Write-Host "      That is issue I-27: the switch claimed the bus, became a secondary, forwarded"
    Write-Host "      and returned 0 without composing its answer."
    exit 1
}

Write-Host ""
Write-Host "A-87: PASS"
exit 0
