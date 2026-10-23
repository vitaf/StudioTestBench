# StudioTestBench — Automated Testing Platform for BioNeuronStudio

**Версия:** v0.3.1 (10 июля 2026)

## Назначение

Платформа автоматизированного тестирования BioNeuronStudio. Покрывает **11 типов тестов**: unit, integration, component, E2E, regression, smoke, performance, security, chaos, visual regression, BDD/acceptance.

Совокупно: **Studio 338 + Core 156 + Orchestrator 16 + Agent 3 + Myotome 12 = 500+ тестов, 100% PASS.**

### Изменения v0.3.0
- **Core:** +10 тестовых файлов (28/28): daemon protocol full, plugin lifecycle, security, chaos, regression, smoke, performance
- **Orchestrator:** +9 тестовых файлов (11/11): crypto, config, API, security, smoke, performance, agent executor/config/integration
- **StudioTestBench:** `test_orch_agent_e2e.cpp` — E2E orchestrator+agent (5 сценариев, SKIP без MySQL)
- **Acceptance/BDD:** `run_acceptance.py` — 15 сценариев: tadpole stages, CPG, BION roundtrip, chaos, daemon, plugins, security, regression, performance, orchestrator API/security, agent executor/integration, crypto, queue. 3 `.feature` файла для behave.
- **Суммарно:** 28 (Core) + 11 (Orchestrator+Agent) + 210+ (Studio) = 250+ тестов

### Изменения v0.2.11
- `test_converter_roundtrip.cpp` (31 тест) — SWC/NML/BION geometry roundtrip: радиусы, позиции, длины, диаметры, типы
- NML default modifiers: NoChannelDensity, WithChannelDensity, AxonHasNoCalcium
- Ion concentrations: extra_ions, intra_Na=10 unification
- Compartment modifiers: radiusFactor, densities, CPU/GPU parity (8 тестов)

## Новое в v0.2.9

- `test_chemotaxis.cpp` (8 тестов) — хемотаксис: SER-нейрон, клинотаксис, координатный фикс, CPU/GPU parity
- `test_phototaxis.cpp` (8 тестов) — фототаксис: AVOID-нейрон, барьер-тень, occlusion, CPU/GPU parity

## Новое в v0.2.2

- `test_ibuf_audit.cpp` — поканальный аудит I_buffer CPU vs GPU с тик-нормировкой (K2P, NaK_HH, KIR, Full resting)
- `test_channel_profiling.cpp` — параметризованное профилирование 18 каналов
- `test_gpu_nan.cpp` — детекция NaN на обоих бэкендах
- `test_gui_flow_debug.cpp` — полная реплика GUI-потока (генерация→save→load→kickstart→V_m)
- `test_gpu_minimal.cpp` — минимальный 1-нейронный GPU-тест

## Структура

### Production-тесты

| Файл | Тип | Проверок | Что тестирует |
|------|-----|----------|---------------|
| `test_comprehensive.cpp` | C++ | 53 | Импорт 21 .nml, BION/NeuroML roundtrip, симуляция, стимулы |
| `test_scenarios_full.cpp` | C++ | 27 | SimulationBridge, Undo/Redo, Copy/Paste, шаблоны, валидация |
| `test_business_logic.cpp` | C++ | 52 | Эксперименты, пробы, стимулы, SWC, вьюпорт, модификаторы |
| `test_sim_spikes.cpp` | C++ | 25 | Спайк-детекция, cable propagation, synapse, ионы |
| `test_remaining.cpp` | C++ | 30 | .bexp, SWC-метрики, Settings, Plugin-границы |
| `test_bridge_probes_exp.cpp` | C++ | 24 | Bridge + ProbeManager + ExperimentManager |
| `test_stdp.cpp` | C++ | 5 | STDP: LTP, LTD, eCB |
| `test_stdp_cascade.cpp` | C++ | 11 | CaMKII, Calcineurin, L-type Ca²⁺, Kv4.2 |
| `test_exp_dendconn.cpp` | C++ | 41 | DendConn 3-neuron experiment |
| `test_experiment_workflow.cpp` | C++ | 80 | ConfigToProbe, ApplyExperimentSetup, index CRUD |
| `test_integration_exp.cpp` | C++ | 81 | Эксперимент↔пробы↔модель↔шаблон |
| `test_regression.cpp` | C++ | — | Регрессии 02-03.06.2026 |
| `test_regression_bugs.cpp` | C++ | 28 | Регрессии 04.06.2026 |
| `test_regression_swim.cpp` | C++ | — | Регрессии плавания |
| `test_cpg_brainstem.cpp` | C++ | 2 | CPG + ствол мозга (dlc_MHR) |
| `test_full_user_flow.cpp` | C++ | — | Полный путь пользователя |
| `test_tadpole_all_stages.cpp` | C++ | 2 | Все 4 стадии + CPG-симуляция |
| `test_myotome.cpp` | C++ | — | Сегментарная модель мышц |
| `test_debug_network.cpp` | C++ | — | Debug network self-test |
| `test_bion_validation.cpp` | C++ | 10 | BION error handling |
| `test_undo_redo.cpp` | C++ | 13 | UndoStack + NetworkModel |
| `test_bridge_lifecycle.cpp` | C++ | 14 | Bridge lifecycle |
| `test_ecb_stdp.cpp` | C++ | 7 | eCB + STDP |
| `test_gpu_complex.cpp` | C++ | 7 | CPU/GPU parity |
| `test_experiment_full_workflow.cpp` | C++ | 6 | record→save→load→repeat |
| `test_performance.cpp` | C++ | 6 | CPU/GPU tick, генерация, save/load, память |
| `test_security.cpp` | C++ | 8 | Fuzzing, .desc JSON, oversized, roundtrip |
| `test_chaos.cpp` | C++ | 5 | Double start, stop без start, load while running |

