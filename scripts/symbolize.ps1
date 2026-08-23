<#
.SYNOPSIS
    Resolves the faulting frame of a RawPage minidump to a function name and source line.

.DESCRIPTION
    Decision D1-13 in claude-plan/Stage1-AppShell-Plan.md 3.

    This toolchain emits DWARF and never a .pdb (Tech-Stack-Plan.md 6.3, risk R7), so there is
    no symbol server and no WinDbg path: a field dump is symbolized against an ARCHIVED
    UNSTRIPPED BINARY, matched by build id, and read with the binutils that ship in the Qt
    install (scripts\env.ps1 puts them on PATH).

    The build id on this toolchain is a PE CodeView RSDS record -- a 16-byte GUID plus a
    4-byte age, rendered here as 33 uppercase hex characters. -Wl,--build-id puts it in the
    binary's `.buildid` section, and MiniDumpWriteDump copies the same record into the dump's
    module list. So a dump identifies the exact binary that produced it, with no side table,
    and this script REFUSES to symbolize against a mismatched binary rather than printing
    plausible nonsense from the wrong build -- which is the failure mode that makes a crash
    report worse than no crash report.

    What it does:
      1. reads the dump's module list, taking the faulting module's base address and build id;
      2. reads the same build id out of the candidate binary and compares them;
      3. converts the faulting address to a file offset;
      4. hands that to addr2line, which reads the DWARF.

.PARAMETER Dump
    Path to a .dmp written by app/CrashHandler. %LOCALAPPDATA%\RawPage\crashes\ by default.

.PARAMETER Binary
    The archived unstripped rawpage.exe to symbolize against. Defaults to the build tree's
    build\x64-release\bin\rawpage.exe, which is the right answer while developing and the wrong
    one for a dump from a user -- hence the build-id check.

.PARAMETER BuildId
    Expected build id, 33 hex characters. Optional: it is read from the dump when omitted.
    Supply it to assert that a dump came from the build you think it did.

.PARAMETER Force
    Symbolize even when the build ids differ. Prints every frame with a MISMATCH marker. For
    "I only have the stripped release and want an address" -- never for a report you will act on.

.EXAMPLE
    . .\scripts\env.ps1
    .\scripts\symbolize.ps1 -Dump "$env:LOCALAPPDATA\RawPage\crashes\rawpage-20260823-141502-018236.dmp"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Dump,

    [string]$Binary,

    [string]$BuildId,

    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# --- Prerequisites ----------------------------------------------------------
foreach ($tool in @('addr2line', 'objdump')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool is not on PATH. Dot-source scripts\env.ps1 first: the binutils come from the Qt install."
    }
}

if (-not (Test-Path -LiteralPath $Dump)) { throw "No such dump: $Dump" }

if (-not $Binary) {
    $Binary = Join-Path $PSScriptRoot '..\build\x64-release\bin\rawpage.exe'
}
$Binary = (Resolve-Path -LiteralPath $Binary -ErrorAction SilentlyContinue).Path
if (-not $Binary) { throw "No such binary: $Binary -- pass -Binary <archived rawpage.exe>" }

# --- The binary's build id --------------------------------------------------
# objdump prints `.buildid` as a hex dump; the RSDS record sits inside it. Parsing the section
# bytes is more robust than scraping objdump's own debug-directory rendering, which differs
# between binutils versions.
function Get-BuildIdFromBinary {
    param([string]$Path)

    $lines = & objdump -s -j .buildid $Path 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $lines) { return $null }

    $bytes = New-Object System.Collections.Generic.List[byte]
    foreach ($line in $lines) {
        if ($line -notmatch '^\s+[0-9a-f]+\s+((?:[0-9a-f]{2,8}\s+){1,4})') { continue }
        foreach ($word in ($matches[1] -split '\s+' | Where-Object { $_ })) {
            for ($i = 0; $i -lt $word.Length; $i += 2) {
                $bytes.Add([Convert]::ToByte($word.Substring($i, 2), 16))
            }
        }
    }

    # Find "RSDS", then GUID (little-endian Data1/2/3, big-endian Data4) + 4-byte age.
    for ($i = 0; $i -le $bytes.Count - 24; $i++) {
        if ($bytes[$i]     -ne 0x52) { continue }   # R
        if ($bytes[$i + 1] -ne 0x53) { continue }   # S
        if ($bytes[$i + 2] -ne 0x44) { continue }   # D
        if ($bytes[$i + 3] -ne 0x53) { continue }   # S

        $guid = [byte[]]$bytes.GetRange($i + 4, 16)
        $age  = [BitConverter]::ToUInt32([byte[]]$bytes.GetRange($i + 20, 4), 0)
        return (Format-BuildId -Guid $guid -Age $age)
    }
    return $null
}

