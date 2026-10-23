// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_performance.cpp — нагрузочные тесты: время генерации, симуляции,
// memory footprint, save/load roundtrip.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

static int tests_run = 0, tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) FAIL(msg); } while(0)

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;
using Us = std::chrono::microseconds;

// ═══════════════════════════════════════════════════════════════════

int bench_generation_early() {
    TEST("Perf: EARLY_TAILBUD generation < 1 sec");
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 1; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;

    auto t0 = Clock::now();
    auto m = TadpoleDevelop(p).generate_connectome();
    auto t1 = Clock::now();
    auto ms = std::chrono::duration_cast<Ms>(t1 - t0).count();

    printf(" %ld ms, %zu neurons, %zu segments", ms, m.neurons.size(), m.segments.size());
    CHECK(ms < 1000, "EARLY_TAILBUD generation too slow");
    PASS();
    return 0;
}

int bench_generation_free() {
    TEST("Perf: FREE_SWIMMING generation < 10 sec");
    auto p = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    p.rb_per_hemisegment = 2; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;

    auto t0 = Clock::now();
    auto m = TadpoleDevelop(p).generate_connectome();
    auto t1 = Clock::now();
    auto ms = std::chrono::duration_cast<Ms>(t1 - t0).count();

    printf(" %ld ms, %zu neurons, %zu segments", ms, m.neurons.size(), m.segments.size());
    CHECK(ms < 10000, "FREE_SWIMMING generation too slow");
    PASS();
    return 0;
}

int bench_network_memory() {
    TEST("Perf: memory per neuron/segment (sizeof)");
    // Оценка RAM через размеры POD-структур
    size_t neuron_sz = sizeof(decltype(std::declval<NetworkModel>().neurons)::value_type);
    size_t segment_sz = sizeof(decltype(std::declval<NetworkModel>().segments)::value_type);
    size_t conn_sz = sizeof(decltype(std::declval<NetworkModel>().connections)::value_type);
    size_t syn_sz = sizeof(decltype(std::declval<NetworkModel>().synapses)::value_type);

    printf(" neuron=%zuB segment=%zuB conn=%zuB syn=%zuB",
           neuron_sz, segment_sz, conn_sz, syn_sz);

    // Все структуры должны быть > 0 и разумного размера
    CHECK(neuron_sz > 0 && neuron_sz < 1024, "unreasonable neuron size");
    CHECK(segment_sz > 0 && segment_sz < 2048, "unreasonable segment size");
    CHECK(conn_sz > 0 && conn_sz < 256, "unreasonable connection size");
    CHECK(syn_sz > 0 && syn_sz < 2048, "unreasonable synapse size");

    // Оценка для FREE_SWIMMING сети
    // ~200 нейронов × neuron_sz + ~5000 сегментов × segment_sz ≈ RAM
    size_t est_ram = 200 * neuron_sz + 5000 * segment_sz + 20000 * conn_sz + 5000 * syn_sz;
    printf(" est_FREE=%.1f MB", est_ram / (1024.0 * 1024.0));
    CHECK(est_ram < 50 * 1024 * 1024, "estimated RAM > 50 MB for FREE_SWIMMING");

    PASS();
    return 0;
}

int bench_save_load_roundtrip() {
    TEST("Perf: save + load .bion roundtrip");
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 2; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;

    auto m = TadpoleDevelop(p).generate_connectome();

    auto t0 = Clock::now();
    SaveNetwork(m, "/tmp/perf_roundtrip.bion");
    auto t1 = Clock::now();
    auto save_ms = std::chrono::duration_cast<Ms>(t1 - t0).count();

    NetworkModel loaded;
    auto t2 = Clock::now();
    bool ok = LoadNetwork(loaded, "/tmp/perf_roundtrip.bion");
    auto t3 = Clock::now();
    auto load_ms = std::chrono::duration_cast<Ms>(t3 - t2).count();

    CHECK(ok, "LoadNetwork failed");
    CHECK(m.neurons.size() == loaded.neurons.size(), "neuron count mismatch after roundtrip");
    CHECK(m.segments.size() == loaded.segments.size(), "segment count mismatch after roundtrip");

    printf(" save=%ld ms, load=%ld ms", save_ms, load_ms);
    CHECK(save_ms < 1000, "save too slow");
    CHECK(load_ms < 1000, "load too slow");
    PASS();
    return 0;
}

