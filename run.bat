@echo off
cd /d "%~dp0"

echo.
echo   CyberSnapper
echo   ------------
echo.

:: Check if Node.js is installed
where node >nul 2>nul
if %errorlevel% neq 0 goto :noNode

:: Check if npm is installed
where npm >nul 2>nul
if %errorlevel% neq 0 goto :noNpm

if not exist "node_modules\" (
  echo Installing dependencies...
  call npm install
)

node src/index.js

pause
exit /b


:noNode
echo   [MISSING] Node.js is not installed.
echo.

:: Check if winget is available (Windows 10 1809+ / Windows 11)
where winget >nul 2>nul
if %errorlevel% neq 0 goto :manualNode

echo   Windows Package Manager (winget) detected.
echo.
echo   CyberSnapper can try to install Node.js automatically.
echo.
choice /c YN /n /m "  Install Node.js LTS now? (Y/N): "
if errorlevel 2 goto :manualNode

echo.
echo   Installing Node.js LTS via winget...
echo   (A UAC prompt may appear - click Yes to allow)
echo.

winget install --id OpenJS.NodeJS.LTS --source winget --silent --accept-package-agreements
if %errorlevel% neq 0 (
  echo.
  echo   [ERROR] Installation failed or was cancelled.
  goto :manualNode
)

echo.
echo   Node.js installed successfully!
echo   Adding Node.js to the current session...

:: Add Node.js to PATH for the current session
if exist "%ProgramFiles%\nodejs\" set "PATH=%ProgramFiles%\nodejs;%PATH%"
if exist "%ProgramFiles(x86)%\nodejs\" set "PATH=%ProgramFiles(x86)%\nodejs;%PATH%"

:: Verify node is now available
where node >nul 2>nul
if %errorlevel% neq 0 (
  echo.
  echo   [ERROR] Node.js was installed but could not be found in PATH.
  echo   Please restart your Command Prompt and run run.bat again.
  echo.
  pause
  exit /b
)

echo   Node.js is now ready.
echo.
echo   Continuing with CyberSnapper...
echo.

goto :continue

:manualNode
echo.
echo   CyberSnapper requires Node.js to run.
echo.
echo   Steps to install manually:
echo   1. Download Node.js (LTS version) from:
echo      https://nodejs.org/
echo.
echo   2. Run the installer -- make sure "Add to PATH" is checked.
echo.
echo   3. Restart your Command Prompt and run run.bat again.
echo.
echo   Or, upgrade Windows to get winget (Windows 10 1809+ / Windows 11)
echo   for automatic installation next time.
echo.
pause
exit /b


:noNpm
echo   [ERROR] npm is not installed or not in your PATH.
echo.
echo   npm should come bundled with Node.js.
echo   Please reinstall Node.js from https://nodejs.org/
echo   and make sure "Add to PATH" is selected during installation.
echo.
pause
exit /b


:continue
:: Check if npm is available now
where npm >nul 2>nul
if %errorlevel% neq 0 (
  echo   [ERROR] npm was not found even after installing Node.js.
  echo   This is unexpected. Please reinstall Node.js from:
  echo   https://nodejs.org/
  echo.
  pause
  exit /b
)

if not exist "node_modules\" (
  echo Installing dependencies...
  call npm install
)

node src/index.js

pause
exit /b
