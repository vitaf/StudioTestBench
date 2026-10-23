#!/bin/bash
# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
# E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
# Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.
#
# coverage.sh — измерение покрытия кода через gcov/lcov.
# Требует: gcov, lcov (apt install lcov).
#
# Использование:
#   ./tests/coverage.sh studio   — покрытие BioNeuronStudio
#   ./tests/coverage.sh core     — покрытие BioNeuronCore
#   ./tests/coverage.sh all      — всё вместе

set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="$ROOT/coverage_report"
mkdir -p "$OUTDIR"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

run_coverage() {
    local name="$1"
    local build_dir="$2"
    local cmake_flags="$3"
    local test_filter="$4"

    echo -e "${YELLOW}=== Coverage: $name ===${NC}"

    cd "$build_dir"

    # Пересборка с флагами покрытия
    echo "  Configuring with coverage flags..."
    cmake .. $cmake_flags -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="--coverage" -DCMAKE_C_FLAGS="--coverage" \
        -DCMAKE_EXE_LINKER_FLAGS="--coverage" 2>&1 | tail -1

    echo "  Building..."
    make -j$(nproc) 2>&1 | tail -1

    # Очистка старых данных
    find . -name "*.gcda" -delete 2>/dev/null || true
    lcov --zerocounters --directory . 2>/dev/null || true

    # Запуск тестов
    echo "  Running tests..."
    if [ -n "$test_filter" ]; then
        ctest -R "$test_filter" --output-on-failure 2>&1 | tail -5
    else
        ctest --output-on-failure 2>&1 | tail -5
    fi

    # Сбор покрытия
    echo "  Collecting coverage data..."
    lcov --capture --directory . --output-file "$OUTDIR/${name}.info" \
        --rc lcov_branch_coverage=1 2>&1 | tail -2

    # Фильтрация системных файлов
    lcov --remove "$OUTDIR/${name}.info" '/usr/*' '*/build/*' '*/_deps/*' \
        --output-file "$OUTDIR/${name}_filtered.info" \
        --rc lcov_branch_coverage=1 2>&1 | tail -1

    # Генерация HTML-отчёта
    genhtml "$OUTDIR/${name}_filtered.info" --output-directory "$OUTDIR/${name}_html" \
        --title "$name Coverage" --rc lcov_branch_coverage=1 \
        --legend 2>&1 | tail -5

    # Краткая сводка
    echo ""
    echo -e "${GREEN}  Summary for $name:${NC}"
    lcov --summary "$OUTDIR/${name}_filtered.info" --rc lcov_branch_coverage=1 2>&1 | grep -E "lines|functions"
    echo "  HTML: $OUTDIR/${name}_html/index.html"
    echo ""
}

case "${1:-all}" in
    core)
        run_coverage "BioNeuronCore" \
            "$ROOT/BioNeuronCore/build" \
            "" \
            "test_"
        ;;
    sdk)
        run_coverage "BioNeuronSDK" \
            "$ROOT/BioNeuronSDK/build" \
            "-DBUILD_SDK_TESTS=ON -DBUILD_EXAMPLES=OFF" \
            ""
        ;;
    studio)
        run_coverage "BioNeuronStudio" \
            "$ROOT/BioNeuronStudio/build" \
            "-DBION_BUILD_TESTS=ON" \
            "sim_"
        ;;
    all|*)
        run_coverage "BioNeuronCore" \
            "$ROOT/BioNeuronCore/build" \
            "" \
            "test_"
        run_coverage "BioNeuronSDK" \
            "$ROOT/BioNeuronSDK/build" \
            "-DBUILD_SDK_TESTS=ON -DBUILD_EXAMPLES=OFF" \
            ""
        run_coverage "BioNeuronStudio" \
            "$ROOT/BioNeuronStudio/build" \
            "-DBION_BUILD_TESTS=ON" \
            "sim_"
        echo -e "${GREEN}All coverage reports in: $OUTDIR/${NC}"
        ;;
esac