int bench_simulation_tick() {
    TEST("Perf: simulation tick time (CPU, minimal network)");
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 1; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    auto m = TadpoleDevelop(p).generate_connectome();
    SaveNetwork(m, "/tmp/perf_tick.bion");

    SimulationBridge sim;
    CHECK(sim.loadNetwork("/tmp/perf_tick.bion"), "loadNetwork failed");

    sim.start();
    // Прогрев: 100 тиков
    for (int i = 0; i < 100; i++) sim.step();

    // Замер: 1000 тиков
    auto t0 = Clock::now();
    for (int i = 0; i < 1000; i++) sim.step();
    auto t1 = Clock::now();
    sim.stop();

    auto total_us = std::chrono::duration_cast<Us>(t1 - t0).count();
    float us_per_tick = total_us / 1000.0f;

    printf(" %.1f µs/tick (%ld µs / 1000 ticks)", us_per_tick, total_us);
    CHECK(us_per_tick < 500.0f, "tick time > 500 µs — too slow");
    PASS();
    return 0;
}

int bench_gpu_vs_cpu_tick() {
    TEST("Perf: CPU vs GPU tick comparison (minimal network)");
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 1; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    auto m = TadpoleDevelop(p).generate_connectome();
    SaveNetwork(m, "/tmp/perf_gpu_cmp.bion");

    // ── CPU ──
    SimulationBridge sim_cpu;
    CHECK(sim_cpu.loadNetwork("/tmp/perf_gpu_cmp.bion"), "CPU loadNetwork failed");
    sim_cpu.start();
    for (int i = 0; i < 100; i++) sim_cpu.step();  // прогрев
    auto t0 = Clock::now();
    for (int i = 0; i < 1000; i++) sim_cpu.step();
    auto t1 = Clock::now();
    sim_cpu.stop();
    auto cpu_us = std::chrono::duration_cast<Us>(t1 - t0).count();
    float cpu_per_tick = cpu_us / 1000.0f;

    // ── GPU ──
    SimulationBridge sim_gpu;
    sim_gpu.initGpuBackend();
    sim_gpu.setBackend(SimulationBridge::Backend::GPU);
    CHECK(sim_gpu.loadNetwork("/tmp/perf_gpu_cmp.bion"), "GPU loadNetwork failed");

    if (sim_gpu.getBackend() != SimulationBridge::Backend::GPU) {
        printf(" CPU=%.1f µs/t, GPU=SKIP (backend unavailable)", cpu_per_tick);
        PASS();
        return 0;
    }

    sim_gpu.start();
    for (int i = 0; i < 100; i++) sim_gpu.step();
    auto t2 = Clock::now();
    for (int i = 0; i < 1000; i++) sim_gpu.step();
    auto t3 = Clock::now();
    sim_gpu.stop();
    auto gpu_us = std::chrono::duration_cast<Us>(t3 - t2).count();
    float gpu_per_tick = gpu_us / 1000.0f;

    printf(" CPU=%.1f µs/t, GPU=%.1f µs/t (ratio=%.2fx)",
           cpu_per_tick, gpu_per_tick,
           gpu_per_tick > 0 ? cpu_per_tick / gpu_per_tick : 0.0f);

    // GPU не обязательно быстрее на минимальной сети (оверхед переноса),
    // просто проверяем что оба работают
    CHECK(cpu_per_tick < 500.0f, "CPU tick too slow");
    CHECK(gpu_per_tick < 500.0f, "GPU tick too slow");
    PASS();
    return 0;
}

// ═══════════════════════════════════════════════════════════════════

int main() {
    printf("=== Performance Tests ===\n\n");

    int failed = 0;
    failed += bench_generation_early();
    failed += bench_generation_free();
    failed += bench_network_memory();
    failed += bench_save_load_roundtrip();
    failed += bench_simulation_tick();
    failed += bench_gpu_vs_cpu_tick();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (failed > 0) { printf("FAILED: %d test(s)\n", failed); return 1; }
    printf("ALL PERFORMANCE TESTS PASSED\n");
    return 0;
}
