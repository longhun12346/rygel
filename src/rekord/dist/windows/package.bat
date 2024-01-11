@echo off
setlocal EnableDelayedExpansion

cd %~dp0
cd ..\..\..\..

start bootstrap.bat
felix.exe -pFast rekord

for /f "tokens=2 delims= " %%i in ('bin\Fast\rekord.exe --version') do (
    set RAW_VERSION=%%i
    goto out
)
:out

for /f "tokens=1,2 delims=-_" %%i in ("%RAW_VERSION%") do (
    set version=%%i
    set part=%%j
)
if "%part%"=="" (
    set VERSION=%version%
) else (
    set VERSION=%version%.%part%
)

set PACKAGE_DIR=bin\Packages\rekord\windows
mkdir %PACKAGE_DIR%

copy bin\Fast\rekord.exe %PACKAGE_DIR%\rekord.exe
copy src\rekord\README.md %PACKAGE_DIR%\README.md

cd %PACKAGE_DIR%

REM Create ZIP file
tar.exe -a -c -f ..\..\rekord_%VERSION%_win64.zip README.md rekord.exe
