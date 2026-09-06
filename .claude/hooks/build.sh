#!/bin/bash
# Stop hook: compile the project named in .claude/active_project.
# Exit 2 blocks the stop and feeds stderr back to Claude.

INPUT=$(cat)

# stop_hook_active means this hook already fired once for this turn.
# Blocking again would loop forever.
case "$INPUT" in
  *'"stop_hook_active":true'* | *'"stop_hook_active": true'*) exit 0 ;;
esac

ACTIVE="$CLAUDE_PROJECT_DIR/.claude/active_project"
[ -f "$ACTIVE" ] || exit 0

DIR="$CLAUDE_PROJECT_DIR/$(tr -d ' \t\r\n' < "$ACTIVE")"
[ -f "$DIR/Makefile" ] || exit 0

# DaisyToolchain is not on Git Bash's default PATH.
if ! command -v make >/dev/null 2>&1; then
  PATH="/c/Program Files/DaisyToolchain/bin:$PATH"
fi
if ! command -v make >/dev/null 2>&1; then
  echo "build: 找不到 make。預期在 C:\Program Files\DaisyToolchain\bin\make.exe" >&2
  exit 2
fi

if ! out=$(cd "$DIR" && make 2>&1); then
  echo "編譯失敗（$DIR）：" >&2
  echo "$out" | grep -iE "error" | head -20 >&2
  exit 2
fi

exit 0
