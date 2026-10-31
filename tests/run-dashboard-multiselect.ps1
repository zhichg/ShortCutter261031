$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Get-ShortCutterTestCompiler
Push-Location $projectDir
try {
    & $compiler -static -Os -Wall -Wextra -o tests/dashboard_multiselect.exe `
        tests/dashboard_multiselect.c -lcomctl32 -lgdi32 -luser32 -lkernel32 `
        -lshell32 -limm32 -lole32 -loleaut32 -luuid -lcomdlg32
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard multiselect test compilation failed.' }
    & .\tests\dashboard_multiselect.exe
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard multiselect test failed.' }
} finally {
    $testBinary = Join-Path $PSScriptRoot 'dashboard_multiselect.exe'
    if (Test-Path -LiteralPath $testBinary) { Remove-Item -LiteralPath $testBinary }
    Pop-Location
}
