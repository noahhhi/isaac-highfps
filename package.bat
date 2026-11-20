@echo off
rem Assembles the shippable download: release\isaac-highfps-<ver>.zip
rem The Workshop companion is maintained in the game's mods folder, not here.
setlocal
cd /d "%~dp0"
if "%HIGHFPS_ENABLE_J510%"=="1" (
    call "%~dp0build.bat" || exit /b 1
    python tools\package-j510.py
    exit /b
)
rem The version comes from src\version.h - the same string the DLL publishes into Lua,
rem so the zip name and what the companion sees can never drift apart.
set VER=
for /f tokens^=2^ delims^=^" %%v in ('findstr HIGHFPS_VERSION src\version.h') do set VER=%%v
if not defined VER (echo could not read HIGHFPS_VERSION from src\version.h & exit /b 1)

call "%~dp0build.bat" || exit /b 1

if exist release rmdir /s /q release
mkdir release\isaac-highfps

copy /y build\winmm.dll        release\isaac-highfps\ >nul
mkdir release\isaac-highfps\alternative-name >nul
copy /y build\dbghelp.dll     release\isaac-highfps\alternative-name\ >nul
copy /y README.md              release\isaac-highfps\ >nul
copy /y isaac-highfps.ini.example release\isaac-highfps\isaac-highfps.ini >nul

powershell -NoProfile -Command ^
  "Compress-Archive -Path 'release\isaac-highfps\*' -DestinationPath 'release\isaac-highfps-%VER%.zip' -Force"
if errorlevel 1 (echo ZIP_FAILED & exit /b 1)

echo.
echo   release\isaac-highfps-%VER%.zip   ^<- upload this to GitHub Releases
echo   then set LATEST = "%VER%" in the Workshop companion's main.lua and re-upload it,
echo   or nobody gets told this version exists.
exit /b 0
