@echo off
cd /d "%~dp0"
setlocal enabledelayedexpansion

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
  echo.
  echo   Installing npm packages...
  call npm install --no-audit --no-fund
  if %errorlevel% neq 0 (
    echo.
    echo   [ERROR] npm install failed. Try running: npm install
    pause
    exit /b
  )
  echo.
  echo   Dependencies installed.
)

:: Remove stale files
del /f /q .cybersnapper.url 2>nul
del /f /q .cybersnapper.pid 2>nul

:: Start server in background
start "CyberSnapper" /B node src/index.js

:: Wait for the server to write the URL file (max 15 seconds)
echo.
echo   Waiting for server to start...
set WAIT_COUNT=0
:waiturl
if not exist ".cybersnapper.url" (
  set /a WAIT_COUNT+=1
  if !WAIT_COUNT! geq 15 (
    echo.
    echo   [ERROR] Server failed to start. Check the output above for errors.
    pause
    exit /b
  )
  timeout /t 1 /nobreak >nul
  goto waiturl
)

:: Read the URL and open the browser
set /p SERVER_URL=<.cybersnapper.url
echo.
echo   Server ready at %SERVER_URL%
echo.
start "" "%SERVER_URL%"

:: Keep the window open so the user can see the server output
echo.
echo   Close this window to stop CyberSnapper.
echo.

:: Read the server PID and wait for that specific process
if exist ".cybersnapper.pid" (
  set /p SERVER_PID=<.cybersnapper.pid
  :waitpid
  timeout /t 2 /nobreak >nul
  tasklist /FI "PID eq !SERVER_PID!" 2>nul | find /I "!SERVER_PID!" >nul
  if !errorlevel! equ 0 goto waitpid
) else (
  :: Fallback: wait for any key
  pause >nul
)

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
  echo.
  echo   Installing npm packages...
  call npm install --no-audit --no-fund
  if %errorlevel% neq 0 (
    echo.
    echo   [ERROR] npm install failed. Try running: npm install
    pause
    exit /b
  )
  echo.
  echo   Dependencies installed.
)

:: Remove stale files
del /f /q .cybersnapper.url 2>nul
del /f /q .cybersnapper.pid 2>nul

:: Start server in background
start "CyberSnapper" /B node src/index.js

:: Wait for the URL file (max 15 seconds)
echo.
echo   Waiting for server to start...
set WAIT_COUNT=0
:waiturl2
if not exist ".cybersnapper.url" (
  set /a WAIT_COUNT+=1
  if !WAIT_COUNT! geq 15 (
    echo.
    echo   [ERROR] Server failed to start. Check the output above for errors.
    pause
    exit /b
  )
  timeout /t 1 /nobreak >nul
  goto waiturl2
)

:: Read the URL and open the browser
set /p SERVER_URL=<.cybersnapper.url
echo.
echo   Server ready at %SERVER_URL%
echo.
start "" "%SERVER_URL%"

:: Keep the window open
echo.
echo   Close this window to stop CyberSnapper.
echo.

:: Wait for the specific PID
if exist ".cybersnapper.pid" (
  set /p SERVER_PID=<.cybersnapper.pid
  :waitpid2
  timeout /t 2 /nobreak >nul
  tasklist /FI "PID eq !SERVER_PID!" 2>nul | find /I "!SERVER_PID!" >nul
  if !errorlevel! equ 0 goto waitpid2
) else (
  pause >nul
)

exit /b
