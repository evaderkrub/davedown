@echo off
REM Configure + build with the MSVC toolchain from a plain shell.
REM Usage: tools\build.cmd [release|debug] [extra ninja targets...]
setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=release
shift
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
REM run from the project root
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% %1 %2 %3 %4 || exit /b 1
endlocal
