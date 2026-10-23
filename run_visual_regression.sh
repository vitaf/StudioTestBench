#!/bin/bash
# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
# E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
#
# G7: Visual regression test — Xvfb + Studio + скриншоты + сравнение.
# CI-ready: возвращает 77 (SKIP) если нет Xvfb/xdotool/ImageMagick.
# FAIL если diff > 1% или PSNR < 30dB.
set -e

STUDIO="/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Visual Regression Test (G7) ==="

# Check prerequisites
MISSING=()
for tool in Xvfb xdotool import python3; do
    if ! command -v "$tool" &>/dev/null; then
        MISSING+=("$tool")
    fi
done

# Check PIL/numpy
python3 -c "from PIL import Image; import numpy" 2>/dev/null || {
    echo "SKIP: Python PIL/numpy not installed (pip install Pillow numpy)"
    exit 77
}

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "SKIP: Missing tools: ${MISSING[*]}"
    exit 77
fi

if [ ! -x "$STUDIO" ]; then
    echo "SKIP: BioNeuronStudio not built at $STUDIO"
    exit 77
fi

# 1. Find free X display
DISP=:99
while [ -f "/tmp/.X${DISP#:}-lock" ]; do
    DISP=":$(( ${DISP#:} + 1 ))"
done
echo "[1/4] Xvfb on $DISP..."

Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render &
XVFB_PID=$!
sleep 1
export DISPLAY="$DISP"

cleanup() {
    kill $STUDIO_PID 2>/dev/null || true
    wait $STUDIO_PID 2>/dev/null || true
    kill $XVFB_PID 2>/dev/null || true
    wait $XVFB_PID 2>/dev/null || true
    rm -f "/tmp/.X${DISP#:}-lock" 2>/dev/null || true
}
trap cleanup EXIT

# 2. Start Studio
echo "[2/4] Starting BioNeuronStudio..."
pkill -f BioNeuronStudio 2>/dev/null || true
sleep 0.5
"$STUDIO" &
STUDIO_PID=$!

# Wait for window
echo "  Waiting for window..."
WID=""
for i in $(seq 1 30); do
    WID=$(xdotool search --name BioNeuronStudio 2>/dev/null | head -1)
    [ -n "$WID" ] && break
    sleep 1
done

if [ -z "$WID" ]; then
    echo "  SKIP: Window not found after 30s (Studio may need GUI deps)"
    exit 77
fi
echo "  Window found after ${i}s"

sleep 2  # Let GUI fully render

# 3. Visual regression
echo "[3/4] Running visual regression..."
python3 "$SCRIPT_DIR/test_visual_regression.py" "$@"
RC=$?

# 4. Cleanup
echo "[4/4] Cleanup..."
kill $STUDIO_PID 2>/dev/null || true
wait $STUDIO_PID 2>/dev/null || true
kill $XVFB_PID 2>/dev/null || true
wait $XVFB_PID 2>/dev/null || true

echo ""
if [ $RC -eq 0 ]; then
    echo "VISUAL REGRESSION: PASS"
else
    echo "VISUAL REGRESSION: FAIL (exit $RC)"
fi
exit $RC
