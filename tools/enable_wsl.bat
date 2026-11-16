@echo off
setlocal EnableExtensions
title Activar WSL2 + Ubuntu (SM64 Living ROM)

rem ---------------------------------------------------------------------------
rem  Ejecuta este script con doble clic. Se auto-eleva a administrador.
rem  Paso 1: habilita los componentes de Windows necesarios.
rem  Paso 2: instala WSL2 + Ubuntu.
rem  Si Windows pide reiniciar, reinicia y vuelve a ejecutar el script.
rem ---------------------------------------------------------------------------

net session >nul 2>&1
if errorlevel 1 (
    echo Solicitando permisos de administrador...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo.
echo === [1/4] Habilitando Subsistema de Windows para Linux ===
dism /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
set RC1=%errorlevel%

echo.
echo === [2/4] Habilitando Plataforma de Maquina Virtual ===
dism /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
set RC2=%errorlevel%

echo.
echo === [3/4] Actualizando el nucleo de WSL ===
wsl --update
wsl --set-default-version 2

echo.
echo === [4/4] Instalando Ubuntu ===
wsl --install -d Ubuntu

echo.
if "%RC1%"=="3010" goto needreboot
if "%RC2%"=="3010" goto needreboot

echo Listo. Comprueba con:   wsl -l -v
echo Luego compila con:      wsl -d Ubuntu -- bash -lc "cd /mnt/c/ruta/a/SM64-Living-ROM && bash tools/build_rom.sh"
goto end

:needreboot
echo ------------------------------------------------------------------
echo  WINDOWS NECESITA REINICIAR para terminar de activar WSL.
echo  1) Reinicia el PC.
echo  2) Vuelve a ejecutar este script (instalara Ubuntu).
echo ------------------------------------------------------------------

:end
echo.
pause
endlocal
