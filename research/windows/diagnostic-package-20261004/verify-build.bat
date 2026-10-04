@echo off
setlocal EnableExtensions
if defined CL exit /b 2
if defined _CL_ exit /b 2
if defined LINK exit /b 2
rem Compile/link only. No signing, installation, registry or device calls.
if "%~1"=="" exit /b 2
if not exist "%~1" exit /b 2
set "BC250_DIAG_PYTHON=%~1"
set "BC250_DIAG_VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%BC250_DIAG_VCVARS%" exit /b 2
call "%BC250_DIAG_VCVARS%" >nul
if errorlevel 1 exit /b 2
"%BC250_DIAG_PYTHON%" -B "%~dp0verify-build.py"
exit /b %errorlevel%
