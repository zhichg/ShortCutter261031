$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Get-ShortCutterTestCompiler
$resourceCompiler = Join-Path (Split-Path $compiler -Parent) 'windres.exe'
Push-Location $projectDir
try {
    & $resourceCompiler src/resource.rc -o tests/gesture_zones-resource.o
    if ($LASTEXITCODE -ne 0) { throw 'Gesture zones test resources failed to compile.' }
    & $compiler -static -Os -Wall -Wextra -Werror -o tests/gesture_zones.exe tests/gesture_zones.c tests/gesture_zones-resource.o `
        -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -luxtheme
    if ($LASTEXITCODE -ne 0) { throw 'Gesture zones test compilation failed.' }
    & .\tests\gesture_zones.exe
    if ($LASTEXITCODE -ne 0) { throw 'Gesture zones tests failed.' }
} finally {
    foreach ($file in 'gesture_zones.exe','gesture_zones-resource.o') {
        $testPath = Join-Path $PSScriptRoot $file
        if (Test-Path -LiteralPath $testPath) { Remove-Item -LiteralPath $testPath }
    }
    Pop-Location
}
