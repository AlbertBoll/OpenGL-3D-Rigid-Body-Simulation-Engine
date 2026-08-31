@echo off
setlocal

cd /d "%~dp0\.."

echo ========================================
echo   GEngine - Generate Visual Studio
echo ========================================
echo.

echo Using Premake VS2022 generator.
echo The generated solution can be opened with Visual Studio 2026.
echo.

vendor\bin\premake\premake5.exe vs2022

if errorlevel 1 (
    echo.
    echo [ERROR] Premake generation failed.
    pause
    exit /b 1
)

echo.
echo [OK] GEngine.sln generated successfully.
echo [INFO] Open GEngine.sln with Visual Studio 2026.
echo.

pause