#Requires -Version 7.2
[CmdletBinding(DefaultParameterSetName='SignIn')]
param(
    [Parameter(Mandatory, ParameterSetName='SignIn')]
    [ValidateSet('3DS','PSP')]
    [string]$Console,
    [Parameter(Mandatory, ParameterSetName='SignIn')]
    [string]$CardRoot,
    [Parameter(ParameterSetName='SignIn')]
    [switch]$Replace,
    [Parameter(Mandatory, ParameterSetName='Test')]
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'

function ConvertTo-Base64Url([byte[]]$Bytes) {
    [Convert]::ToBase64String($Bytes).TrimEnd('=').Replace('+','-').Replace('/','_')
}

function Get-CodeChallenge([string]$Verifier) {
    ConvertTo-Base64Url ([Security.Cryptography.SHA256]::HashData(
        [Text.Encoding]::ASCII.GetBytes($Verifier)))
}

function Read-AuthorizationCallback([string]$Request, [uri]$Redirect, [string]$State) {
    $lines = $Request -split "\r\n"
    if ($lines[0] -cnotmatch '^GET (/[^ ]*) HTTP/1\.[01]$') { return }
    $target = $Matches[1]
    if ($lines -inotcontains "Host: $($Redirect.Authority)") { return }
    $parts = $target.Split('?', 2)
    if ($parts.Count -ne 2 -or $parts[0] -cne $Redirect.AbsolutePath) { return }
    $query = [System.Web.HttpUtility]::ParseQueryString($parts[1])
    $states = $query.GetValues('state')
    if ($states.Count -ne 1 -or $states[0] -cne $State) { return }
    $codes = $query.GetValues('code')
    if ($query['error'] -and !$codes) { return @{ Cancelled = $true } }
    if ($codes.Count -eq 1 -and $codes[0] -cmatch '^[A-Za-z0-9_]{1,2048}$') {
        return @{ Code = $codes[0] }
    }
}

function Receive-AuthorizationCode(
    [Net.Sockets.TcpListener]$Listener, [uri]$Redirect, [string]$State
) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalMinutes -lt 5) {
        if (!$Listener.Pending()) { Start-Sleep -Milliseconds 100; continue }
        $client = $Listener.AcceptTcpClient()
        try {
            $client.ReceiveTimeout = 1000
            $client.SendTimeout = 1000
            $stream = $client.GetStream()
            $request = [Text.StringBuilder]::new()
            $requestTimer = [Diagnostics.Stopwatch]::StartNew()
            $complete = $false
            while ($request.Length -lt 8192 -and $requestTimer.Elapsed.TotalSeconds -lt 2) {
                $byte = $stream.ReadByte()
                if ($byte -lt 0) { break }
                [void]$request.Append([char]$byte)
                $n = $request.Length
                if ($n -ge 4 -and $request.ToString($n-4,4) -ceq "`r`n`r`n") {
                    $complete = $true
                    break
                }
            }
            $callback = if ($complete) { Read-AuthorizationCallback $request.ToString() $Redirect $State }
            $status = if ($callback) { '200 OK' } else { '400 Bad Request' }
            $message = if ($callback) { 'Return to Lichess Handheld Setup.' } else { 'Invalid callback.' }
            $response = "HTTP/1.1 $status`r`nContent-Type: text/plain`r`nCache-Control: no-store`r`nConnection: close`r`nContent-Length: $($message.Length)`r`n`r`n$message"
            $bytes = [Text.Encoding]::ASCII.GetBytes($response)
            try { $stream.Write($bytes,0,$bytes.Length) } catch [IO.IOException] {}
            if ($callback.Cancelled) { throw 'Lichess authorization was cancelled.' }
            if ($callback.Code) { return $callback.Code }
        } catch [IO.IOException] {
            # A browser can close an unused connection before sending a request.
        } finally {
            $client.Dispose()
        }
    }
    throw 'Lichess sign-in timed out. Run the helper again to retry.'
}

