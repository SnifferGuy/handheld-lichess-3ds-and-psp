param([switch]$Build,[switch]$Verify)
$ErrorActionPreference='Stop'
if($Verify) {
    & (Join-Path $PSScriptRoot 'test.ps1') -Capture
    & (Join-Path $PSScriptRoot 'lichess-sign-in.ps1') -SelfTest
}
if($Build) {
    & (Join-Path $PSScriptRoot 'build-3ds.ps1') -LichessHttps
    & (Join-Path $PSScriptRoot 'build-psp-windows.ps1') -LichessHttps
}
$dist=Join-Path $PSScriptRoot 'dist'
$stage=Join-Path $dist ('release-'+[guid]::NewGuid().ToString('N'))
$ctr=Join-Path $stage '3DS/3ds/HandheldChess'
$psp=Join-Path $stage 'PSP/PSP/GAME/HandheldChess'
$source=Join-Path $stage 'source'
New-Item -ItemType Directory -Force $ctr,$psp,$source | Out-Null
Copy-Item -LiteralPath (Join-Path $dist 'HandheldChess.3dsx') -Destination $ctr
Copy-Item -LiteralPath (Join-Path $dist 'EBOOT.PBP') -Destination $psp
foreach($destination in $ctr,$psp) {
    Copy-Item -LiteralPath (Join-Path $dist 'cacert.pem') -Destination $destination
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $destination -Recurse
    foreach($asset in 'assets/cburnett/GPL-2.0.txt','assets/cburnett/UPSTREAM-COPYING.md','assets/NotoSans-OFL.txt','assets/README.md') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $asset) -Destination (Join-Path $destination 'licenses')
    }
}
foreach($name in 'LICENSE','LICENSE-NOTICE.md','RELEASE.md','TEST-REPORT.md','DEPENDENCIES.md','lichess-sign-in.ps1') {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $stage
}
foreach($name in '3ds-toolchain.txt','psp-toolchain.txt') { Copy-Item -LiteralPath (Join-Path $dist $name) -Destination $stage }
Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in '.cpp','.hpp','.ps1','.sh','.py','.md' -or $_.Name -eq 'Makefile.psp' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $source
}
foreach($name in 'LICENSE','.gitignore') { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $source }
foreach($name in 'assets','licenses') { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $source -Recurse }
$manifest=Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/')
}
$manifest | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding utf8
$zip=Join-Path $dist 'LichessHandheld-release-candidate.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
Write-Output "Packaged $zip"
