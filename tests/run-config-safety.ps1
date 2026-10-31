$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Get-ShortCutterTestCompiler
Push-Location $projectDir
try {
    & $compiler -static -O2 -Wall -Wextra -o tests/config_safety.exe tests/config_safety.c `
        -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -ladvapi32 -lole32 -loleaut32 -luuid
    if ($LASTEXITCODE -ne 0) { throw 'Configuration safety test compilation failed.' }
    & .\tests\config_safety.exe
    if ($LASTEXITCODE -ne 0) { throw 'Configuration safety test failed.' }
} finally {
    $testPath = Join-Path $PSScriptRoot 'config_safety.exe'
    if (Test-Path -LiteralPath $testPath) { Remove-Item -LiteralPath $testPath }
    Pop-Location
}
