@echo off
rem ============================================================
rem  vigia dev: recompila quando src\ muda e troca o delta.exe
rem  instalado em quente (o Windows permite renomear o exe em
rem  uso; o .old que sobra o proprio Delta apaga no proximo /quit)
rem
rem  uso: abra este arquivo num terminal de lado e deixe rodando
rem  enquanto edita o codigo. Ctrl+C encerra o vigia.
rem ============================================================
setlocal EnableDelayedExpansion
set "RAIZ=%~dp0"
set "INSTALADO=%LOCALAPPDATA%\Delta\delta.exe"

echo [vigia] projeto:  %RAIZ%
echo [vigia] destino:  %INSTALADO%
echo [vigia] vigiando src\ (Ctrl+C para sair)
echo.

set "ANTES="
:loop
for /f "usebackq delims=" %%i in (`powershell -nologo -noprofile -Command "(Get-ChildItem -LiteralPath '%RAIZ%src' -Recurse -File -Include *.c,*.h | Sort-Object LastWriteTimeUtc | Select-Object -Last 1).LastWriteTimeUtc.Ticks"`) do set "AGORA=%%i"

if not "!AGORA!"=="!ANTES!" if not "!ANTES!"=="" (
    echo [vigia] mudanca em src\ detectada, compilando...
    call "%RAIZ%build.bat" > "%TEMP%\delta-vigia-build.log" 2>&1
    if errorlevel 1 (
        echo [vigia] COMPILACAO FALHOU - log em %TEMP%\delta-vigia-build.log
    ) else (
        echo [vigia] compilou: bin\delta.exe
        if exist "%INSTALADO%" call :trocar
    )
)
set "ANTES=!AGORA!"
ping -n 4 127.0.0.1 >nul
goto :loop

:trocar
rem 1. tira o exe em uso do caminho (renomear pode, apagar nao)
ren "%INSTALADO%" "delta.exe.old" >nul 2>&1
if errorlevel 1 (
    echo [vigia] nao consegui renomear o exe instalado - tentando de novo no proximo ciclo
    exit /b 1
)
rem 2. poe o novo no lugar
copy /y "%RAIZ%bin\delta.exe" "%INSTALADO%" >nul
if errorlevel 1 (
    echo [vigia] falhou ao gravar o novo exe - restaurando o anterior
    copy /y "%LOCALAPPDATA%\Delta\delta.exe.old" "%INSTALADO%" >nul
    exit /b 1
)
rem 3. o .old em uso fica para tras; o Delta o apaga no proximo inicio
del "%LOCALAPPDATA%\Delta\delta.exe.old" >nul 2>&1
echo [vigia] delta.exe instalado atualizado em quente - no proximo /quit ele ja vem novo
exit /b 0
