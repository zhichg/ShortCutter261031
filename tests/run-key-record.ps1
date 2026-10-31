$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$compiler = if ($env:MINGW) { Join-Path $env:MINGW 'bin\gcc.exe' } else { 'gcc' }
$resourceCompiler = if ($env:MINGW) { Join-Path $env:MINGW 'bin\windres.exe' } else { 'windres' }
$outputDir = Join-Path $PSScriptRoot '.output'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$executable = Join-Path $outputDir 'key-record-test.exe'
Push-Location $projectRoot
try {
    & $compiler -std=c11 -Wall -Wextra -O2 -ffunction-sections -fdata-sections `
        'tests/key_record_test.c' '-Wl,--gc-sections' -luser32 -lkernel32 -lshell32 `
        -lole32 -loleaut32 -luuid -ladvapi32 -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Key recording test compilation failed.' }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw 'Key recording tests failed.' }
    $uiExecutable = Join-Path $outputDir 'key-record-preview.exe'
    $resource = Join-Path $outputDir 'key-record-resource.o'
    & $resourceCompiler 'src/resource.rc' -o $resource
    if ($LASTEXITCODE -ne 0) { throw 'Recording dialog test resource compilation failed.' }
    & $compiler -static -Os -Wall -Wextra -o $uiExecutable `
        'tests/key_record_preview.c' 'src/main.c' 'src/config.c' 'src/gesture.c' `
        'src/actions.c' 'src/dashboard.c' 'src/quick_input.c' $resource `
        -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -ladvapi32 `
        -lole32 -loleaut32 -luuid -luxtheme -lcomdlg32
    if ($LASTEXITCODE -ne 0) { throw 'Recording dialog test compilation failed.' }
    & $uiExecutable --check
    if ($LASTEXITCODE -ne 0) { throw 'Recording dialog tests failed.' }
} finally {
    Pop-Location
}
