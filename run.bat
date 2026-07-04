@echo off
cd /d "%~dp0"

echo.
echo   Screenshot Bot
echo   -------------
echo.

if not exist "node_modules\" (
  echo Installing dependencies...
  call npm install
)

node capture.js %*

echo.
pause
