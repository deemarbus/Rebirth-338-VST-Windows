@echo off
setlocal

set "worker=%~dp0dist\windows\rebirth-bridge-worker.exe"
set "dll=C:\ReBirth RB-338\Rebirth.dll"

if not exist "%worker%" (
    echo ReBirth bridge worker not found:
    echo %worker%
    echo Build or extract the Windows package into dist\windows first.
    pause
    exit /b 1
)

if not exist "%dll%" (
    echo ReBirth engine DLL not found:
    echo %dll%
    echo Edit the dll path in this file if ReBirth is installed elsewhere.
    pause
    exit /b 1
)

echo Starting ReBirth Bridge. Leave this window open while using Ableton.
"%worker%" "%dll%"
set "workerExit=%ERRORLEVEL%"

if not "%workerExit%"=="0" (
    echo.
    echo ReBirth Bridge stopped with exit code %workerExit%.
)

pause
exit /b %workerExit%