# The 33-character rendering CrashHandler::buildId() produces, so the two can be compared as
# strings anywhere -- log line, sidecar, dump, binary.
function Format-BuildId {
    param([byte[]]$Guid, [uint32]$Age)

    $d1 = [BitConverter]::ToUInt32($Guid, 0)
    $d2 = [BitConverter]::ToUInt16($Guid, 4)
    $d3 = [BitConverter]::ToUInt16($Guid, 6)
    $tail = ($Guid[8..15] | ForEach-Object { $_.ToString('X2') }) -join ''
    return ('{0:X8}{1:X4}{2:X4}{3}{4:X}' -f $d1, $d2, $d3, $tail, $Age)
}

# --- The dump ---------------------------------------------------------------
# A minidump is a small, well-documented container: a header with a stream directory, and the
# two streams we need are ModuleList (type 4) and Exception (type 6). Read directly rather
# than through dbghelp, so this script works from any PowerShell with no SDK installed --
# which is the situation a support engineer is actually in.
function Read-Minidump {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 32) { throw "Truncated dump: $Path" }
    if ([BitConverter]::ToUInt32($bytes, 0) -ne 0x504D444D) {   # 'MDMP'
        throw "Not a minidump (no MDMP signature): $Path"
    }

    $streamCount = [BitConverter]::ToUInt32($bytes, 8)
    $streamRva   = [BitConverter]::ToUInt32($bytes, 12)

    $result = [ordered]@{ Modules = @(); Exception = $null }

    for ($i = 0; $i -lt $streamCount; $i++) {
        $entry = $streamRva + ($i * 12)
        $type  = [BitConverter]::ToUInt32($bytes, $entry)
        $rva   = [BitConverter]::ToUInt32($bytes, $entry + 8)

        switch ($type) {
            4 {   # ModuleListStream
                $count = [BitConverter]::ToUInt32($bytes, $rva)
                for ($m = 0; $m -lt $count; $m++) {
                    # MINIDUMP_MODULE, 4-byte packed, 108 bytes:
                    #   0  ULONG64 BaseOfImage        20 RVA     ModuleNameRva
                    #   8  ULONG32 SizeOfImage        24 VS_FIXEDFILEINFO (13 DWORDs = 52)
                    #  12  ULONG32 CheckSum           76 MINIDUMP_LOCATION_DESCRIPTOR CvRecord
                    #  16  ULONG32 TimeDateStamp      84 MiscRecord, 92/100 Reserved
                    $mod        = $rva + 4 + ($m * 108)
                    $base       = [BitConverter]::ToUInt64($bytes, $mod)
                    $size       = [BitConverter]::ToUInt32($bytes, $mod + 8)
                    $nameRva    = [BitConverter]::ToUInt32($bytes, $mod + 20)
                    $cvSize     = [BitConverter]::ToUInt32($bytes, $mod + 76)
                    $cvRva      = [BitConverter]::ToUInt32($bytes, $mod + 80)

                    $nameLength = [BitConverter]::ToUInt32($bytes, $nameRva)
                    $name = [System.Text.Encoding]::Unicode.GetString($bytes, $nameRva + 4, $nameLength)

                    $id = $null
                    if ($cvSize -ge 24 -and
                        $bytes[$cvRva] -eq 0x52 -and $bytes[$cvRva + 1] -eq 0x53 -and
                        $bytes[$cvRva + 2] -eq 0x44 -and $bytes[$cvRva + 3] -eq 0x53) {
                        $guid = $bytes[($cvRva + 4)..($cvRva + 19)]
                        $age  = [BitConverter]::ToUInt32($bytes, $cvRva + 20)
                        $id = Format-BuildId -Guid $guid -Age $age
                    }

                    $result.Modules += [pscustomobject]@{
                        Name = $name; Base = $base; Size = $size; BuildId = $id
                    }
                }
            }
            6 {   # ExceptionStream
                $record = $rva + 8      # past ThreadId + alignment
                $result.Exception = [pscustomobject]@{
                    Code    = [BitConverter]::ToUInt32($bytes, $record)
                    Address = [BitConverter]::ToUInt64($bytes, $record + 16)
                }
            }
        }
    }
    return [pscustomobject]$result
}

