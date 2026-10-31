@echo off
setlocal
if defined MINGW set "PATH=%MINGW%\bin;%PATH%"

echo === ShortCutter Build ===

:: Compile resource
windres src\resource.rc -o src\resource.o
if errorlevel 1 goto :err

:: Compile C sources
gcc -static -Os -s ^
    -Wall -Wextra ^
    -o ShortCutter.exe ^
    src\main.c ^
    src\config.c ^
    src\ui.c ^
    src\gesture.c ^
    src\actions.c ^
    src\dashboard.c ^
    src\quick_input.c ^
    src\resource.o ^
    -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -ladvapi32 ^
    -lole32 -loleaut32 -luuid -luxtheme -lcomdlg32 ^
    -mwindows
if errorlevel 1 goto :err

echo === Build OK ===
dir ShortCutter.exe
goto :end

:err
echo === Build FAILED ===
exit /b 1

:end
