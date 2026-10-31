$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Get-ShortCutterTestCompiler
$resourceCompiler = Join-Path (Split-Path $compiler -Parent) 'windres.exe'
Push-Location $projectDir
try {
    & $resourceCompiler src/resource.rc -o tests/scan_search-resource.o
    if ($LASTEXITCODE -ne 0) { throw 'Scan dialog test resources failed to compile.' }
    & $compiler -static -Os -Wall -Wextra -Werror -o tests/scan_search.exe tests/scan_search.c tests/scan_search-resource.o `
        -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -luxtheme
    if ($LASTEXITCODE -ne 0) { throw 'Scan dialog test compilation failed.' }
    & .\tests\scan_search.exe
    if ($LASTEXITCODE -ne 0) { throw 'Scan dialog tests failed.' }
} finally {
    foreach ($file in 'scan_search.exe','scan_search-resource.o') {
        $testPath = Join-Path $PSScriptRoot $file
        if (Test-Path -LiteralPath $testPath) { Remove-Item -LiteralPath $testPath }
    }
    Pop-Location
}
