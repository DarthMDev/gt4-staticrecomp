@echo off
setlocal
cd /d "%~dp0\.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b %errorlevel%
chcp 65001 >nul
private\tooling-venv\Scripts\python.exe scripts\prepare_external_vulkan.py private\slice114-native-base\port
if errorlevel 1 exit /b %errorlevel%
"%USERPROFILE%\.cargo\bin\cargo.exe" build --locked --release --manifest-path tools\wgsl_spirv\Cargo.toml --target-dir private\slice114-native-base\shader-compiler
if errorlevel 1 exit /b %errorlevel%
private\slice114-native-base\shader-compiler\release\gt4-wgsl-spirv.exe private\references\ps2-web-recomp\runtime\src\gs\gpu\gs.wgsl private\slice114-native-base\shaders
if errorlevel 1 exit /b %errorlevel%
cmake -S . -B private\slice114-native-base\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MICROS~2/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe"
if errorlevel 1 exit /b %errorlevel%
cmake --build private\slice114-native-base\build
exit /b %errorlevel%
