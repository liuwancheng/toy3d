@echo off
rem Toy3d project Editor launcher
setlocal EnableExtensions DisableDelayedExpansion
chcp 65001 >nul
set "TOY3D_DESCRIPTOR="
set "TOY3D_DEFAULT_BIN="
if exist "%~dp0saved\editor_launch.txt" (
    rem FOR /F handles the UTF-8 LF record; SET /P requires CRLF.
    for /f "usebackq eol=| delims=" %%L in ("%~dp0saved\editor_launch.txt") do (
        if not defined TOY3D_DESCRIPTOR (
            set "TOY3D_DESCRIPTOR=%%L"
        ) else (
            set "TOY3D_DEFAULT_BIN=%%L"
        )
    )
)
if defined TOY3D_DESCRIPTOR if not exist "%~dp0%TOY3D_DESCRIPTOR%" set "TOY3D_DESCRIPTOR="
if defined TOY3D_DESCRIPTOR goto descriptor_ready
for %%F in ("%~dp0*.toy") do if exist "%%~fF" (
    if defined TOY3D_DESCRIPTOR goto ambiguous_project
    set "TOY3D_DESCRIPTOR=%%~nxF"
)
if not defined TOY3D_DESCRIPTOR goto missing_project
:descriptor_ready
if defined TOY3D_EDITOR_BIN (
    set "TOY3D_BIN=%TOY3D_EDITOR_BIN%"
) else if defined TOY3D_DEFAULT_BIN (
    set "TOY3D_BIN=%TOY3D_DEFAULT_BIN%"
) else (
    set "TOY3D_BIN=%~dp0..\bin"
)
if not exist "%TOY3D_BIN%\Toy3dEditor.exe" goto missing_editor
"%TOY3D_BIN%\Toy3dEditor.exe" %* "--Project=%~dp0%TOY3D_DESCRIPTOR%"
set "TOY3D_LAUNCH_RESULT=%errorlevel%"
if "%TOY3D_LAUNCH_RESULT%"=="0" exit /b 0
if "%~1"=="" pause
exit /b %TOY3D_LAUNCH_RESULT%
:ambiguous_project
echo ERROR: Multiple .toy files. Open the intended project in Editor to refresh saved/editor_launch.txt. 1>&2
goto failed
:missing_project
echo ERROR: No .toy project beside this launcher. 1>&2
goto failed
:missing_editor
echo ERROR: Editor was not found. Build the engine or set TOY3D_EDITOR_BIN to its bin directory. 1>&2
:failed
if "%~1"=="" pause
exit /b 1
