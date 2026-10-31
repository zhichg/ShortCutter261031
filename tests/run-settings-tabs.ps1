$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Get-ShortCutterTestCompiler
$windres = Join-Path (Split-Path $compiler -Parent) 'windres.exe'
$outputDir = Join-Path $PSScriptRoot '.output'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
Push-Location $projectDir
try {
    $resource = Join-Path $outputDir 'settings-tabs-resource.o'
    $binary = Join-Path $outputDir 'settings-tabs.exe'
    & $windres src/resource.rc -o $resource
    if ($LASTEXITCODE -ne 0) { throw 'Tab rendering test resources failed.' }
    & $compiler -static -Os -Wall -Wextra -Werror -o $binary tests/settings_tabs.c $resource `
        -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -luxtheme
    if ($LASTEXITCODE -ne 0) { throw 'Tab rendering test compilation failed.' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'Tab rendering test failed.' }
} finally { Pop-Location }
