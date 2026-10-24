@echo off
REM Run the console test binaries for a preset. Usage: tools\test.cmd [release|debug]
setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=release
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
REM run from the project root
ctest --preset %PRESET%
endlocal
