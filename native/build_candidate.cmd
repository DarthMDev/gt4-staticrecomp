@echo off
setlocal
if /i not "%~1"=="baseline" if /i not "%~1"=="recovered" (
    echo Usage: native\build_candidate.cmd baseline^|recovered assets-root
    exit /b 2
)
if "%~2"=="" exit /b 2
set "GT4_PROFILE=%~1"
set "GT4_ASSETS=%~f2"
cd /d "%~dp0\.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b %errorlevel%
chcp 65001 >nul
set "GT4_BUILD=private\slice118-ee-candidates\%GT4_PROFILE%\build"
if exist "%GT4_BUILD%" (
    echo Candidate build directory already exists. Preserve it; configure a new qualified slice.
    exit /b 2
)
cmake -S . -B "%GT4_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MICROS~2/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -DGT4_EE_PROFILE="%GT4_PROFILE%" -DGT4_ASSETS_ROOT="%GT4_ASSETS%" -DGT4_PYTHON="%GT4_ASSETS%/private/tooling-venv/Scripts/python.exe"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%GT4_BUILD%"
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%GT4_BUILD%" --output-on-failure
exit /b %errorlevel%
