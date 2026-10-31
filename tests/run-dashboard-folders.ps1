param([switch]$Preview)
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Get-ShortCutterTestCompiler
Push-Location $projectDir
try {
    & $compiler -static -Os -Wall -Wextra -o tests/dashboard_folders.exe `
        tests/dashboard_folders.c src/ui.c -lcomctl32 -lgdi32 -luser32 -lkernel32 `
        -lshell32 -limm32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -luxtheme
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard folder test compilation failed.' }
    if ($Preview) { & .\tests\dashboard_folders.exe --preview }
    else { & .\tests\dashboard_folders.exe }
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard folder test failed.' }
} finally {
    foreach ($testFile in 'dashboard_folders.exe', 'dashboard-test.ini') {
        $testPath = Join-Path $PSScriptRoot $testFile
        if (Test-Path -LiteralPath $testPath) { Remove-Item -LiteralPath $testPath }
    }
    Pop-Location
}
