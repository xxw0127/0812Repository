param(
    [string]$Version = "0.2.0",
    [string]$QtDir = "D:\Qt\5.15.2\mingw81_64",
    [string]$MingwDir = "D:\Qt\Tools\mingw810_64",
    [string]$OpenSSLDir = "C:\Program Files\OpenSSL-Win64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $root "build_client.ps1") -QtDir $QtDir -MingwDir $MingwDir -OpenSSLDir $OpenSSLDir
if ($LASTEXITCODE -ne 0) { throw "build_client.ps1 failed" }

$release = Join-Path $root "build\release"
$distRoot = Join-Path $root "dist"
$name = "chatroom-client-$Version"
$target = Join-Path $distRoot $name
if (Test-Path $target) { Remove-Item $target -Recurse -Force }
New-Item -ItemType Directory -Force -Path $target | Out-Null

Copy-Item (Join-Path $release "chatroom_client.exe") $target -Force
Copy-Item (Join-Path $release "*.dll") $target -Force
foreach ($directory in @("certs", "platforms", "styles")) {
    $source = Join-Path $release $directory
    if (Test-Path $source) {
        Copy-Item $source $target -Recurse -Force
    }
}
Copy-Item (Join-Path $root "README.md") $target -Force

$zip = Join-Path $distRoot "$name-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $target -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Package complete: $zip"
