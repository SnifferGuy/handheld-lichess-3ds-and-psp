param(
    [string]$Sdk = (Join-Path $PSScriptRoot 'build\pspdev-win\sdk'),
    [string]$Bash = 'C:\devkitPro\msys2\usr\bin\bash.exe',
    [switch]$LichessHttps
)
$ErrorActionPreference = 'Stop'
$Sdk = (Resolve-Path -LiteralPath $Sdk).Path
if (!(Test-Path -LiteralPath (Join-Path $Sdk 'bin\psp-g++.exe')) -or
    !(Test-Path -LiteralPath (Join-Path $Sdk 'psp\sdk\lib\build.mak'))) {
    throw 'Pass -Sdk with an extracted portable Windows PSPDEV toolchain'
}
if (!(Test-Path -LiteralPath $Bash)) { throw 'Pass -Bash with the path to an MSYS2 bash.exe' }
# GCC specs require a space-free include and library prefix.
$aliasPath = Join-Path ([IO.Path]::GetTempPath()) ("handheld-pspdev-" + [guid]::NewGuid().ToString('N'))
if ($aliasPath -match '\s') { throw 'The portable compiler requires a space-free TEMP path' }
New-Item -ItemType Junction -Path $aliasPath -Target $Sdk | Out-Null
try {
    $oldCurl=$env:HANDHELD_CURL
    if($LichessHttps) { $env:HANDHELD_CURL='1' }
    & $Bash -lc 'set -e; export PSPDEV="$(cygpath -m "$2")"; export PSPSDK="$PSPDEV/psp/sdk"; export PATH="$(cygpath -u "$2")/bin:$PATH"; bash "$(cygpath -u "$1")/build-psp.sh"' handheld-chess $PSScriptRoot $aliasPath
    if ($LASTEXITCODE) { throw 'Portable PSP build failed' }
} finally {
    $env:HANDHELD_CURL=$oldCurl
    [IO.Directory]::Delete($aliasPath)
}
