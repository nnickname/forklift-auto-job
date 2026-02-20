@echo off
REM ═══════════════════════════════════════════════════════
REM  Coastguard Auto-Launcher
REM  Launches SA-MP, connects to server.
REM  The .asi mod handles: auto-login + coastguard route.
REM  Auto-kills GTA at STOP_HOUR (default 08:00).
REM
REM  Usage: launch_coastguard.bat
REM  Schedule: Task Scheduler → daily at 04:10
REM ═══════════════════════════════════════════════════════

set SAMP_DIR=C:\Users\barto\Desktop\games\installergta
set SERVER_IP=play.sarp.es
set SERVER_PORT=7777
set PLAYER_NAME=Xylos
set STOP_HOUR=08

echo ═══════════════════════════════════════════════
echo   COASTGUARD AUTO-LAUNCHER
echo   [%date% %time%]
echo   Server: %SERVER_IP%:%SERVER_PORT%
echo   Player: %PLAYER_NAME%
echo   Auto-stop: %STOP_HOUR%:00
echo ═══════════════════════════════════════════════

REM Copy latest .asi to GTA directory
copy /Y "C:\Users\barto\Desktop\samp-forklift-mod\build\Release\samp-forklift-mod.asi" "%SAMP_DIR%\samp-forklift-mod.asi" >nul 2>&1

REM Launch SA-MP
cd /d "%SAMP_DIR%"
start "" "samp.exe" %SERVER_IP%:%SERVER_PORT%
echo [%time%] SA-MP launched. Waiting until %STOP_HOUR%:00 to auto-kill...

REM ── Wait loop: check every 60s if it's time to stop ──
:WAIT_LOOP
timeout /t 60 /nobreak >nul
for /f "tokens=1 delims=:" %%h in ("%time: =0%") do set CURHOUR=%%h
if %CURHOUR% GEQ %STOP_HOUR% goto STOP_GAME
goto WAIT_LOOP

:STOP_GAME
echo [%time%] Stop hour reached — killing GTA...
taskkill /F /IM gta_sa.exe >nul 2>&1
taskkill /F /IM samp.exe >nul 2>&1
echo [%time%] Done. Exiting.
exit /b 0