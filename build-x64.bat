
    
  
@echo off
setlocal

rem Build Dual Scrollbars x64 with Visual Studio 2026.
rem Works from a regular Command Prompt (no Developer Prompt needed).
set "ROOT=%~dp0"
set "PROJECT=%ROOT%DualScrollbars.vcxproj"
set "DLL=%ROOT%bin\x64\Release\DualScrollbars.dll"
set "DIST=%ROOT%dist"
set "ZIP=%DIST%\Dual-Scrollbars-v1.1.0-x64.zip"
set "MSBUILD="

if not exist "%PROJECT%" (
    echo ERREUR : Projet introuvable : "%PROJECT%"
    pause
    exit /b 1
)

rem Use an existing MSBuild on PATH, if available.
for %%I in (MSBuild.exe) do if not "%%~$PATH:I"=="" set "MSBUILD=%%~$PATH:I"

rem Otherwise detect the installed Visual Studio with Microsoft's vswhere.
if not defined MSBUILD (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
            if exist "%%I\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD=%%I\MSBuild\Current\Bin\MSBuild.exe"
        )
    )
)

rem Fallback for common Visual Studio 2026 installation paths.
if not defined MSBUILD (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\18\%%E\MSBuild\Current\Bin\MSBuild.exe" (
            set "MSBUILD=%ProgramFiles%\Microsoft Visual Studio\18\%%E\MSBuild\Current\Bin\MSBuild.exe"
        )
    )
)

if not defined MSBUILD (
    echo ERREUR : MSBuild.exe introuvable. Verifiez les composants C++ de Visual Studio 2026.
    pause
    exit /b 1
)

echo MSBuild : "%MSBUILD%"
echo Compilation de Dual Scrollbars x64 en Release...
"%MSBUILD%" "%PROJECT%" /m /nologo /p:Configuration=Release /p:Platform=x64
if errorlevel 1 (
    echo ERREUR : La compilation x64 a echoue.
    pause
    exit /b 1
)

if not exist "%DLL%" (
    echo ERREUR : DLL generee introuvable : "%DLL%"
    pause
    exit /b 1
)
if not exist "%DIST%" mkdir "%DIST%"
if errorlevel 1 (
    echo ERREUR : Impossible de creer le dossier dist.
    pause
    exit /b 1
)

set "DUAL_DLL=%DLL%"
set "DUAL_ZIP=%ZIP%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; Compress-Archive -LiteralPath $env:DUAL_DLL -DestinationPath $env:DUAL_ZIP -CompressionLevel Optimal -Force"
if errorlevel 1 (
    echo ERREUR : Impossible de creer l'archive x64.
    pause
    exit /b 1
)

echo.
echo SUCCES : DLL x64 : "%DLL%"
echo SUCCES : Archive x64 : "%ZIP%"
pause
endlocal

