param([string]$DevkitProRoot = 'C:\devkitPro', [switch]$LichessHttps)
$ErrorActionPreference = 'Stop'
$tool = Join-Path $DevkitProRoot 'devkitARM\bin\arm-none-eabi-g++.exe'
$pack = Join-Path $DevkitProRoot 'tools\bin\3dsxtool.exe'
$smdhTool = Join-Path $DevkitProRoot 'tools\bin\smdhtool.exe'
$ctr = Join-Path $DevkitProRoot 'libctru'
foreach ($path in $tool,$pack,$smdhTool) {
    if (!(Test-Path -LiteralPath $path)) { throw "Required devkitPro tool missing: $path" }
}
$build = Join-Path $PSScriptRoot 'build\3ds'
$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force $build,$dist | Out-Null
$arch = @('-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft')
$onlineCompile = @()
$onlineLink = @()
if ($LichessHttps) {
    $portlibs = Join-Path $DevkitProRoot 'portlibs\3ds'
    foreach ($library in 'curl','mbedtls','mbedx509','mbedcrypto','z') {
        if (!(Test-Path -LiteralPath "$portlibs\lib\lib$library.a")) {
            throw 'Install 3ds-curl, 3ds-mbedtls, and 3ds-zlib to enable Lichess HTTPS.'
        }
    }
    $onlineCompile = @('-DHANDHELD_CURL',"-I$portlibs\include")
    $onlineLink = @("-L$portlibs\lib",'-lcurl','-lmbedtls','-lmbedx509','-lmbedcrypto','-lz')
}
$objects = foreach ($name in 'chess','ai','match','lan','screen','lichess_account','online_extra','app','platform_3ds') {
    $source = Join-Path $PSScriptRoot "$name.cpp"
    $obj = Join-Path $build "$name.o"
    & $tool @arch -O2 -std=gnu++17 -Wall -Wextra -Werror -mword-relocations -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -D__3DS__ "-I$ctr\include" @onlineCompile -c $source -o $obj
    if ($LASTEXITCODE) { throw "3DS compilation failed: $name" }
    $obj
}
$elf = Join-Path $build 'HandheldChess.elf'
$smdh = Join-Path $build 'HandheldChess.smdh'
$artifact = Join-Path $dist 'HandheldChess.3dsx'
& $tool @arch '-specs=3dsx.specs' '-Wl,--gc-sections' @objects "-L$ctr\lib" @onlineLink -lcitro2d -lcitro3d -lctru -lm -o $elf
if ($LASTEXITCODE) { throw '3DS link failed' }
& $smdhTool --create 'Lichess Handheld' 'Online, LAN and offline chess' 'Handheld homebrew project' "$ctr\default_icon.png" $smdh
if ($LASTEXITCODE) { throw '3DS metadata generation failed' }
& $pack $elf $artifact "--smdh=$smdh"
if ($LASTEXITCODE) { throw '3DS packaging failed' }
& $tool --version | Set-Content -LiteralPath (Join-Path $dist '3ds-toolchain.txt')
Write-Output "Built $artifact"
