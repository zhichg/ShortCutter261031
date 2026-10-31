function Get-ShortCutterTestCompiler {
    if ($env:MINGW) {
        $configuredCompiler = Join-Path $env:MINGW 'bin\gcc.exe'
        if (Test-Path -LiteralPath $configuredCompiler) { return $configuredCompiler }
    }
    $availableCompiler = Get-Command gcc.exe -ErrorAction SilentlyContinue
    if ($availableCompiler) { return $availableCompiler.Source }
    $workspaceCompiler = 'D:\7-AIWorkspace\tools\mingw64\bin\gcc.exe'
    if (Test-Path -LiteralPath $workspaceCompiler) { return $workspaceCompiler }
    throw 'MinGW GCC was not found. Set MINGW for this process or add GCC to PATH.'
}
