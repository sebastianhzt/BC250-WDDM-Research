@echo off
setlocal EnableExtensions
if defined CL exit /b 2
if defined _CL_ exit /b 2
if defined LINK exit /b 2
if "%~1"=="" exit /b 2
if not exist "%~1" exit /b 2
set "BC250_LIFE_PYTHON=%~1"
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 2
"%BC250_LIFE_PYTHON%" -B "%~dp0verify-offline.py"
exit /b %errorlevel%
