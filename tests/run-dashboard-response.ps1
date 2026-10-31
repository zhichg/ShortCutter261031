$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Get-ShortCutterTestCompiler
Push-Location $projectDir
try {
    & $compiler -static -Os -Wall -Wextra -o tests/dashboard_response.exe `
        tests/dashboard_response.c -lcomctl32 -lgdi32 -luser32 -lkernel32 `
        -lshell32 -limm32 -lole32 -loleaut32 -luuid -lcomdlg32
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard test compilation failed.' }
    & .\tests\dashboard_response.exe
    if ($LASTEXITCODE -ne 0) { throw 'Slow icon response test failed.' }
    & .\tests\dashboard_response.exe --real-icons
    if ($LASTEXITCODE -ne 0) { throw 'Real Shell icon test failed.' }
} finally {
    $testBinary = Join-Path $PSScriptRoot 'dashboard_response.exe'
    if (Test-Path -LiteralPath $testBinary) {
        Remove-Item -LiteralPath $testBinary
    }
    Pop-Location
}
