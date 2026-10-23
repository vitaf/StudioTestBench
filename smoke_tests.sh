#!/bin/bash
# smoke_tests.sh — быстрая проверка здоровья MindCopy (≤30 сек)
# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
PASSED=0; FAILED=0; TOTAL=0

run_test() {
    local label="$1" dir="$2" cmd="$3"
    TOTAL=$((TOTAL + 1))
    printf "${CYAN}[%d]${NC} %s ... " "$TOTAL" "$label"
    if (cd "$dir" && eval "$cmd") > /tmp/smoke_$$.txt 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}FAIL${NC}"
        tail -15 /tmp/smoke_$$.txt | sed 's/^/     /'
        FAILED=$((FAILED + 1))
    fi
}

echo -e "${YELLOW}=== MindCopy Smoke Tests ===${NC}\n"

# Тип 1: Unit — Core
run_test "Core: plugin manager lifecycle" \
    "$ROOT/BioNeuronCore/build" \
    "ctest -R test_plugin_manager --output-on-failure -j1"

# Тип 1: Unit — Studio (bion_tests: 5 быстрых проверок)
run_test "Studio: data model unit tests" \
    "$ROOT/BioNeuronStudio/build" \
    "ctest -R 'DataModel\.' --output-on-failure -j1"

# Тип 2: Integration — минимальная генерация + симуляция
run_test "Studio: regression bugs" \
    "$ROOT/BioNeuronStudio/build" \
    "ctest -R sim_regression_bugs --output-on-failure -j1"

# Тип 3: Component — SDK CMake-модуль
run_test "SDK: cmake module" \
    "$ROOT/BioNeuronSDK/build" \
    "ctest -R test_cmake_module --output-on-failure -j1"

# Тип 8: Chaos — двойной старт/стоп
run_test "Studio: chaos (double start)" \
    "$ROOT/BioNeuronStudio/build" \
    "ctest -R sim_chaos --output-on-failure -j1"

# Тип 6: Security — битый .bion
run_test "Studio: bion validation" \
    "$ROOT/BioNeuronStudio/build" \
    "ctest -R sim_bion_validation --output-on-failure -j1"

echo ""
echo -e "${YELLOW}=== Smoke: ${PASSED}/${TOTAL} passed ===${NC}"
[ $FAILED -eq 0 ] && echo -e "${GREEN}ALL SMOKE TESTS PASSED${NC}" || echo -e "${RED}${FAILED} FAILED${NC}"
rm -f /tmp/smoke_$$.txt
exit $FAILED
