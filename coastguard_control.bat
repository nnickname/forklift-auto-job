@echo off
title Coastguard Control Panel
color 0A

:MENU
cls
echo.
echo  ╔═══════════════════════════════════════════════╗
echo  ║       COASTGUARD CONTROL PANEL                ║
echo  ╠═══════════════════════════════════════════════╣
echo  ║                                               ║
echo  ║   [1] Ver estado de la tarea programada       ║
echo  ║   [2] Ejecutar AHORA (lanzar SA-MP)           ║
echo  ║   [3] Cambiar horario (hora inicio)           ║
echo  ║   [4] Activar / Desactivar tarea              ║
echo  ║   [5] Matar GTA + SA-MP ahora                 ║
echo  ║   [6] Ver log del mod                          ║
echo  ║   [7] Recrear tarea (04:10 AM default)        ║
echo  ║   [0] Salir                                    ║
echo  ║                                               ║
echo  ╚═══════════════════════════════════════════════╝
echo.

set /p OPT=Elegi una opcion: 

if "%OPT%"=="1" goto STATUS
if "%OPT%"=="2" goto RUN_NOW
if "%OPT%"=="3" goto CHANGE_TIME
if "%OPT%"=="4" goto TOGGLE
if "%OPT%"=="5" goto KILL
if "%OPT%"=="6" goto LOG
if "%OPT%"=="7" goto RECREATE
if "%OPT%"=="0" goto EXIT
goto MENU

REM ═══════════════════════════════════════════════
:STATUS
cls
echo.
echo  ── Estado de la tarea "CoastguardLauncher" ──
echo.
schtasks /Query /TN "CoastguardLauncher" /V /FO LIST 2>nul || echo  [!] La tarea no existe. Usa opcion 7 para crearla.
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:RUN_NOW
cls
echo.
echo  Lanzando SA-MP ahora...
start "" "%~dp0launch_coastguard.bat"
echo  [OK] Lanzado.
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:CHANGE_TIME
cls
echo.
echo  Hora actual de la tarea:
schtasks /Query /TN "CoastguardLauncher" /V /FO LIST 2>nul | findstr /i "Hora de inicio"
echo.
set /p NEWTIME=Nueva hora (formato HH:MM, ej: 04:10): 
echo.
echo  Actualizando a %NEWTIME%...
schtasks /Delete /TN "CoastguardLauncher" /F >nul 2>&1
schtasks /Create /TN "CoastguardLauncher" /TR "\"%~dp0launch_coastguard.bat\"" /SC DAILY /ST %NEWTIME% /RL HIGHEST /F
echo.
echo  [OK] Tarea actualizada a las %NEWTIME% diariamente.
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
set /p TOPT=Opcion: 
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
:RECREATE
cls
echo.
echo  Recreando tarea "CoastguardLauncher"...
echo  Horario: Diario a las 04:10 AM
echo.
schtasks /Delete /TN "CoastguardLauncher" /F >nul 2>&1
schtasks /Create /TN "CoastguardLauncher" /TR "\"%~dp0launch_coastguard.bat\"" /SC DAILY /ST 04:10 /RL HIGHEST /F
echo.
schtasks /Query /TN "CoastguardLauncher" /V /FO LIST | findstr /i "Hora Nombre Estado Tipo"
echo.
echo  [OK] Tarea creada.
echo.
pause
goto MENU

REM ═══════════════════════════════════════════════
:EXIT
exit /b 0
