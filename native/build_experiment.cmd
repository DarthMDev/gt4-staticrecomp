@echo off
setlocal
cd /d "%~dp0\.."
set "VARIANT=%~1"
if "%VARIANT%"=="" (
  echo usage: native\build_experiment.cmd ^<variant^> [build options]
  echo Derived output: private\slice115-perf-investigation\^<variant^>
  exit /b 2
)
set "SLICE=private\slice115-perf-investigation\%VARIANT%"
if exist "%SLICE%\build" (
  echo refusing to rebuild over an existing experiment: %SLICE%\build
  exit /b 2
)
rem Same qualified recipe as native\build.cmd, with fresh experiment outputs.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b %errorlevel%
chcp 65001 >nul
private\tooling-venv\Scripts\python.exe scripts\prepare_external_vulkan.py "%SLICE%\port"
if errorlevel 1 exit /b %errorlevel%
"%USERPROFILE%\.cargo\bin\cargo.exe" build --locked --release --manifest-path tools\wgsl_spirv\Cargo.toml --target-dir "%SLICE%\shader-compiler"
if errorlevel 1 exit /b %errorlevel%
"%SLICE%\shader-compiler\release\gt4-wgsl-spirv.exe" private\references\ps2-web-recomp\runtime\src\gs\gpu\gs.wgsl "%SLICE%\shaders"
if errorlevel 1 exit /b %errorlevel%
cmake -S . -B "%SLICE%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MICROS~2/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -DGT4_PORT_DIR="%CD%\%SLICE%\port" -DGT4_SHADER_DIR="%CD%\%SLICE%\shaders"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%SLICE%\build" %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
