@echo off
setlocal
:: Builds koi_frames.exe with Windhawk's clang (the compiler Glass Wings' preview uses).
:: It includes Glass Wings' koi renderer, windhawk\scenes\koi_pond.h, from the folder below.
set "GW=%~dp0..\..\..\..\glass-wings\windhawk\scenes"
if not "%GLASS_WINGS_SCENES%"=="" set "GW=%GLASS_WINGS_SCENES%"
set "CLANG=%ProgramW6432%\Windhawk\Compiler\bin\clang++.exe"
if not exist "%CLANG%" set "CLANG=%ProgramFiles%\Windhawk\Compiler\bin\clang++.exe"
if not exist "%CLANG%" (
    echo [koi_frames] Windhawk compiler not found: "%CLANG%"
    exit /b 1
)
if not exist "%GW%\koi_pond.h" (
    echo [koi_frames] koi_pond.h not found in "%GW%" ^(set GLASS_WINGS_SCENES^)
    exit /b 1
)
"%CLANG%" -std=c++23 -O2 -target x86_64-w64-mingw32 ^
    -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D__USE_MINGW_ANSI_STDIO=0 ^
    -municode -Wall -Wextra -Wno-unused-function -static -I"%GW%" ^
    "%~dp0koi_frames.cpp" -o "%~dp0koi_frames.exe" ^
    -ld2d1 -lwindowscodecs -lole32 -lgdi32 -luser32 -luuid
set RC=%ERRORLEVEL%
if %RC% neq 0 (echo [koi_frames] FAILED ^(exit %RC%^)) else (echo [koi_frames] OK)
endlocal & exit /b %RC%
