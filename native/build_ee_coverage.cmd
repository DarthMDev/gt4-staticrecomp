@echo off
setlocal
rem Slice161 (TASK-014): slice157 VU1 all-entries runtime with the explicit-seed EE library (entries).
rem Usage: native\build_ee_coverage.cmd assets-root
rem Requires in this checkout's private/: slice153-pacing-port, slice153-pacing-shaders and
rem slice157-vu1-all-runtime (byte copies of the pinned capsules), and the library from build_entries.cmd.
if "%~1"=="" (
    echo Usage: native\build_ee_coverage.cmd assets-root
    exit /b 2
)
set "GT4_ASSETS=%~f1"
cd /d "%~dp0\.."
set "ROOT_FWD=%CD:\=/%"
set "GT4_BUILD=private\slice161-ee-coverage-build\build"
if exist "%GT4_BUILD%" (
    echo Candidate build directory already exists. Preserve it; configure a new qualified slice.
    exit /b 2
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b %errorlevel%
chcp 65001 >nul
cmake -S . -B "%GT4_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MICROS~2/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -DGT4_EE_PROFILE=entries -DGT4_ASSETS_ROOT="%GT4_ASSETS%" -DGT4_PYTHON="%GT4_ASSETS%/private/tooling-venv/Scripts/python.exe" -DGT4_PERF_RUNTIME_SOURCE="%ROOT_FWD%/private/slice157-vu1-all-runtime" -DGT4_PORT_DIR="%ROOT_FWD%/private/slice153-pacing-port" -DGT4_SHADER_DIR="%ROOT_FWD%/private/slice153-pacing-shaders" -DGT4_VU1_GENERATED_DIR="%GT4_ASSETS%\private\slice157-vu1-all-gen"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%GT4_BUILD%" --parallel 4
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%GT4_BUILD%" --output-on-failure
exit /b %errorlevel%
