param([string]$Sdk=(Join-Path $PSScriptRoot 'build/pspdev-win/sdk'))
$ErrorActionPreference='Stop'
$tar=Join-Path $env:SystemRoot 'System32/tar.exe'
$packages=@(
    @('curl-7.64.1-13-any.pkg.tar.gz','95a73b4693786bb0c0dd53fff2e1da98a457a6dcdb27011e0ebc4225a8634ed0'),
    @('mbedtls-2.28.10-2-any.pkg.tar.gz','0ffa576c1604bd91163bbc1c94494d72d21d3fba5417efe2aefec5f90bb235e7'),
    @('zlib-1.3.1-8-any.pkg.tar.gz','738ce52578c2859e0980518b689ffb068268db2c60e2c2965c2aa2042331a0b4'))
$Sdk=(Resolve-Path -LiteralPath $Sdk).Path
$cache=Join-Path $PSScriptRoot 'build/https-packages'
New-Item -ItemType Directory -Force $cache | Out-Null
foreach($package in $packages) {
    $archive=Join-Path $cache $package[0]
    if(!(Test-Path -LiteralPath $archive)) { Invoke-WebRequest ('https://pspdev.github.io/psp-packages/'+$package[0]) -OutFile $archive }
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package[1]) { throw "Checksum mismatch: $($package[0])" }
    $members=@(& $tar -tzf $archive)
    if($LASTEXITCODE) { throw 'Cannot inspect package' }
    foreach($member in $members) {
        if($member -match '(^/|^[A-Za-z]:|(^|/)\.\.(/|$)|\\)') { throw 'Unsafe archive member' }
    }
    & $tar -xzf $archive -C $Sdk psp
    if($LASTEXITCODE) { throw 'Package extraction failed' }
}
Write-Output 'Installed verified PSPDEV HTTPS packages into the local SDK.'
