@echo off
rem ==================================================================
rem  Delta Agent - empacotador do Windows: dist\Delta.exe + zip
rem ==================================================================
setlocal
cd /d "%~dp0"
call build.bat || exit /b 1
if not exist dist mkdir dist
copy /y "bin\delta.exe" "dist\Delta.exe" >nul || exit /b 1
if exist "dist\LEIA-ME.txt" (
    powershell -NoProfile -Command "Compress-Archive -Path 'dist\Delta.exe','dist\LEIA-ME.txt' -DestinationPath 'dist\Delta-windows-x64.zip' -Force" >nul
)
for %%A in (dist\Delta.exe) do set "SIZE=%%~zA"
echo.
echo  ============================================================
echo   dist\Delta.exe        ^(%SIZE% bytes^)  arquivo unico
echo   dist\Delta-windows-x64.zip
echo.
echo   duplo clique -> instalador no navegador
echo   terminal     -> delta
echo  ============================================================
endlocal
