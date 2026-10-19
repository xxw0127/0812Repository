$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$qt = "D:\Qt\5.15.2\mingw81_64"
$mingw = "D:\Qt\Tools\mingw810_64"
if (-not (Test-Path (Join-Path $qt "bin\qmake.exe"))) {
    throw "qmake not found under $qt"
}
if (-not (Test-Path (Join-Path $mingw "bin\mingw32-make.exe"))) {
    throw "mingw32-make not found under $mingw"
}
$env:PATH = "$qt\bin;$mingw\bin;$env:PATH"
Push-Location $root
try {
    & (Join-Path $qt "bin\qmake.exe") tests.pro
    if ($LASTEXITCODE -ne 0) { throw "qmake failed" }
    & (Join-Path $mingw "bin\mingw32-make.exe") -f Makefile.Release -j8
    if ($LASTEXITCODE -ne 0) { throw "make failed" }
    & ".\release\client_tests.exe" -o results.txt,txt
    $testExit = $LASTEXITCODE
    if (Test-Path "results.txt") {
        Get-Content "results.txt"
    }
    if ($testExit -ne 0) { throw "client tests failed" }
} finally {
    Pop-Location
}
