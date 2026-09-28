@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Resolve paths from this script so invocation does not depend on the working directory.
for %%I in ("%~dp0.") do set "TOY3D_SCRIPT_ROOT=%%~fI"
set "TOY3D_BUILD_DIR=%TOY3D_SCRIPT_ROOT%\build"
set "TOY3D_CONFIGURATION=Debug"
set "TOY3D_TARGET=Toy3dEditor"
set "TOY3D_EXIT_CODE=0"
set "TOY3D_PAUSE_ON_EXIT=0"
rem Keep the result visible when double-clicked; explicit arguments never pause.
if "%~1"=="" set "TOY3D_PAUSE_ON_EXIT=1"

if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
if /I "%~1"=="/?" goto usage
if not "%~3"=="" goto invalid_arguments

if not "%~1"=="" set "TOY3D_CONFIGURATION=%~1"
if not "%~2"=="" set "TOY3D_TARGET=%~2"

for %%C in (Debug Release RelWithDebInfo MinSizeRel) do if /I "%TOY3D_CONFIGURATION%"=="%%C" (
    set "TOY3D_CONFIGURATION=%%C"
    goto configuration_valid
)
goto invalid_arguments

:configuration_valid
if /I "%TOY3D_TARGET%"=="Toy3dEditor" (
    set "TOY3D_TARGET=Toy3dEditor"
    goto target_valid
)
if /I "%TOY3D_TARGET%"=="Toy3dCubeTest" (
    set "TOY3D_TARGET=Toy3dCubeTest"
    goto target_valid
)
goto invalid_arguments

:target_valid
where cmake.exe >nul 2>&1
if errorlevel 1 goto cmake_missing

echo [1/2] Generate Visual Studio 2022 x64 projects.
cmake.exe -S "%TOY3D_SCRIPT_ROOT%" -B "%TOY3D_BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DTOY3D_ENABLE_VULKAN_RHI=ON -DTOY3D_ENABLE_ASSIMP_MODEL_IMPORT=ON
set "TOY3D_EXIT_CODE=%errorlevel%"
if not "%TOY3D_EXIT_CODE%"=="0" goto configure_failed

echo [2/2] Build %TOY3D_TARGET% ^(%TOY3D_CONFIGURATION%^) and deploy to bin.
cmake.exe --build "%TOY3D_BUILD_DIR%" --config "%TOY3D_CONFIGURATION%" --target "%TOY3D_TARGET%" --parallel
set "TOY3D_EXIT_CODE=%errorlevel%"
if not "%TOY3D_EXIT_CODE%"=="0" goto build_failed

echo Build succeeded.
echo Solution: "%TOY3D_BUILD_DIR%\toy3d.sln"
echo Executable: "%TOY3D_SCRIPT_ROOT%\bin\%TOY3D_TARGET%.exe"
goto finish

:configure_failed
echo ERROR: CMake configuration failed with exit code %TOY3D_EXIT_CODE%. 1>&2
goto finish

:build_failed
echo ERROR: Build or bin deployment failed with exit code %TOY3D_EXIT_CODE%. 1>&2
echo Close the running target executable before rebuilding if it is locked. 1>&2
goto finish

:cmake_missing
set "TOY3D_EXIT_CODE=1"
echo ERROR: cmake.exe was not found in PATH. Install CMake and reopen the terminal. 1>&2
goto finish

:invalid_arguments
set "TOY3D_EXIT_CODE=2"
echo ERROR: Unsupported arguments. 1>&2

:usage
echo Usage: build_win.bat [Debug^|Release^|RelWithDebInfo^|MinSizeRel] [Toy3dEditor^|Toy3dCubeTest]
echo Default: Debug Toy3dEditor
echo Examples:
echo   build_win.bat Debug
echo   build_win.bat Release Toy3dEditor
echo   build_win.bat Debug Toy3dCubeTest
echo Requires CMake and Visual Studio 2022 with Desktop development with C++.
echo The target's CMake POST_BUILD rules deploy the executable and resources to bin.

:finish
if "%TOY3D_PAUSE_ON_EXIT%"=="1" pause
exit /b %TOY3D_EXIT_CODE%
