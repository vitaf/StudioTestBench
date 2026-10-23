# StudioTestBench — Installation & Build Guide

## Системные требования

| Компонент | Минимальная версия |
|-----------|-------------------|
| ОС | Ubuntu 20.04+ / Debian 11+ |
| Компилятор | GCC 9+ (C++17) |
| CMake | 3.20+ |
| OpenGL | 3.3 Core (для GUI-тестов) |
| GLFW | 3.3+ |
| NVIDIA GPU | GTX 1650+ (опционально, для CUDA-тестов) |
| ОЗУ | 8GB+ |

## Зависимости

```bash
sudo apt install build-essential cmake libglfw3-dev libcurl4-openssl-dev libssl-dev
```

Для CUDA-тестов:
```bash
sudo apt install nvidia-cuda-toolkit
```

## Сборка

```bash
cd StudioTestBench/build
cmake .. && make -j$(nproc)
```

FetchContent автоматически загружает: Google Test, Dear ImGui, ImPlot, GLM.

Сборка GPU backend (.so) — см. BioNeuronCore/install.md.

## Запуск тестов

```bash
cd build
ctest --output-on-failure          # все тесты
./tests/sim_debug_network           # только debug network
./tests/sim_gpu_dual_parity         # GPU parity тесты
```

## Категории тестов

| Категория | Бинарник | Описание |
|-----------|----------|---------|
| Debug Network | sim_debug_network | 9 тестов CPG отладочной сети |
| GPU Complex | sim_gpu_complex | 7 тестов GPU-симуляции |
| GPU Dual Parity | sim_gpu_dual_parity | Intel vs CPU, NVIDIA vs CPU |
| CPG Brainstem | sim_cpg_brainstem | Структурная целостность CPG |
| Full Network | sim_full_network | Полная сеть головастика |
| Regression | sim_regression, sim_regression_bugs, sim_regression_swim | Регрессионные тесты |
| Integration | sim_ipc_bridge, sim_ipc_equiv | IPC-интеграция |
| Сенсоры | sim_auditory, sim_vestibular, sim_electroreception, sim_magnetoreception | Сенсорные системы |
| Поведение | sim_chemotaxis, sim_phototaxis | Таксисы |
| E2E | sim_bridge, sim_bridge_lifecycle, sim_exp_full_workflow | End-to-end |

## Структура проекта

```
StudioTestBench/
├── build/                  # Сборочная директория
├── tests/                  # Тестовые бинарники (после сборки)
├── test_*.cpp              # Исходники тестов
├── README.md
├── CLAUDE.md
├── install.md
├── relnotes.md
└── DISCLAIMER.md
```


## Права и интеллектуальная собственность

© 2024–2026 **Фешин Виталий Александрович** / Vitaly Alexandrovich Feshin. All rights reserved.
E-mail: feshin2004@mail.ru · vfeshin@mindcopy.ru · Тел.: +7(918)345-75-18. Краснодар, Россия / Krasnodar, Russia.

Свидетельство о государственной регистрации программы для ЭВМ (Роспатент) **№ 727894_683451**.
Коммерческое использование, распространение и сублицензирование без письменного разрешения автора запрещены.
Полный текст — см. `LICENSE` и `DISCLAIMER.md` в корне платформы.
