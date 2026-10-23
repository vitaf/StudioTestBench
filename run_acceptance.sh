#!/bin/bash
# run_acceptance.sh — приёмочные (BDD) тесты MindCopy
# Запускается из ctest, требует собранных тестовых бинарников.
set -e
MINDCOPY_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export MINDCOPY_ROOT

# Проверка prerequisites: все нужные бинарники должны существовать
REQUIRED=(
    "$MINDCOPY_ROOT/BioNeuronStudio/build/tests/sim_tadpole_stages"
    "$MINDCOPY_ROOT/BioNeuronCore/build/test_core_smoke"
    "$MINDCOPY_ROOT/BioNeuronOrchestrator/build/test_orch_api"
)
MISSING=0
for f in "${REQUIRED[@]}"; do
    if [ ! -f "$f" ] && [ ! -x "$f" ]; then
        echo "SKIP: missing prerequisite: $f"
        MISSING=1
    fi
done
if [ $MISSING -eq 1 ]; then
    echo "SKIP: not all prerequisites are built — skipping acceptance tests"
    exit 77
fi

cd "$MINDCOPY_ROOT/BioNeuronStudio/build"
python3 "$MINDCOPY_ROOT/StudioTestBench/tests-infra/acceptance/run_acceptance.py"
