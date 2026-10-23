#!/bin/bash
# GUI-тест v3: мышиные клики (работают на Xvfb) + проверка лога
STUDIO="/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio"
PASS=0; FAIL=0
rm -f ~/.BioNeuronStudio/logs/studio.log*

log() { echo "  [$(date +%H:%M:%S)] $*"; }
pass() { PASS=$((PASS+1)); echo "  [PASS] $*"; }
fail() { FAIL=$((FAIL+1)); echo "  [FAIL] $*"; }

cleanup() {
    [ -n "$STUDIO_PID" ] && kill $STUDIO_PID 2>/dev/null
    [ -n "$XVFB_PID" ] && kill $XVFB_PID 2>/dev/null
    echo ""; echo "=== GUI TEST: $PASS PASS, $FAIL FAIL ==="
    exit $FAIL
}
trap cleanup EXIT

check() {
    local step="$1"; local marker="$2"; sleep 0.5
    if ! kill -0 $STUDIO_PID 2>/dev/null; then
        fail "$step: Studio CRASHED"
        log "Last log lines:"
        tail -30 ~/.BioNeuronStudio/logs/studio.log 2>/dev/null | grep -E 'GUI-TEST|error|FATAL|Segmentation'
        cleanup
    fi
    if [ -n "$marker" ] && grep -q "$marker" ~/.BioNeuronStudio/logs/studio.log 2>/dev/null; then
        pass "$step"
    else
        fail "$step: marker '$marker' not in log"
    fi
}

# Старт
export DISPLAY=:99
Xvfb :99 -screen 0 1280x720x24 -ac +extension RANDR &
XVFB_PID=$!
sleep 1

log "Launching Studio..."
$STUDIO &
STUDIO_PID=$!
for i in $(seq 1 40); do
    WID=$(xdotool search --name BioNeuronStudio 2>/dev/null | head -1)
    [ -n "$WID" ] && break
    sleep 0.5
done
[ -z "$WID" ] && { fail "Window not found"; exit 1; }
log "WID=$WID PID=$STUDIO_PID"
sleep 2
check "STARTUP" "APP START"

# ШАГ 1: Открыть панель (Alt+L → мышь на Подопытные → Головастик)
log "STEP 1: Open tadpole panel..."
# Alt+L открывает меню Лаборатория
xdotool key --window $WID --clearmodifiers "alt+l"
sleep 0.8
# Мышь: меню открыто в левом верхнем углу. Подопытные примерно на 200px вниз
xdotool mousemove --window $WID 250 380
sleep 0.3
xdotool click --window $WID 1
sleep 0.5
# Submenu: Головастик
xdotool mousemove --window $WID 400 400
sleep 0.3
xdotool click --window $WID 1
sleep 2
check "PANEL_OPEN" "GUI-TEST.*RenderTadpolePanel"

# ШАГ 2: Выбрать стадию (комбо-бокс) и нажать Generate
log "STEP 2: Generate..."
# Клик на комбо-бокс стадии (примерно x=300, y=280 в панели)
xdotool mousemove --window $WID 300 320
sleep 0.3
xdotool click --window $WID 1
sleep 0.3
# Выбрать Late Tailbud (второй пункт)
xdotool mousemove --window $WID 300 350
sleep 0.3
xdotool click --window $WID 1
sleep 0.5
# Клик на "Сгенерировать коннектом" (примерно y=360)
xdotool mousemove --window $WID 300 390
sleep 0.3
xdotool click --window $WID 1
sleep 8
check "GENERATE" "GUI-TEST.*GENERATE done"

# ШАГ 3: Загрузить в редактор
log "STEP 3: Load into editor..."
xdotool mousemove --window $WID 300 450
sleep 0.3
xdotool click --window $WID 1
sleep 3
check "LOAD" "GUI-TEST.*LOAD button clicked"

# ШАГ 4: Запустить эмуляцию+симуляцию
log "STEP 4: Start emulation+simulation..."
# Скроллим вниз в панели до кнопки Start
xdotool mousemove --window $WID 300 600
sleep 0.3
xdotool click --window $WID 4  # scroll down
sleep 0.3
xdotool click --window $WID 4
sleep 0.3
xdotool click --window $WID 4
sleep 0.5
# Клик на кнопку Start
xdotool mousemove --window $WID 350 620
sleep 0.3
xdotool click --window $WID 1
sleep 6

if ! kill -0 $STUDIO_PID 2>/dev/null; then
    fail "CRASHED during emulation start"
    log "Crash log:"
    grep -E 'GUI-TEST|error|FATAL|Segmentation|signal' ~/.BioNeuronStudio/logs/studio.log 2>/dev/null | tail -20
else
    check "EMU_START" "GUI-TEST.*START EMULATION"
    check "EMU_DONE" "GUI-TEST.*on_start_emulation.*DONE"
fi

# Итоговый лог
log "=== All GUI-TEST markers ==="
grep 'GUI-TEST' ~/.BioNeuronStudio/logs/studio.log 2>/dev/null | head -20
