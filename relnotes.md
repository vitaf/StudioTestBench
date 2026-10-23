# Release Notes — StudioTestBench

## v0.3.x (2026-09-15 — 2026-09-27) — синхронизация тестов с Core/Studio API
- **PIR→HCN**: маркер dlc PIR→HCN1 (аудит 2026-09-09), флаг 93 → HCN (HCN1/HCN2).
- **80/81 → валидные модификаторы** (CAV13/AMPA/IONOTROPIC_CL).
- **Фаза 4 (NAK→Nav1.2)**: замена `MOD_IONOTROPIC_NAK`→`MOD_AXON_NAV12` в 35 тестах.
- **Диапазон plugin type ids**: [0,187]→[0,196] (MOD_TYPE_COUNT=197).
- **Магниторецепция (#80)**: COUNT 27→29 + disable CryptochromeModulation.
- **sim_spikes**: reserve segments — висячий указатель при переаллокации (2403c52).
- **visual_regression**: обновлён baseline main_window + 3 новых сценария.

## v0.2.0 (2026-07-01) — CPG debug network determinism + CUDA tests
- **CPG debug network**: 9/9 тестов PASS (добавлен AaaWarmupInfrastructure для прогрева Core)
- **Test determinism**: фиксированные srand() seed'ы в чувствительных тестах, сниженный параметрический джиттер
- **CUDA two-pass diffusion**: тесты CPU-GPU паритета (221 assertion, delta=0.29mV)
- **GPU dual parity tests**: Intel vs CPU, NVIDIA vs CPU, Intel vs NVIDIA (NVIDIA — skip из-за стабильности драйвера)
- **47 тестовых бинарников**: sim_debug_network, sim_gpu_complex, sim_gpu_dual_parity, sim_cpg_brainstem, sim_full_network, и др.

## v0.1.0 (2026-06-13) — Initial release
- CPG brainstem тесты (dlc_MHR→CPG drive)
- Regression тесты симуляции
- sim_bridge (Core↔Studio)
- GPU-тесты (complex, dual parity, bridge stim)
- 15+ тестовых бинарников


## Права и интеллектуальная собственность

© 2024–2026 **Фешин Виталий Александрович** / Vitaly Alexandrovich Feshin. All rights reserved.
E-mail: feshin2004@mail.ru · vfeshin@mindcopy.ru · Тел.: +7(918)345-75-18. Краснодар, Россия / Krasnodar, Russia.

Свидетельство о государственной регистрации программы для ЭВМ (Роспатент) **№ 727894_683451**.
Коммерческое использование, распространение и сублицензирование без письменного разрешения автора запрещены.
Полный текст — см. `LICENSE` и `DISCLAIMER.md` в корне платформы.
