#!/bin/bash
# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
# E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
# Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.
#
# valgrind.sh — проверка утечек памяти в ключевых тестах.
# Требует: valgrind (apt install valgrind).
#
# Использование:
#   ./tests/valgrind.sh          — все тесты
#   ./tests/valgrind.sh quick    — только быстрые тесты

set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STUDIO_BUILD="$ROOT/BioNeuronStudio/build/tests"
CORE_BUILD="$ROOT/BioNeuronCore/build"
SDK_BUILD="$ROOT/BioNeuronSDK/build"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED=0
FAILED=0

VALGRIND_FLAGS="--leak-check=full --quiet"

run_valgrind() {
    local label="$1"
    local exe="$2"
    local extra_args="${3:-}"
    echo -e "${YELLOW}[valgrind]${NC} $label ..."
    valgrind $VALGRIND_FLAGS $exe $extra_args > /tmp/vg_out_$$.txt 2>&1
    local rc=$?
    # Ищем слова "definitely lost" или "indirectly lost" — это настоящие утечки
    if grep -qE "definitely lost: [1-9]|indirectly lost: [1-9]" /tmp/vg_out_$$.txt; then
        echo -e "  ${RED}FAIL${NC} (leak detected)"
        grep -E "definitely lost|indirectly lost|possibly lost" /tmp/vg_out_$$.txt | grep -v "0 bytes" | sed 's/^/    /'
        FAILED=$((FAILED + 1))
    else
        echo -e "  ${GREEN}PASS${NC} (no leaks)"
        PASSED=$((PASSED + 1))
    fi
    rm -f /tmp/vg_out_$$.txt
}

echo -e "${YELLOW}=== Valgrind Memory Leak Tests ===${NC}"
echo ""

# ── Быстрые тесты (всегда) ──────────────────────────────────────

# Core: plugin_manager
run_valgrind "Core: plugin_manager" \
    "$CORE_BUILD/test_plugin_manager"

# SDK: cmake_module (должен запускаться из SDK/build/)
echo -e "${YELLOW}[valgrind]${NC} SDK: cmake_module ..."
if (cd "$SDK_BUILD" && valgrind $VALGRIND_FLAGS ./test_cmake_module > /tmp/vg_out_$$.txt 2>&1); then
    echo -e "  ${GREEN}PASS${NC} (no leaks)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}FAIL${NC}"
    grep -E "definitely lost|indirectly lost|ERROR SUMMARY" /tmp/vg_out_$$.txt | tail -5 | sed 's/^/    /'
    FAILED=$((FAILED + 1))
fi
rm -f /tmp/vg_out_$$.txt

# Studio: regression_bugs (быстрый)
run_valgrind "Studio: regression_bugs" \
    "$STUDIO_BUILD/sim_regression_bugs"

# Studio: undo_redo
run_valgrind "Studio: undo_redo" \
    "$STUDIO_BUILD/sim_undo_redo"

# Studio: bion_validation
run_valgrind "Studio: bion_validation" \
    "$STUDIO_BUILD/sim_bion_validation"

# ── Полные тесты (только без quick) ──────────────────────────────

if [ "${1:-}" != "quick" ]; then
    # Studio: security
    run_valgrind "Studio: security" \
        "$STUDIO_BUILD/sim_security"

    # Studio: chaos
    run_valgrind "Studio: chaos" \
        "$STUDIO_BUILD/sim_chaos"

    # Studio: bridge_lifecycle
    run_valgrind "Studio: bridge_lifecycle" \
        "$STUDIO_BUILD/sim_bridge_lifecycle"

    # Studio: tadpole_stages (без CPG — только генерация)
    run_valgrind "Studio: tadpole_stages" \
        "$STUDIO_BUILD/sim_tadpole_stages" \
        "--gtest_filter=TadpoleAllStages.AllStagesGenerateWithoutOrphans"

    # Studio: ecb_stdp
    run_valgrind "Studio: ecb_stdp" \
        "$STUDIO_BUILD/sim_ecb_stdp"
fi

echo ""
echo -e "${YELLOW}=== Valgrind Results: $PASSED PASS, $FAILED FAIL ===${NC}"
exit $FAILED
