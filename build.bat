@echo off
rem Build Terrain Oscillator (Release): Standalone, VST3, CLAP + RenderTest
rem Needs Visual Studio 2022 (Build Tools or Community) with the C++ workload (includes CMake and Ninja).
rem CMake downloads the WebView2 SDK on first configure.
setlocal
cd /d "%~dp0"

set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if not exist "%VSROOT%" set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo Visual Studio 2022 C++ tools not found & exit /b 1)
set "CM=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CM%\CMake\bin;%CM%\Ninja;%PATH%"

rem Submodules (JUCE, clap-juce-extensions)
if not exist libs\JUCE\CMakeLists.txt git submodule update --init --recursive || exit /b 1

if not exist build\build.ninja cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build %* || exit /b 1
echo.
echo Done: build\TerrainOscillator_artefacts\Release\  (Standalone, VST3, CLAP)
