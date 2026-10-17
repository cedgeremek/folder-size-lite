@echo off
setlocal
set "FSL_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "FSL_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%FSL_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Prepare.ps1"
set "FSL_RESULT=%ERRORLEVEL%"
if not "%FSL_RESULT%"=="0" echo Preparation failed. Read the error above before retrying.
pause
exit /b %FSL_RESULT%
