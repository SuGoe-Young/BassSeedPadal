#!/bin/bash
# Scaffold a new Daisy Seed project and point active_project at it.
#
#   new_project.sh <name> [parent-dir]
#
# parent-dir is relative to the repo root and defaults to
# DaisyExamples/seed. The Makefile's LIBDAISY_DIR / DAISYSP_DIR are computed
# from the actual directory depth, so the project can live anywhere under the
# repo root - not just two levels down like the stock examples.

set -e

NAME="$1"
PARENT="${2:-DaisyExamples/seed}"

if [ -z "$NAME" ]; then
  echo "usage: new_project.sh <name> [parent-dir]" >&2
  exit 1
fi

case "$NAME" in
  *[!A-Za-z0-9_-]*)
    echo "new_project: 專案名只能用英數字、底線、減號（收到：$NAME）" >&2
    exit 1 ;;
esac

ROOT="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}"
DIR="$ROOT/$PARENT/$NAME"

if [ -e "$DIR" ]; then
  echo "new_project: $DIR 已存在，不覆寫" >&2
  exit 1
fi

LIBDAISY="$ROOT/DaisyExamples/libDaisy"
DAISYSP="$ROOT/DaisyExamples/DaisySP"
for d in "$LIBDAISY" "$DAISYSP"; do
  [ -d "$d" ] || { echo "new_project: 找不到 $d" >&2; exit 1; }
done

mkdir -p "$DIR"

# Relative paths from the new project dir back to the libraries.
REL_LIBDAISY=$(realpath --relative-to="$DIR" "$LIBDAISY")
REL_DAISYSP=$(realpath --relative-to="$DIR" "$DAISYSP")

cat > "$DIR/Makefile" <<EOF
# Project Name
TARGET = $NAME

# Sources
CPP_SOURCES = $NAME.cpp

# Library Locations
LIBDAISY_DIR ?= $REL_LIBDAISY
DAISYSP_DIR ?= $REL_DAISYSP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = \$(LIBDAISY_DIR)/core
include \$(SYSTEM_FILES_DIR)/Makefile
EOF

cat > "$DIR/$NAME.cpp" <<EOF
#include "daisy_seed.h"
#include "daisysp.h"

// Use the daisy namespace to prevent having to type
// daisy:: before all libdaisy functions
using namespace daisy;
using namespace daisysp;

// Declare a DaisySeed object called hardware
DaisySeed hardware;

void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    // Interleaved stereo: i is left, i + 1 is right.
    for(size_t i = 0; i < size; i += 2)
    {
        out[i]     = in[i];
        out[i + 1] = in[i + 1];
    }
}

int main(void)
{
    // Configure and Initialize the Daisy Seed
    // These are separate to allow reconfiguration of any of the internal
    // components before initialization.
    hardware.Configure();
    hardware.Init();
    hardware.SetAudioBlockSize(4);

    // Start calling the audio callback
    hardware.StartAudio(AudioCallback);

    // Loop forever
    for(;;) {}
}
EOF

cat > "$DIR/README.md" <<EOF
# $NAME

Audio passthrough starter. Build with \`make\`, flash with
\`.claude/hooks/flash.sh\`.
EOF

# Point the build hook at the new project.
echo "$PARENT/$NAME" > "$ROOT/.claude/active_project"

echo "new_project: 建立 $DIR"
echo "new_project: active_project -> $PARENT/$NAME"
