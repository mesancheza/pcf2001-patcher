@echo off
rem Builds everything as 32-bit into build\<config>.
rem Usage: build.bat [Release|Debug]   (default: Release)
rem Needs Visual Studio or its Build Tools with the "Desktop development with C++" workload.
setlocal

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    echo No Visual Studio installation with the C++ x86 tools was found.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cmake -S "%~dp0." -B "%~dp0build\%CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "%~dp0build\%CONFIG%" || exit /b 1
