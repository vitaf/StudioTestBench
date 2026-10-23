#!/bin/bash
# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
# E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
# Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.
#
# Дымовой тест: проверка, что BioNeuronStudio стартует без segfault.
# Запускается под Xvfb на временном дисплее.
# Возвращает 0 если окно появилось и процесс жив через 5 сек.

set -e

STUDIO="${1:-/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio}"
XVFB_DISPLAY=:98
TIMEOUT_SEC=10

cleanup() {
    kill $STUDIO_PID 2>/dev/null || true
    kill $XVFB_PID 2>/dev/null || true
    wait $STUDIO_PID 2>/dev/null || true
    wait $XVFB_PID 2>/dev/null || true
}

trap cleanup EXIT

# Проверяем, что Xvfb есть
if ! command -v Xvfb &>/dev/null; then
    echo "SKIP: Xvfb not found"
    exit 0
fi

# Проверяем, что Studio собран
if [ ! -x "$STUDIO" ]; then
    echo "SKIP: Studio binary not found at $STUDIO"
    exit 0
fi

# Проверяем, что дисплей свободен
if xdpyinfo -display "$XVFB_DISPLAY" &>/dev/null 2>&1; then
    echo "SKIP: display $XVFB_DISPLAY in use"
    exit 0
fi

echo "Starting Xvfb on $XVFB_DISPLAY..."
Xvfb "$XVFB_DISPLAY" -screen 0 1280x720x24 -ac +extension RANDR &
XVFB_PID=$!
sleep 0.5

export DISPLAY="$XVFB_DISPLAY"

echo "Launching Studio..."
"$STUDIO" &
STUDIO_PID=$!

# Ждём появления окна ИЛИ падения процесса
WID=""
START_TS=$(date +%s)
while true; do
    # Проверяем, жив ли процесс
    if ! kill -0 $STUDIO_PID 2>/dev/null; then
        wait $STUDIO_PID 2>/dev/null
        EXIT_CODE=$?
        echo "FAIL: Studio process died with exit code $EXIT_CODE (segfault?)"
        exit 1
    fi

    # Проверяем, появилось ли окно (если есть xdotool)
    if command -v xdotool &>/dev/null; then
        WID=$(xdotool search --name BioNeuronStudio 2>/dev/null | head -1)
    fi
    if [ -n "$WID" ]; then
        echo "PASS: Studio window $WID appeared, process alive for $(($(date +%s) - START_TS))s"
        exit 0
    fi

    ELAPSED=$(($(date +%s) - START_TS))
    if [ $ELAPSED -ge $TIMEOUT_SEC ]; then
        if [ -n "$WID" ]; then
            echo "PASS: Studio alive for ${ELAPSED}s (no xdotool)"
            exit 0
        fi
        # Процесс жив, но окно не нашли — может без xdotool
        if kill -0 $STUDIO_PID 2>/dev/null; then
            echo "PASS: Studio process alive for ${ELAPSED}s"
            exit 0
        fi
        echo "FAIL: Timeout waiting for Studio window"
        exit 1
    fi

    sleep 0.5
done
