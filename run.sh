#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

echo ""
echo "  Screenshot Bot"
echo "  ——————————————"
echo ""

if [ ! -d "node_modules" ]; then
  echo "Installing dependencies..."
  npm install
fi

node capture.js "$@"

echo ""
echo "Press Enter to close."
read -r
