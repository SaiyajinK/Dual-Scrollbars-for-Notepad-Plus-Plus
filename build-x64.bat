@echo off
setlocal
where msbuild >nul 2>nul
if errorlevel 1 (
  echo MSBuild est introuvable. Lance ce fichier depuis un "Developer Command Prompt for VS 2022".
  pause
  exit /b 1
)
msbuild DualScrollbars.vcxproj /m /p:Configuration=Release /p:Platform=x64
if errorlevel 1 exit /b 1
echo.
echo DLL generee dans : bin\x64\Release\DualScrollbars.dll
endlocal
