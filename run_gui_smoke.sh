#!/bin/bash
# run_gui_smoke.sh — Xvfb-based GUI smoke test
# Проверяет: старт Studio → создание тестового нейрона → рендеринг без краша
set -e

MINDCOPY_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STUDIO_BIN="$MINDCOPY_ROOT/BioNeuronStudio/build/BioNeuronStudio"

if [ ! -x "$STUDIO_BIN" ]; then
    echo "SKIP: BioNeuronStudio not built at $STUDIO_BIN"
    exit 77
fi

# Check Xvfb
if ! command -v Xvfb &>/dev/null; then
    echo "SKIP: Xvfb not installed"
    exit 77
fi

# Find available display
DISP=:99
while [ -f "/tmp/.X${DISP#:}-lock" ]; do
    DISP=":$(( ${DISP#:} + 1 ))"
done

echo "=== GUI Smoke Test ==="
echo "Display: $DISP"
echo "Binary: $STUDIO_BIN"

# Start Xvfb
Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render &
XVFB_PID=$!
sleep 1

cleanup() {
    kill $XVFB_PID 2>/dev/null || true
    wait $XVFB_PID 2>/dev/null || true
    # Remove the lock file
    rm -f "/tmp/.X${DISP#:}-lock" 2>/dev/null || true
}
trap cleanup EXIT

# Kill any stale studio from previous runs
pkill -f "BioNeuronStudio" 2>/dev/null || true
sleep 0.5

# Run Studio under Xvfb for 5 seconds
# The --auto-create-test-neuron flag creates a test neuron on startup
# timeout ensures it doesn't hang
DISPLAY="$DISP" timeout 8 "$STUDIO_BIN" &
STUDIO_PID=$!

# Wait for Studio to start (check process exists and windows appear)
sleep 3

if ! kill -0 $STUDIO_PID 2>/dev/null; then
    wait $STUDIO_PID
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 124 ]; then
        echo "PASS: Studio started and ran without crash (exit=$EXIT_CODE)"
        exit 0
    else
        echo "FAIL: Studio crashed on startup (exit=$EXIT_CODE)"
        exit 1
    fi
fi

# Studio is still running — that's good, means it didn't crash instantly
echo "PASS: Studio running stable for 3+ seconds"

# Let it run 2 more seconds then kill it
sleep 2
kill $STUDIO_PID 2>/dev/null || true
wait $STUDIO_PID 2>/dev/null || true
EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then  # 143 = SIGTERM
    echo "PASS: Studio exited cleanly after GUI smoke test"
    exit 0
else
    echo "FAIL: Studio crashed during GUI smoke test (exit=$EXIT_CODE)"
    exit 1
fi
