@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
    echo Usage: Jogar-GT4-Candidato.cmd assets-root [--profile baseline^|recovered] [--aspect 16:9^|4:3]
    exit /b 2
)
set "GT4_ASSETS=%~f1"
shift
if not "%~5"=="" exit /b 2
rem SHIFT does not alter %%*, so pass the bounded optional arguments explicitly.
"%GT4_ASSETS%\private\tooling-venv\Scripts\python.exe" scripts\play_ee_candidate.py --assets-root "%GT4_ASSETS%" %1 %2 %3 %4
set "game_result=%errorlevel%"
if not "%game_result%"=="0" pause
exit /b %game_result%
