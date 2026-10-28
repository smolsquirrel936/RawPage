<#
.SYNOPSIS
    Launches a built rawpage.exe from a terminal and WAITS for it, so its log stays readable.

.DESCRIPTION
    Typed directly, `.\build\x64-release\bin\rawpage.exe` is a GUI-subsystem program, and PowerShell does
    not wait for one: the prompt comes back at once while the product goes on writing its log into the same
    console. PSReadLine still believes its prompt is where it drew it, so the next key -- Enter, Ctrl+C --
    moves the cursor back up to that line, and everything after overwrites the log in place, leaving the tails
    of longer lines behind (Stage 6 guide I-35, `Stage6-Annotations-Testing-Guide.md` section 8). This script
    holds the prompt until the product exits, so nothing redraws over the log.

        .\scripts\run.ps1 tests\manual\M-05\annots-threads.pdf
        .\scripts\run.ps1 -Preset x64-debug --lang zh-TW tests\manual\M-05\annots-threads.pdf

    Ctrl+C in the terminal reaches the product, which closes its windows as their own close buttons would
    (`app/ConsoleInterrupt.h`); a second Ctrl+C while the first is still waiting ends it at once. The prompt
    comes back when the product has exited.

    STARTED THROUGH ShellExecute, NOT WITH -NoNewWindow, and that is measured rather than taste (2026-10-22, a
    -mwindows probe): -NoNewWindow hands the child this console's handles, so `app/main.cpp` never calls
    AttachConsole and the child is not ON the console -- it prints, but a Ctrl+C never reaches it. Started
    without it, the child has no handles, attaches to this console as a typed launch does, and is one of the
    processes a Ctrl+C is delivered to. A GUI program opens no window of its own either way.

    Every argument after -Preset goes to rawpage.exe unchanged, quoted by the rules Windows splits a command
    line with, so a path holding a space arrives whole.

.PARAMETER Preset
    Which build to run: x64-release (the default) or x64-debug. Read from the arguments by hand rather than
    declared, so that the product's own switches (`--lang`, `--help`) pass through untouched.
#>

$ErrorActionPreference = 'Stop'

$preset = 'x64-release'
$passed = [System.Collections.Generic.List[string]]::new()
for ($i = 0; $i -lt $args.Count; ++$i) {
    if ($args[$i] -eq '-Preset' -and $i + 1 -lt $args.Count) { $preset = [string]$args[++$i]; continue }
    $passed.Add([string]$args[$i])
}

$repository = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repository "build\$preset\bin\rawpage.exe"
if (-not (Test-Path $exe)) {
    Write-Host "No rawpage.exe at $exe -- build the $preset preset first." -ForegroundColor Red
    exit 2
}

# CommandLineToArgvW's rules, which QCoreApplication::arguments() reads the command line by: backslashes are
# literal except before a quote, where each one doubles and the quote is escaped.
function ConvertTo-CommandLineArgument([string]$Argument) {
    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') { return $Argument }
    $escaped = $Argument -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}
$commandLine = ($passed | ForEach-Object { ConvertTo-CommandLineArgument $_ }) -join ' '

# The debug binaries need the Qt debug DLLs on PATH (CLAUDE.md, *Traps*: without them a debug binary exits
# 0xC0000135 and prints nothing). The child inherits this process's environment, so env.ps1 is dot-sourced
# here and every variable it may set is put back afterwards: running the product does not configure the shell.
$saved = @{}
foreach ($name in 'PATH', 'VCPKG_ROOT', 'RAWPAGE_QT_PREFIX', 'RAWPAGE_MINGW_ROOT') {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$process = $null
$code = 0
try {
    . (Join-Path $PSScriptRoot 'env.ps1') -Quiet

    $startArguments = @{
        FilePath         = $exe
        WorkingDirectory = (Get-Location).ProviderPath
        PassThru         = $true
    }
    if ($commandLine) { $startArguments.ArgumentList = $commandLine }
    $process = Start-Process @startArguments
    $null = $process.Handle   # held now, or ExitCode reads empty once the process has gone

    # A blocking .NET call, so a Ctrl+C here does not abandon the wait: the same Ctrl+C reaches the product,
    # which closes, and PowerShell acts on it only once the call returns -- below the product's last line.
    $process.WaitForExit()
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }

    # The exit code is reported HERE, not after the block: that same Ctrl+C reaches PowerShell too, which stops
    # this script the moment WaitForExit() returns, and only a finally still runs -- so a line after it never
    # printed the code of a forced end, 0xC000013A, the one end worth flagging (Stage 6 guide I-51).
    if ($process -and $process.HasExited) {
        $code = $process.ExitCode
        if ($code -ne 0) { Write-Host ("rawpage exited with code {0} (0x{0:X8})" -f $code) -ForegroundColor Yellow }
    }
}

exit $code
