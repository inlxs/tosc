@echo off
rem Build Terrain Oscillator (Release): Standalone, VST3, CLAP + RenderTest
rem Needs Visual Studio 2022 Build Tools with the C++ workload (includes CMake and Ninja).
setlocal
cd /d "%~dp0"

set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if not exist "%VSROOT%" set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo Visual Studio 2022 C++ tools not found & exit /b 1)
set "CM=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CM%\CMake\bin;%CM%\Ninja;%PATH%"

rem Submodules (JUCE, clap-juce-extensions)
if not exist libs\JUCE\CMakeLists.txt git submodule update --init --recursive || exit /b 1

rem WebView2 SDK from NuGet (headers + static loader); downloaded, not stored in git
set "WV2=Microsoft.Web.WebView2.1.0.2903.40"
set "WV2DIR=libs\nuget\%WV2%"
if not exist "%WV2DIR%\build\native\include\WebView2.h" (
  echo Downloading %WV2% ...
  if not exist "%WV2DIR%" mkdir "%WV2DIR%"
  curl -sL -o "%WV2DIR%\package.zip" "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40" || exit /b 1
  tar -xf "%WV2DIR%\package.zip" -C "%WV2DIR%" || exit /b 1
  del /q "%WV2DIR%\package.zip"
)

if not exist build\build.ninja cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build %* || exit /b 1
echo.
echo Done: build\TerrainOscillator_artefacts\Release\  (Standalone, VST3, CLAP)
