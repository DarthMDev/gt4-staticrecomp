@echo off
setlocal
if "%~2"=="" (
    echo Usage: tools\ee_producer\build.cmd assets-root fresh-producer-directory
    exit /b 2
)
set "GT4_ASSETS=%~f1"
set "GT4_PRODUCER=%~f2"
cd /d "%~dp0\..\.."
set "GT4_PYTHON=%GT4_ASSETS%\private\tooling-venv\Scripts\python.exe"
"%GT4_PYTHON%" scripts\ee_producer.py prepare --assets-root "%GT4_ASSETS%" --output "%GT4_PRODUCER%"
if errorlevel 1 exit /b %errorlevel%
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b %errorlevel%
chcp 65001 >nul
cmake -S tools\ee_producer -B "%GT4_PRODUCER%\tool-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MICROS~2/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -DGT4_DONOR="%GT4_ASSETS%/private/references/ps2-web-recomp" -DGT4_PYTHON="%GT4_PYTHON%"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%GT4_PRODUCER%\tool-build" --target eerecomp
if errorlevel 1 exit /b %errorlevel%
"%GT4_PYTHON%" scripts\ee_producer.py generate --output "%GT4_PRODUCER%" --translator "%GT4_PRODUCER%\tool-build\eerecomp.exe"
if errorlevel 1 exit /b %errorlevel%
cmake -S tools\ee_producer -B "%GT4_PRODUCER%\game-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MICROS~2/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -DGT4_DONOR="%GT4_ASSETS%/private/references/ps2-web-recomp" -DGT4_PYTHON="%GT4_PYTHON%" -DGT4_GENERATED="%GT4_PRODUCER%/generated" -DGT4_BUILD_TRANSLATOR=OFF
if errorlevel 1 exit /b %errorlevel%
cmake --build "%GT4_PRODUCER%\game-build" --target ps2game
if errorlevel 1 exit /b %errorlevel%
"%GT4_PYTHON%" scripts\ee_producer.py record-library --output "%GT4_PRODUCER%"
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%GT4_PRODUCER%\game-build" --output-on-failure
exit /b %errorlevel%
