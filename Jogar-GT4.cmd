@echo off
setlocal
cd /d "%~dp0"
private\tooling-venv\Scripts\python.exe scripts\play_external_runtime.py %*
set "game_result=%errorlevel%"
if not "%game_result%"=="0" pause
exit /b %game_result%
