#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

echo ""
echo "  CyberSnapper"
echo "  ————————————"
echo ""

# Check if Node.js is installed
if ! command -v node &>/dev/null; then
  echo "  [MISSING] Node.js is not installed."
  echo ""

  # Detect OS / package manager
  if [[ "$OSTYPE" == "darwin"* ]]; then
    if command -v brew &>/dev/null; then
      echo "  Install with Homebrew:"
      echo "    brew install node"
    else
      echo "  Install Node.js from:"
      echo "    https://nodejs.org/"
      echo ""
      echo "  Or install Homebrew first, then run:"
      echo "    /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
      echo "    brew install node"
    fi
  elif command -v apt &>/dev/null; then
    echo "  Install with apt:"
    echo "    sudo apt update && sudo apt install nodejs npm"
    echo ""
    echo "  Note: On older Debian/Ubuntu, the binary is called \`nodejs\`."
    echo "  If \`node\` is not found after install, create a symlink:"
    echo "    sudo ln -s /usr/bin/nodejs /usr/bin/node"
  elif command -v dnf &>/dev/null; then
    echo "  Install with dnf:"
    echo "    sudo dnf install nodejs npm"
  elif command -v pacman &>/dev/null; then
    echo "  Install with pacman:"
    echo "    sudo pacman -S nodejs npm"
  elif command -v apk &>/dev/null; then
    echo "  Install with apk:"
    echo "    apk add nodejs npm"
  else
    echo "  Download Node.js from:"
    echo "    https://nodejs.org/"
  fi

  echo ""
  echo "  Alternatively, use nvm (Node Version Manager):"
  echo "    curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash"
  echo "    nvm install --lts"
  echo ""
  echo "  After installing, restart your terminal and run run.sh again."
  echo ""
  exit 1
fi

# Check if npm is installed
if ! command -v npm &>/dev/null; then
  echo "  [ERROR] npm is not installed."
  echo ""
  echo "  npm should come bundled with Node.js."
  echo "  Please reinstall Node.js from https://nodejs.org/"
  echo ""
  exit 1
fi

if [ ! -d "node_modules" ]; then
  echo "Installing dependencies..."
  npm install
fi

node src/index.js
