#!/bin/bash
# GUI tests for BioNeuronStudio using Xvfb (headless virtual display)
# Usage: ./run_gui_tests.sh

STUDIO="/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio"
OUTDIR="/tmp/studio_gui_tests"
PASS=0; FAIL=0
mkdir -p "$OUTDIR"

log() { echo "  [$(date +%H:%M:%S)] $*"; }
pass() { PASS=$((PASS+1)); echo "  [PASS] $*"; }
fail() { FAIL=$((FAIL+1)); echo "  [FAIL] $*"; }

# Start Xvfb on display :99
log "Starting Xvfb on :99..."
Xvfb :99 -screen 0 1280x720x24 -ac +extension RANDR &
XVFB_PID=$!
sleep 1
export DISPLAY=:99

# Helper: take screenshot
shot() {
    local name="$1"
    xdotool windowactivate --sync $WID 2>/dev/null
    sleep 0.3
    import -window $WID "$OUTDIR/${name}.png" 2>/dev/null
    local sz=$(stat -c%s "$OUTDIR/${name}.png" 2>/dev/null || echo 0)
    log "  screenshot $name: ${sz}B"
}

# Helper: send keys to Studio
key() {
    xdotool windowactivate --sync $WID 2>/dev/null
    sleep 0.15
    xdotool key --window $WID "$@" 2>/dev/null
    sleep 0.3
}

# ── TEST 1: Launch + window ──
log "TEST 1: Launch Studio..."
$STUDIO &
STUDIO_PID=$!
for i in $(seq 1 30); do
    WID=$(xdotool search --name BioNeuronStudio 2>/dev/null | head -1)
    [ -n "$WID" ] && break
    sleep 0.5
done
if [ -z "$WID" ]; then
    fail "Studio window not found"
    kill $STUDIO_PID 2>/dev/null; kill $XVFB_PID 2>/dev/null; exit 1
fi
pass "Studio launched (WID=$WID)"
sleep 2

# ── TEST 2: Ctrl+N (new network) ──
shot "01_startup"
log "TEST 2: New network (Ctrl+N)..."
key "ctrl+n"
sleep 1
shot "02_new_network"

# ── TEST 3: Create segments via menu ──
log "TEST 3: Create segment..."
# Ctrl+Shift+B or use Build panel (Окно → Панель сборки)
key "alt+o"  # Window menu
sleep 0.3
key "Down"  # first item
key "Return"
sleep 0.5
shot "03_build_panel"

# ── TEST 4: Open NeuroML ──
log "TEST 4: Import NeuroML..."
key "alt+f"  # File menu
sleep 0.4
for i in $(seq 1 4); do key "Down"; done  # Import submenu
key "Right"
key "Return"  # NeuroML...
sleep 0.8
xdotool type --window $WID "/home/vitaf/workarea/develop/MindCopy/examples/neuroml/networks/DendConn.net.nml" 2>/dev/null
sleep 0.3
key "Return"
sleep 4
shot "04_dendconn_imported"

# ── TEST 5: Simulation start ──
log "TEST 5: Start simulation (Ctrl+R)..."
key "ctrl+r"
sleep 3
shot "05_simulation_running"

# ── TEST 6: Stop ──
log "TEST 6: Stop simulation..."
key "ctrl+t"
sleep 0.5
shot "06_stopped"

# ── TEST 7: Smooth joints ──
log "TEST 7: Toggle smooth joints..."
key "alt+v"  # View menu
sleep 0.3
for i in $(seq 1 8); do key "Down"; done
key "Return"
sleep 0.5
shot "07_smooth_joints"

# ── TEST 8: Wireframe ──
log "TEST 8: Wireframe toggle..."
key "alt+v"; sleep 0.3
for i in $(seq 1 6); do key "Down"; done
key "Return"
sleep 0.5
shot "08_wireframe"

# ── АНАЛИЗ СКРИНШОТОВ ──
log "=== Screenshot analysis ==="
for f in "$OUTDIR"/*.png; do
    name=$(basename "$f" .png)
    size=$(stat -c%s "$f")
    # Basic check: screenshot must be > 50KB (not blank)
    if [ "$size" -gt 50000 ]; then
        pass "$name: ${size}B (valid screenshot)"
    else
        fail "$name: ${size}B (too small - blank?)"
    fi
done

# ── CLEANUP ──
kill $STUDIO_PID 2>/dev/null
kill $XVFB_PID 2>/dev/null

echo ""
echo "=== GUI TEST REPORT: $PASS PASS, $FAIL FAIL ==="
[ $FAIL -eq 0 ] && echo "ALL GUI TESTS PASSED" || echo "SOME GUI TESTS FAILED"
exit $FAIL