if ($SelfTest) {
    $challenge = Get-CodeChallenge 'dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk'
    if ($challenge -cne 'E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM') {
        throw 'PKCE SHA-256 test failed.'
    }
    $redirect = [uri]'http://127.0.0.1:12345/callback'
    $headers = " HTTP/1.1`r`nHost: 127.0.0.1:12345`r`n`r`n"
    $ok = Read-AuthorizationCallback ("GET /callback?state=expected&code=sample" + $headers) $redirect 'expected'
    $wrong = Read-AuthorizationCallback ("GET /callback?state=wrong&code=sample" + $headers) $redirect 'expected'
    $duplicate = Read-AuthorizationCallback ("GET /callback?state=expected&state=expected&code=sample" + $headers) $redirect 'expected'
    $denied = Read-AuthorizationCallback ("GET /callback?state=expected&error=access_denied" + $headers) $redirect 'expected'
    if ($ok.Code -cne 'sample' -or $wrong -or $duplicate -or !$denied.Cancelled) {
        throw 'OAuth callback test failed.'
    }
    Write-Output 'PASS PKCE challenge, callback validation, and cancellation'
    return
}

$card = Get-Item -LiteralPath $CardRoot
if (!$card.PSIsContainer -or $card.PSProvider.Name -ne 'FileSystem') {
    throw 'CardRoot must be a mounted card directory.'
}
$relativePath = if ($Console -eq '3DS') { '3ds/HandheldChess' } else { 'PSP/GAME/HandheldChess' }
$directory = Join-Path $card.FullName $relativePath
$tokenPath = Join-Path $directory 'lichess-token.txt'
if ((Test-Path -LiteralPath $tokenPath) -and !$Replace) {
    throw 'This card already has a credential. Pass -Replace to overwrite it.'
}

$clientId = 'handheld-chess-' + $Console.ToLowerInvariant()
$verifier = ConvertTo-Base64Url ([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$state = ConvertTo-Base64Url ([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
$listener.Start()
try {
    $port = $listener.LocalEndpoint.Port
    $redirect = "http://127.0.0.1:$port/callback"
    $params = [ordered]@{
        response_type = 'code'
        client_id = $clientId
        redirect_uri = $redirect
        scope = 'board:play follow:read follow:write'
        code_challenge_method = 'S256'
        code_challenge = Get-CodeChallenge $verifier
        state = $state
    }
    $query = ($params.GetEnumerator() | ForEach-Object {
        $_.Key + '=' + [uri]::EscapeDataString($_.Value)
    }) -join '&'
    Write-Output "Complete sign-in in your browser for the $Console card at $($card.FullName)."
    Start-Process "https://lichess.org/oauth?$query"
    $code = Receive-AuthorizationCode $listener $redirect $state
} finally {
    $listener.Stop()
}

try {
    $response = Invoke-RestMethod -Method Post -Uri 'https://lichess.org/api/token' -TimeoutSec 20 -MaximumRedirection 0 `
        -ContentType 'application/x-www-form-urlencoded' -Body @{
            grant_type='authorization_code'; code=$code; code_verifier=$verifier
            client_id=$clientId; redirect_uri=$redirect
        }
    $token = $response.access_token
    if ($token -cnotmatch '^[A-Za-z0-9_]{1,2048}$') { throw 'Unexpected token format.' }
    $account = Invoke-RestMethod -Uri 'https://lichess.org/api/account' -TimeoutSec 20 -MaximumRedirection 0 `
        -Headers @{ Authorization = "Bearer $token" }
    if (!$account.username) { throw 'Missing account name.' }
} catch {
    throw 'Could not exchange the Lichess code or verify the account. Run the helper again to retry.'
}

[void][IO.Directory]::CreateDirectory($directory)
$stagingPath = Join-Path $directory ("lichess-token-" + [guid]::NewGuid().ToString('N') + ".tmp")
try {
    [IO.File]::WriteAllText($stagingPath,$token,[Text.Encoding]::ASCII)
    [IO.File]::Move($stagingPath,$tokenPath,[bool]$Replace)
} finally {
    if ([IO.File]::Exists($stagingPath)) { [IO.File]::Delete($stagingPath) }
}
Write-Output "Saved the $Console credential for $($account.username)."