$dumpInfo = Read-Minidump -Path $Dump

if (-not $dumpInfo.Exception) {
    throw "The dump has no exception stream, so there is no faulting frame to resolve. Was it written by app/CrashHandler?"
}

$faultAddress = $dumpInfo.Exception.Address
$module = $dumpInfo.Modules |
    Where-Object { $faultAddress -ge $_.Base -and $faultAddress -lt ($_.Base + $_.Size) } |
    Select-Object -First 1

Write-Host ''
Write-Host 'RawPage minidump' -ForegroundColor Cyan
Write-Host ("  dump             {0}" -f $Dump)
Write-Host ("  exception code   0x{0:X8}" -f $dumpInfo.Exception.Code)
Write-Host ("  fault address    0x{0:X16}" -f $faultAddress)
Write-Host ("  faulting module  {0}" -f $(if ($module) { $module.Name } else { '(outside every loaded module)' }))

if (-not $module) {
    throw "The faulting address is in no loaded module -- a corrupt stack or a jump through a bad pointer. Nothing to symbolize."
}

# --- Build-id agreement ------------------------------------------------------
$binaryId = Get-BuildIdFromBinary -Path $Binary
$dumpId   = if ($BuildId) { $BuildId.ToUpperInvariant() } else { $module.BuildId }

Write-Host ("  build id (dump)  {0}" -f $(if ($dumpId)   { $dumpId }   else { '(absent)' }))
Write-Host ("  build id (exe)   {0}" -f $(if ($binaryId) { $binaryId } else { '(absent)' }))
Write-Host ''

if (-not $binaryId) {
    throw @"
$Binary carries no build id, so it cannot be matched to this dump.
That means -Wl,--build-id was lost from the link (cmake/RawPageWarnings.cmake, D1-13).
This is a finding about the BUILD, not about the dump.
"@
}

$matched = ($dumpId -and $binaryId -eq $dumpId)
if (-not $matched -and -not $Force) {
    throw @"
Build ids differ: the dump did not come from this binary.
Symbolizing anyway would print function names from the wrong build -- plausible, confident and
wrong, which is worse than no answer. Find the archived rawpage.exe with build id $dumpId,
or re-run with -Force if you only want raw offsets.
"@
}

# --- addr2line ---------------------------------------------------------------
# addr2line wants a VIRTUAL address in the binary's own preferred image space, not a file
# offset: subtract the module's load base and add the binary's ImageBase from its PE header.
function Get-ImageBase {
    param([string]$Path)
    $private = & objdump -p $Path
    foreach ($line in $private) {
        if ($line -match '^ImageBase\s+([0-9a-fA-F]+)') {
            return [Convert]::ToUInt64($matches[1], 16)
        }
    }
    throw "Could not read ImageBase from $Path"
}

$imageBase = Get-ImageBase -Path $Binary
$staticAddress = $faultAddress - $module.Base + $imageBase

$resolved = & addr2line -e $Binary -f -C -i ("0x{0:X}" -f $staticAddress)

Write-Host 'Faulting frame' -ForegroundColor Cyan
if (-not $matched) { Write-Host '  *** MISMATCH -- these names come from a different build ***' -ForegroundColor Red }

# addr2line -f -i emits alternating function / location lines, innermost first.
for ($i = 0; $i -lt $resolved.Count; $i += 2) {
    $name     = $resolved[$i]
    $location = if ($i + 1 -lt $resolved.Count) { $resolved[$i + 1] } else { '??:0' }
    Write-Host ("  {0}" -f $name) -ForegroundColor Yellow
    Write-Host ("      at {0}" -f $location)
}

if ($resolved -and $resolved[0] -eq '??') {
    Write-Warning @"
addr2line resolved nothing. Either the binary was stripped -- symbolize against the ARCHIVED
UNSTRIPPED one, which is the whole reason it is archived -- or the fault is in a Qt or system
DLL, whose DWARF we do not have.
"@
    exit 1
}

Write-Host ''
