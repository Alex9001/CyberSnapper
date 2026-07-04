@echo off
cd /d "%~dp0"

echo.
echo   CyberSnapper
echo   ------------
echo.

if not exist "node_modules\" (
  echo Installing dependencies...
  call npm install
)

node src/index.js

pause
