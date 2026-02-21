@echo off
REM ═══════════════════════════════════════════════════════
REM  Coastguard Auto-Launcher
REM  Launches SA-MP, connects to server.
REM  The .asi mod handles: auto-login + coastguard route.
REM  Reads STOP_HOUR from coastguard_config.txt
REM
REM  Usage: launch_coastguard.bat
REM  Schedule: via coastguard_control.bat
REM ═══════════════════════════════════════════════════════

set SAMP_DIR=C:\Users\barto\Desktop\games\installergta
set SERVER_IP=play.sarp.es
set SERVER_PORT=7777
set PLAYER_NAME=Xylos
set STOP_TIME=08:00

REM ── Read config file if exists ──
set "CFGFILE=%~dp0coastguard_config.txt"
if exist "%CFGFILE%" (
    for /f "tokens=1,2 delims==" %%a in (%CFGFILE%) do (
        if "%%a"=="STOP_TIME" set STOP_TIME=%%b
    )
)

REM ── Parse stop time ──
for /f "tokens=1,2 delims=:" %%a in ("%STOP_TIME%") do (
    set STOP_HOUR=%%a
    set STOP_MIN=%%b
)

echo ═══════════════════════════════════════════════
echo   COASTGUARD AUTO-LAUNCHER
echo   [%date% %time%]
echo   Server: %SERVER_IP%:%SERVER_PORT%
echo   Player: %PLAYER_NAME%
echo   Auto-stop: %STOP_HOUR%:%STOP_MIN%
echo ═══════════════════════════════════════════════

REM Copy latest .asi to GTA directory
copy /Y "C:\Users\barto\Desktop\samp-forklift-mod\build\Release\samp-forklift-mod.asi" "%SAMP_DIR%\samp-forklift-mod.asi" >nul 2>&1

REM Launch SA-MP
cd /d "%SAMP_DIR%"
start "" "samp.exe" %SERVER_IP%:%SERVER_PORT%
echo [%time%] SA-MP launched. Auto-kill at %STOP_HOUR%:%STOP_MIN%

REM ── Wait loop: check every 60s if it's time to stop ──
:WAIT_LOOP
timeout /t 60 /nobreak >nul
for /f "tokens=1,2 delims=:." %%h in ("%time: =0%") do (
    set CURHOUR=%%h
    set CURMIN=%%i
)
REM Compare HHMM as number
set /a CURVAL=%CURHOUR%*60+%CURMIN%
set /a STOPVAL=%STOP_HOUR%*60+%STOP_MIN%
if %CURVAL% GEQ %STOPVAL% goto STOP_GAME
goto WAIT_LOOP

:STOP_GAME
echo [%time%] Stop time reached (%STOP_HOUR%:%STOP_MIN%) — killing GTA...
taskkill /F /IM gta_sa.exe >nul 2>&1
taskkill /F /IM samp.exe >nul 2>&1
echo [%time%] Done. Exiting.
exit /b 0
exit /b 0