### Инструменты

| Файл | Тип | Что делает |
|------|-----|-----------|
| `run_gui_tests.sh` | Bash | GUI-тесты (Xvfb + xdotool) |
| `analyze_screenshots.py` | Python | PIL-анализ скриншотов |
| `gui_test_framework.py` | Python | Фреймворк GUI-тестов |
| `test_visual_regression.py` | Python | Сравнение скриншотов с baseline (PIL/NumPy) |
| `run_visual_regression.sh` | Bash | Запуск Xvfb + Studio + скриншот + сравнение |
| `visual_baseline/main_window.png` | PNG | Эталонный скриншот (1280×720) |

### Корневые скрипты (в MindCopy/)

| Файл | Что делает |
|------|-----------|
| `smoke_tests.sh` | Быстрый прогон 4 ключевых тестов |
| `tests/coverage.sh` | lcov/gcov HTML-отчёты покрытия |
| `tests/valgrind.sh` | Проверка утечек (10 тестов) |
| `tests/acceptance/` | BDD: Gherkin .feature + behave steps + run_acceptance.py |
| `.github/workflows/test.yml` | CI/CD: 4 джобы (Core, SDK, Studio, Smoke) |

## Сборка и запуск

```bash
cd ~/workarea/develop/MindCopy/BioNeuronStudio/build
cmake .. -DBION_BUILD_TESTS=ON
make -j$(nproc)
ctest  # 174 теста, 100% PASS
```

Smoke (быстрый, ~30 сек):
```bash
cd ~/workarea/develop/MindCopy && bash smoke_tests.sh
```

Valgrind:
```bash
bash ~/workarea/develop/MindCopy/tests/valgrind.sh
```

Coverage:
```bash
bash ~/workarea/develop/MindCopy/tests/coverage.sh studio
```

Visual regression:
```bash
bash StudioTestBench/run_visual_regression.sh
```

## Зависимости
- BioNeuronCore (libbiocore_gcc.a)
- Xvfb, xdotool, ImageMagick (для GUI/visual тестов)
- Python 3 + Pillow + NumPy (для визуальных тестов)
- Valgrind (для проверки утечек)
- lcov (для покрытия)
- behave (для BDD, опционально)

---

## Права и интеллектуальная собственность

© 2024–2026 **Фешин Виталий Александрович** / Vitaly Alexandrovich Feshin. All rights reserved.
E-mail: feshin2004@mail.ru · vfeshin@mindcopy.ru · Тел.: +7(918)345-75-18. Краснодар, Россия / Krasnodar, Russia.

Свидетельство о государственной регистрации программы для ЭВМ (Роспатент) **№ 727894_683451**.
Коммерческое использование, распространение и сублицензирование без письменного разрешения автора запрещены.
Полный текст — см. `LICENSE` и `DISCLAIMER.md` в корне платформы.
