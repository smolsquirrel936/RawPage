<#
.SYNOPSIS
    Fetches the pinned prebuilt PDFium and prepares it for the MinGW toolchain.

.DESCRIPTION
    Run from the repository root:

        .\scripts\fetch-pdfium.ps1

    PDFium does not come from vcpkg -- there is no port -- so this script is the acquisition
    step that risk A6 asked us to document (Tech-Stack-Plan.md 3.3). It is idempotent: an
    already-prepared tree is left alone unless -Force is given.

    THE PIN IS A HASH, NOT A TAG. A release asset can be replaced; a SHA-256 cannot. If the
    download stops matching, the script refuses rather than carrying on, because "the binary
    that goes into every shipped copy changed and nobody noticed" is not a recoverable state.

    THE IMPORT LIBRARY IS GENERATED, NOT SHIPPED. The upstream archive carries
    lib/pdfium.dll.lib, which is MSVC format. gendef reads the DLL export table and dlltool
    turns it into libpdfium.dll.a for GNU ld -- both from C:\Qt\Tools, which is why
    Tech-Stack-Plan.md 6.3 lists those two binutils as load-bearing rather than incidental.
#>
[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# ---- The pin -------------------------------------------------------------------------
$Tag          = 'chromium/8009'
$Asset        = 'pdfium-win-x64.tgz'     # NOT the -v8- variant; see the note below
$ExpectedHash = 'c78a8cd51b48abafcb266d868e401afefda1d189aa01ebbe743dc1d144e06031'
$PdfiumVersion = '153.0.8009.0'

# The v8 build is 12.5 MB against 3.7 MB and carries a JavaScript engine. RawPage uses PDFium
# for READ AND RASTER ONLY, so V8 would be ~9 MB and a large attack surface for an entry
# point we never call. If PDF form JavaScript is ever wanted (Features.md 9.1 reserves a
# javascript/ settings group), that is a decision for Stage 9 with its own security review,
# not a default acquired by accident here.

$repoRoot = Split-Path -Parent $PSScriptRoot
$target   = Join-Path $repoRoot 'third_party\pdfium'
$download = Join-Path $target 'download'
$archive  = Join-Path $download $Asset

if ((Test-Path (Join-Path $target 'lib\libpdfium.dll.a')) -and -not $Force) {
    Write-Host "PDFium $PdfiumVersion already prepared at $target (use -Force to redo)." -ForegroundColor Green
    return
}

New-Item -ItemType Directory -Force $download | Out-Null

if (-not (Test-Path $archive)) {
    $url = "https://github.com/bblanchon/pdfium-binaries/releases/download/$Tag/$Asset"
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
}

$actualHash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLower()
if ($actualHash -ne $ExpectedHash) {
    Remove-Item $archive -Force
    throw @"
PDFium archive hash mismatch -- refusing to use it.
  expected $ExpectedHash
  actual   $actualHash
The asset for tag '$Tag' is not the one this project was verified against. Do not "fix" this
by updating the hash: find out why it changed first. Risk A6 was answered against the exact
bytes above.
"@
}
Write-Host "hash verified: $actualHash" -ForegroundColor Green

Push-Location $target
try {
    tar -xzf (Join-Path 'download' $Asset)

    if (-not (Test-Path 'bin\pdfium.dll')) { throw "archive did not contain bin/pdfium.dll" }

    # MSVC-format import library in, GNU-format out.
    Write-Host "generating the MinGW import library"
    & gendef 'bin\pdfium.dll' | Out-Null
    if (-not (Test-Path 'pdfium.def')) { throw "gendef produced no .def -- is C:\Qt\Tools\mingw1310_64\bin on PATH?" }
    & dlltool -d 'pdfium.def' -l 'lib\libpdfium.dll.a'
    if (-not (Test-Path 'lib\libpdfium.dll.a')) { throw "dlltool produced no import library" }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "PDFium $PdfiumVersion ready:" -ForegroundColor Green
Write-Host "  headers      $target\include"
Write-Host "  import lib   $target\lib\libpdfium.dll.a"
Write-Host "  runtime DLL  $target\bin\pdfium.dll   (must sit beside rawpage.exe, or be on PATH)"
