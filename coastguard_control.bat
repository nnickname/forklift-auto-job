@echo off
title Coastguard Control Panel
color 0A
set "CFGFILE=%~dp0coastguard_config.txt"

REM ── Load saved config (defaults) ──
set SAVED_START=04:10
set SAVED_STOP=08:00
if exist "%CFGFILE%" (
    for /f "tokens=1,2 delims==" %%a in (%CFGFILE%) do (
        if "%%a"=="START_TIME" set SAVED_START=%%b
        if "%%a"=="STOP_TIME" set SAVED_STOP=%%b
    )
)

:MENU
cls
echo.
echo  ╔══════════════════════════════════════════════════════╗
echo  ║          COASTGUARD CONTROL PANEL                    ║
echo  ╠══════════════════════════════════════════════════════╣
echo  ║                                                      ║
echo  ║   [1] Ver estado de la tarea                         ║
echo  ║   [2] Ejecutar AHORA (lanzar SA-MP)                  ║
echo  ║   [3] Programar horario (inicio + fin)               ║
echo  ║   [4] Activar / Desactivar tarea                     ║
echo  ║   [5] Matar GTA + SA-MP ahora                        ║
echo  ║   [6] Ver log del mod                                 ║
echo  ║   [0] Salir                                           ║
echo  ║                                                      ║
echo  ╠══════════════════════════════════════════════════════╣
echo  ║   Inicio: %SAVED_START%    Fin: %SAVED_STOP%                       ║
echo  ╚══════════════════════════════════════════════════════╝
echo.

set /p OPT="  Elegi una opcion: "

if "%OPT%"=="1" goto STATUS
if "%OPT%"=="2" goto RUN_NOW
if "%OPT%"=="3" goto SCHEDULE
if "%OPT%"=="4" goto TOGGLE
if "%OPT%"=="5" goto KILL
if "%OPT%"=="6" goto LOG
if "%OPT%"=="0" goto EXIT
goto MENU

REM ═══════════════════════════════════════════════
:STATUS
cls
echo.
echo  ╔══════════════════════════════════════════════════════╗
echo  ║          ESTADO DEL COASTGUARD                       ║
echo  ╠══════════════════════════════════════════════════════╣
echo  ║                                                      ║
echo  ║   Hora de INICIO:  %SAVED_START%  (Task Scheduler)            ║
echo  ║   Hora de FIN:     %SAVED_STOP%  (auto-kill GTA)              ║
echo  ║                                                      ║
echo  ╚══════════════════════════════════════════════════════╝
echo.
echo  ── Tarea de Windows ──
for /f "tokens=*" %%L in ('schtasks /Query /TN "CoastguardLauncher" /FO LIST 2^>nul ^| findstr /i "Estado Hora Nombre tarea"') do echo  %%L
schtasks /Query /TN "CoastguardLauncher" >nul 2>&1 || echo  [!] La tarea NO existe. Usa opcion 3 para crearla.
echo.
echo  (La "Fecha final" de Windows siempre dice N/A
echo   porque la tarea se repite para siempre.
echo   La hora de FIN la controla nuestro launcher.)
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:RUN_NOW
cls
echo.
echo  Lanzando SA-MP ahora...
start "" "%~dp0launch_coastguard.bat"
echo  [OK] Lanzado. Se cerrara automaticamente a las %SAVED_STOP%.
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:SCHEDULE
cls
echo.
echo  ╔══════════════════════════════════════════════════════╗
echo  ║            PROGRAMAR HORARIO                         ║
echo  ╠══════════════════════════════════════════════════════╣
echo  ║                                                      ║
echo  ║   Formato: HH:MM  (24 horas)                        ║
echo  ║                                                      ║
echo  ║   Ejemplos:                                          ║
echo  ║     04:10  = 4:10 AM                                 ║
echo  ║     16:30  = 4:30 PM                                 ║
echo  ║     22:00  = 10:00 PM                                ║
echo  ║     08:00  = 8:00 AM                                 ║
echo  ║                                                      ║
echo  ╚══════════════════════════════════════════════════════╝
echo.
echo  Horario actual:  Inicio: %SAVED_START%   Fin: %SAVED_STOP%
echo.

set /p START_TIME="  Hora de INICIO (ej: 04:10): "
echo.
set /p STOP_TIME="  Hora de FIN / auto-kill (ej: 08:00): "
echo.

REM ── Update variables ──
set SAVED_START=%START_TIME%
set SAVED_STOP=%STOP_TIME%

REM ── Save config ──
echo START_TIME=%SAVED_START%> "%CFGFILE%"
echo STOP_TIME=%SAVED_STOP%>> "%CFGFILE%"

REM ── Create/update scheduled task ──
echo  Configurando tarea...
schtasks /Delete /TN "CoastguardLauncher" /F >nul 2>&1
schtasks /Create /TN "CoastguardLauncher" /TR "\"%~dp0launch_coastguard.bat\"" /SC DAILY /ST %START_TIME% /RL HIGHEST /F

echo.
echo  ╔══════════════════════════════════════════════════════╗
echo  ║   [OK] Tarea programada!                             ║
echo  ║                                                      ║
echo  ║   Inicio:   %START_TIME%  (todos los dias)                  ║
echo  ║   Fin:      %STOP_TIME%  (auto-kill GTA)                   ║
echo  ╚══════════════════════════════════════════════════════╝
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:TOGGLE
cls
echo.
echo  Estado actual:
schtasks /Query /TN "CoastguardLauncher" /FO LIST 2>nul | findstr /i "Estado"
echo.
echo  [1] Activar (Enable)
echo  [2] Desactivar (Disable)
echo.
set /p TOPT="  Opcion: "
if "%TOPT%"=="1" (
    schtasks /Change /TN "CoastguardLauncher" /ENABLE
    echo  [OK] Tarea ACTIVADA.
)
if "%TOPT%"=="2" (
    schtasks /Change /TN "CoastguardLauncher" /DISABLE
    echo  [OK] Tarea DESACTIVADA.
)
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:KILL
cls
echo.
echo  Matando GTA y SA-MP...
taskkill /F /IM gta_sa.exe >nul 2>&1
taskkill /F /IM samp.exe >nul 2>&1
echo  [OK] Procesos terminados.
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:LOG
cls
echo.
echo  ── Ultimas 50 lineas del log ──
echo.
set "LOGPATH=%USERPROFILE%\Documents\GTA San Andreas User Files\coastguard_log.txt"
if exist "%LOGPATH%" (
    powershell -Command "Get-Content '%LOGPATH%' -Tail 50"
) else (
    echo  [!] Log no encontrado en: %LOGPATH%
    echo  Buscando en directorio del exe...
    for %%f in ("%~dp0*.log" "%~dp0*.txt") do (
        echo  Encontrado: %%f
    )
)
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:EXIT
exit /b 0
