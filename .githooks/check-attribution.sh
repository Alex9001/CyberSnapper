#!/bin/sh
# check-attribution.sh — shared AI-tool attribution trailer check.
#
# Reads a commit message from the file given as $1, or from stdin when no
# file is given. Prints any offending lines to stdout and exits 1 when the
# message contains an AI-tool attribution trailer; exits 0 when clean.
#
# Blocks lines like:
#
#   🤖 Generated with Codebuff
#   Co-Authored-By: Codebuff <noreply@codebuff.com>
#   Co-Authored-By: Copilot <noreply@github.com>
#   Generated with Claude Code
#
# Human "Co-Authored-By: Real Person <person@example.com>" trailers are
# allowed; only known AI tools are blocked. Add new tools to ai_tools.

ai_tools='Codebuff|Copilot|Claude|Codex|ChatGPT|Gemini|OpenAI|Cursor|Tabnine|Aider|Windsurf|Cline|Qwen|DeepSeek|Manicode'

pattern="Co-Authored-By:[[:space:]]*(GitHub[[:space:]]+)?(${ai_tools})|Generated[[:space:]]+(with|by)[[:space:]]+(GitHub[[:space:]]+)?(${ai_tools})"

if [ $# -ge 1 ] && [ -n "$1" ]; then
  if [ ! -r "$1" ]; then
    exit 0
  fi
  if grep -inE "$pattern" "$1"; then
    exit 1
  fi
else
  if grep -inE "$pattern"; then
    exit 1
  fi
fi

exit 0
