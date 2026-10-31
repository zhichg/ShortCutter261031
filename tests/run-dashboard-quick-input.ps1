$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Get-ShortCutterTestCompiler
Push-Location $projectDir
try {
    & $compiler -static -Os -Wall -Wextra -o tests/dashboard_quick_input.exe `
        tests/dashboard_quick_input.c -lcomctl32 -lgdi32 -luser32 -lkernel32 `
        -lshell32 -limm32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard quick input test compilation failed.' }
    & .\tests\dashboard_quick_input.exe
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard quick input test failed.' }
} finally {
    $testBinary = Join-Path $PSScriptRoot 'dashboard_quick_input.exe'
    if (Test-Path -LiteralPath $testBinary) { Remove-Item -LiteralPath $testBinary }
    Pop-Location
}
