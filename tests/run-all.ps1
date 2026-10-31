param([string]$LogDirectory)
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$testCompiler = Get-ShortCutterTestCompiler
$testLogDir = if ($LogDirectory) { [IO.Path]::GetFullPath($LogDirectory) } else { Join-Path $projectDir 'design-review\implementation-2026-10-07' }
New-Item -ItemType Directory -Path $testLogDir -Force | Out-Null
Push-Location $projectDir
try {
    foreach ($suite in 'config-safety','dashboard-folders','dashboard-quick-input','dashboard-multiselect','dashboard-response','gesture-zones') {
        & (Join-Path $PSScriptRoot "run-$suite.ps1") *> (Join-Path $testLogDir "$suite.log")
        if ($LASTEXITCODE -ne 0) { throw "$suite failed." }
        Write-Output "PASS: $suite"
    }
    foreach ($fixture in 'actions_resilience','actions_activation','actions_foreground','actions_input_thread','actions_hotkey','settings_resilience','gesture_resilience','gesture_dispatch','dashboard_input_safety','dashboard_focus_lifecycle') {
        $binary = Join-Path $PSScriptRoot "$fixture.exe"
        try {
            & $testCompiler -static -Os -Wall -Wextra -Werror -o $binary "tests/$fixture.c" `
                -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -luxtheme
            if ($LASTEXITCODE -ne 0) { throw "$fixture compilation failed." }
            & $binary *> (Join-Path $testLogDir "$fixture.log")
            if ($LASTEXITCODE -ne 0) { throw "$fixture failed." }
            if ($fixture -eq 'gesture_dispatch') {
                & $binary right >> (Join-Path $testLogDir "$fixture.log")
                if ($LASTEXITCODE -ne 0) { throw "$fixture right-button check failed." }
            }
            Write-Output "PASS: $fixture"
        } finally {
            if (Test-Path -LiteralPath $binary) { Remove-Item -LiteralPath $binary }
        }
    }
} finally { Pop-Location }
