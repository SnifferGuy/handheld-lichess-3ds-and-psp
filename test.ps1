param([string]$Zig = (Join-Path $PSScriptRoot '..\.local\tools\zig\zig-x86_64-windows-0.15.2\zig.exe'),
    [switch]$Capture)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Zig)) { throw 'Pass -Zig with the path to a Zig 0.15 compiler' }
$build = Join-Path $PSScriptRoot 'build\desktop'
New-Item -ItemType Directory -Force $build | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $build 'global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $build 'local-cache'
$env:TEMP = $build
$env:TMP = $build
$env:HANDHELD_TEST_DIR = $build
$env:HANDHELD_CAPTURE_DIR = if ($Capture) {
    (New-Item -ItemType Directory -Force (Join-Path $build 'screens')).FullName
} else { '' }
$files = 'chess','ai','match','lan','screen','lichess_account','online_extra','app','tests' | ForEach-Object { Join-Path $PSScriptRoot "$_.cpp" }
$exe = Join-Path $build 'chess-tests.exe'
& $Zig c++ -std=c++17 -O2 -Wall -Wextra -Werror @files -lws2_32 -o $exe
if ($LASTEXITCODE) { throw 'Desktop build failed' }
& $exe
if ($LASTEXITCODE) { throw 'Desktop tests failed' }
