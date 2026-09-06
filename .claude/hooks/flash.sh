#!/bin/bash
# Flash the active project over DFU. Not a hook - run it directly.
#
# make program-dfu exits 74 even on success: the :leave argument reboots the
# chip the moment the write finishes, so dfu-util's final get_status finds the
# USB connection already gone. The only reliable success signal is the string
# "File downloaded successfully" in the output.

PROJ="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}"

ACTIVE="$PROJ/.claude/active_project"
if [ ! -f "$ACTIVE" ]; then
  echo "flash: 找不到 $ACTIVE" >&2
  exit 1
fi

DIR="$PROJ/$(tr -d ' \t\r\n' < "$ACTIVE")"
cd "$DIR" || { echo "flash: 找不到 $DIR" >&2; exit 1; }

if ! command -v make >/dev/null 2>&1 || ! command -v dfu-util >/dev/null 2>&1; then
  PATH="/c/Program Files/DaisyToolchain/bin:$PATH"
fi

if ! dfu-util -l 2>/dev/null | grep -q "Found DFU"; then
  echo "flash: 沒偵測到 DFU 裝置。按住 BOOT -> 點 RESET -> 放開 BOOT，然後重跑" >&2
  exit 1
fi

out=$(make program-dfu 2>&1)
echo "$out"

if echo "$out" | grep -q "File downloaded successfully"; then
  echo "flash: OK ($DIR)"
  exit 0
fi

echo "flash: 燒錄失敗" >&2
exit 1
