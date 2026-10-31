@echo off
REM Script de build pour Windows
REM Chess Engine - David Serreau

echo ========================================
echo Chess Engine v1.0.0 - Build
echo ========================================
echo.

REM Créer le répertoire de build
if not exist build mkdir build
cd build

REM Configurer avec CMake
echo Configuration avec CMake...
cmake -G "MinGW Makefiles" ..
if %errorlevel% neq 0 (
    echo Erreur lors de la configuration CMake
    pause
    exit /b 1
)

REM Compiler
echo Compilation...
mingw32-make -j4
if %errorlevel% neq 0 (
    echo Erreur lors de la compilation
    pause
    exit /b 1
)

echo.
echo Build terminé avec succès !
echo.
echo Exécuter: chess_engine.exe
echo.

pause
