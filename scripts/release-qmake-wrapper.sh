#!/usr/bin/env bash
set -euo pipefail

: "${CYBERSNAPPER_REAL_QMAKE:?Set CYBERSNAPPER_REAL_QMAKE to the Qt qmake executable}"
: "${CYBERSNAPPER_RELEASE_PLUGINS:?Set CYBERSNAPPER_RELEASE_PLUGINS to the curated plugin directory}"

if [[ "${1:-}" == "-query" && "${2:-}" == "QT_INSTALL_PLUGINS" ]]; then
  printf '%s\n' "$CYBERSNAPPER_RELEASE_PLUGINS"
  exit 0
fi

if [[ "${1:-}" == "-query" ]]; then
  while IFS= read -r line; do
    if [[ "$line" == QT_INSTALL_PLUGINS:* ]]; then
      printf 'QT_INSTALL_PLUGINS:%s\n' "$CYBERSNAPPER_RELEASE_PLUGINS"
    else
      printf '%s\n' "$line"
    fi
  done < <("$CYBERSNAPPER_REAL_QMAKE" "$@")
  exit 0
fi

exec "$CYBERSNAPPER_REAL_QMAKE" "$@"
