@echo off
rem =====================================================================
rem  Delta Agent - build em um comando (Windows, gcc/MinGW, sem dependencias)
rem  Uso:  build.bat            compila bin\delta.exe
rem        build.bat test       compila e roda os testes
rem        build.bat clean      remove build\ e bin\
rem =====================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

if /i "%~1"=="clean" (
    if exist build rmdir /s /q build
    if exist bin rmdir /s /q bin
    echo limpo.
    exit /b 0
)

rem --- encontra o gcc -------------------------------------------------
rem 1) prefere um caminho sem espacos (gcc quebra o linker quando o
rem    diretorio de instalacao contem espaco)
set "GCC="
if exist "C:\mingw64\bin\gcc.exe" set "GCC=C:\mingw64\bin\gcc.exe"

if not defined GCC (
    for /f "delims=" %%I in ('where gcc 2^>nul') do if not defined GCC set "GCC=%%I"
)
if not defined GCC (
    echo.
    echo   ERRO: gcc nao encontrado no PATH.
    echo   Instale o MinGW-w64 ^(winget install BrechtSanders.WinLibs.POSIX.UCRT^)
    echo   e abra um novo terminal.
    echo.
    exit /b 1
)

rem --- gcc em caminho com espaco: tenta o nome curto 8.3 --------------
set "NOSPACE=!GCC: =!"
if not "!NOSPACE!"=="!GCC!" (
    for %%I in ("!GCC!") do set "GCC=%%~sI"
    set "NOSPACE=!GCC: =!"
    if not "!NOSPACE!"=="!GCC!" (
        echo.
        echo   ERRO: gcc esta em um caminho com espacos e o gcc nao consegue
        echo   passar as pastas de bibliotecas para o linker.
        echo   Corrija uma vez com:
        echo.
        echo       robocopy "!GCC!\..\.." C:\mingw64 /E
        echo.
        echo   e rode o build novamente.
        exit /b 1
    )
    echo [info] gcc em caminho com espaco; usando ^"%GCC%^"
)

echo [info] compilando com "%GCC%"
if not exist build mkdir build
if not exist bin   mkdir bin

set "CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc"
set "LDFLAGS=-static -lwinhttp -luser32 -lkernel32 -lmsvcrt -lws2_32 -lshell32 -lole32 -luuid"

set "OBJS="
for %%F in (util json okcolor theme ui term clipboard i18n tools tools_ext extensions provider modelsdev agent codegraph setup main) do (
    echo   cc src\%%F.c
    "%GCC%" %CFLAGS% -c "src\%%F.c" -o "build\%%F.o" || exit /b 1
    set "OBJS=!OBJS! build\%%F.o"
)

rem icone do executavel (opcional: so' se windres estiver no PATH)
set "RES="
for %%I in (windres.exe) do if not "%%~$PATH:I"=="" set "RES=build\delta-res.o"
if defined RES (
    echo   rc assets\delta.rc
    windres -i assets\delta.rc -O coff -o build\delta-res.o || set "RES="
)

echo   ld bin\delta.exe
"%GCC%" %OBJS% %RES% -o bin\delta.exe %LDFLAGS% || exit /b 1

for %%A in (bin\delta.exe) do set "SIZE=%%~zA"
echo.
echo   pronto: bin\delta.exe  ^(%SIZE% bytes^)
echo.

if /i "%~1"=="test" (
    echo   cc tests\test_main.c
    set "COREOBJS="
    for %%F in (util json okcolor theme ui) do set "COREOBJS=!COREOBJS! build\%%F.o"
    "%GCC%" %CFLAGS% !COREOBJS! tests\test_main.c -o bin\delta-tests.exe -lm || exit /b 1
    bin\delta-tests.exe
)

endlocal
