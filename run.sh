#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

echo ""
echo "  CyberSnapper"
echo "  ————————————"
echo ""

if [ ! -d "node_modules" ]; then
  echo "Installing dependencies..."
  npm install
fi

node src/index.js

