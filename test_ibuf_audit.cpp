// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_ibuf_audit.cpp — поканальный аудит I_buffer: CPU vs GPU на одном тике
// Измеряет фактические токи, conductance, V_m для поиска ~5x расхождения.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
#define GPU_BACKEND_DIR MINDCopy_ROOT "/BioNeuronCore/build"

// Глобальный GPU-мост (живёт всё время тестов — нет use-after-free от detach)
static SimulationBridge g_gpu_bridge;
static bool g_gpu_available = false;
static bool g_gpu_inited = false;
static bool init_global_gpu() {
    if (!g_gpu_inited) {
        g_gpu_inited = true;
        g_gpu_bridge.initGpuBackend(GPU_BACKEND_DIR);
        g_gpu_available = g_gpu_bridge.isGpuAvailable();
    }
    return g_gpu_available;
}

// Один нейрон с заданными модификаторами, замер V_m после 1 тика.
struct TickResult { float vm_after; float delta_vm; float vm_stable; int samples; };

static TickResult measure_one_tick(SimulationBridge& sim, const char* label) {
    std::vector<SegmentSnapshot> segs;
    uint32_t tick0 = sim.getCurrentTick();
    sim.getSegmentsSnapshot(segs);
    float vm_before = segs.empty() ? -99 : segs[0].V_m;

    // Ждём чтобы симуляция сделала несколько тиков
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sim.getSegmentsSnapshot(segs);
    float vm_after = segs.empty() ? -99 : segs[0].V_m;

    // Собираем ещё несколько измерений для стабильности
    float sum = 0; int n = 0;
    for (int i = 0; i < 10; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sim.getSegmentsSnapshot(segs);
        if (!segs.empty()) { sum += segs[0].V_m; n++; }
    }
    float vm_stable = n > 0 ? sum / n : -99;

    uint32_t ticks = sim.getCurrentTick() - tick0;
    printf("[AUDIT] %-12s: ticks=%u Vm_0=%.2f Vm_1=%.2f Vm_st=%.2f dV=%.3f\n", label, ticks,
           vm_before, vm_after, vm_stable, vm_stable - vm_before);
    return {vm_after, vm_stable - vm_before, vm_stable, n};
}

// ── Тест 1: K2P_LEAK изолированно ──
TEST(IbufAudit, K2pLeakAlone) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = -65.0f; s.prev_V_m = -65.0f;
    // Стандартные концентрации
    s.intra_ions[0] = 15.0f;  s.intra_ions[1] = 140.0f;  // Na, K
    s.intra_ions[2] = 0.0001f; s.intra_ions[3] = 5.0f;  // Ca, Cl
    s.extra_ions[0] = 140.0f;  s.extra_ions[1] = 5.0f;
    s.extra_ions[2] = 1.2f; s.extra_ions[3] = 130.0f;
    s.add_modifier(ModifierType::MOD_K2P_LEAK, 0.03f);  // как в debug сети
    m.segments.push_back(s);
    m.neurons.push_back({0, 0, 1, 0});

    ASSERT_TRUE(::SaveNetwork(m, "/tmp/audit_k2p.bion"));

    // CPU
    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/audit_k2p.bion"));
    cpu_sim.start();
    TickResult cpu_r = measure_one_tick(cpu_sim, "K2P_CPU");
    cpu_sim.stop();
    EXPECT_NE(cpu_r.vm_stable, -65.0f) << "CPU: V_m не изменился!";

    // GPU
    bool gpu_ok = false;
    if (init_global_gpu()) {
        g_gpu_bridge.setBackend(SimulationBridge::Backend::GPU);
        ASSERT_TRUE(g_gpu_bridge.loadNetwork("/tmp/audit_k2p.bion"));
        g_gpu_bridge.start();
        TickResult gpu_r = measure_one_tick(g_gpu_bridge, "K2P_GPU");
        g_gpu_bridge.stop();
        gpu_ok = true;

        float ratio = (cpu_r.delta_vm != 0) ? gpu_r.delta_vm / cpu_r.delta_vm : -1;
        printf("[AUDIT] K2P GPU/CPU dV ratio: %.3f\n", ratio);
        // Ожидаем что dV одного знака и в разумных пределах
        EXPECT_GT(gpu_r.delta_vm * cpu_r.delta_vm, 0) << "K2P: CPU и GPU dV разного знака!";
    } else {
        printf("[AUDIT] K2P GPU: SKIP (not available)\n");
    }
    std::remove("/tmp/audit_k2p.bion");
}

// ── Тест 2: NaK_HH + Pump изолированно ──
TEST(IbufAudit, NakHhWithPump) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = -65.0f; s.prev_V_m = -65.0f;
    s.intra_ions[0] = 15.0f;  s.intra_ions[1] = 140.0f;
    s.intra_ions[2] = 0.0001f; s.intra_ions[3] = 5.0f;
    s.extra_ions[0] = 140.0f;  s.extra_ions[1] = 5.0f;
    s.extra_ions[2] = 1.2f; s.extra_ions[3] = 130.0f;
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    m.segments.push_back(s);
    m.neurons.push_back({0, 0, 1, 0});

    ASSERT_TRUE(::SaveNetwork(m, "/tmp/audit_nak.bion"));

    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/audit_nak.bion"));
    cpu_sim.start();
    TickResult cpu_r = measure_one_tick(cpu_sim, "NaK_CPU");
    cpu_sim.stop();

    if (init_global_gpu()) {
        g_gpu_bridge.setBackend(SimulationBridge::Backend::GPU);
        ASSERT_TRUE(g_gpu_bridge.loadNetwork("/tmp/audit_nak.bion"));
        g_gpu_bridge.start();
        TickResult gpu_r = measure_one_tick(g_gpu_bridge, "NaK_GPU");
        g_gpu_bridge.stop();

        float ratio = (cpu_r.delta_vm != 0) ? gpu_r.delta_vm / cpu_r.delta_vm : -1;
        printf("[AUDIT] NaK_HH GPU/CPU dV ratio: %.3f\n", ratio);
        EXPECT_NEAR(ratio, 1.0f, 0.9f) << "NaK_HH: GPU/CPU dV ratio слишком далёк от 1.0!";
    }
    std::remove("/tmp/audit_nak.bion");
}

// ── Тест 3: KIR изолированно ──
TEST(IbufAudit, KirAlone) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = -65.0f; s.prev_V_m = -65.0f;
    s.intra_ions[0] = 15.0f;  s.intra_ions[1] = 140.0f;
    s.intra_ions[2] = 0.0001f; s.intra_ions[3] = 5.0f;
    s.extra_ions[0] = 140.0f;  s.extra_ions[1] = 5.0f;
    s.extra_ions[2] = 1.2f; s.extra_ions[3] = 130.0f;
    s.add_modifier(ModifierType::MOD_KIR_CHANNEL, 0.008f);
    m.segments.push_back(s);
    m.neurons.push_back({0, 0, 1, 0});

    ASSERT_TRUE(::SaveNetwork(m, "/tmp/audit_kir.bion"));

    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/audit_kir.bion"));
    cpu_sim.start();
    TickResult cpu_r = measure_one_tick(cpu_sim, "KIR_CPU");
    cpu_sim.stop();

    if (init_global_gpu()) {
        g_gpu_bridge.setBackend(SimulationBridge::Backend::GPU);
        ASSERT_TRUE(g_gpu_bridge.loadNetwork("/tmp/audit_kir.bion"));
        g_gpu_bridge.start();
        TickResult gpu_r = measure_one_tick(g_gpu_bridge, "KIR_GPU");
        g_gpu_bridge.stop();

        float ratio = (cpu_r.delta_vm != 0) ? gpu_r.delta_vm / cpu_r.delta_vm : -1;
        printf("[AUDIT] KIR GPU/CPU dV ratio: %.3f\n", ratio);
        EXPECT_NEAR(ratio, 1.0f, 0.9f) << "KIR: GPU/CPU dV ratio слишком далёк от 1.0!";
    }
    std::remove("/tmp/audit_kir.bion");
}

// ── Тест 4: K2P + NaK_HH + Pump + KIR — полный набор покоя ──
TEST(IbufAudit, FullRestingSet) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = -65.0f; s.prev_V_m = -65.0f;
    s.intra_ions[0] = 15.0f; s.intra_ions[1] = 140.0f;
    s.intra_ions[2] = 0.0001f; s.intra_ions[3] = 5.0f;
    s.extra_ions[0] = 140.0f; s.extra_ions[1] = 5.0f;
    s.extra_ions[2] = 1.2f; s.extra_ions[3] = 130.0f;
    s.add_modifier(ModifierType::MOD_K2P_LEAK, 0.03f);
    s.add_modifier(ModifierType::MOD_KIR_CHANNEL, 0.008f);
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    m.segments.push_back(s);
    m.neurons.push_back({0, 0, 1, 0});

    ASSERT_TRUE(::SaveNetwork(m, "/tmp/audit_full.bion"));

    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/audit_full.bion"));
    cpu_sim.start();
    TickResult cpu_r = measure_one_tick(cpu_sim, "Full_CPU");
    cpu_sim.stop();

    if (init_global_gpu()) {
        g_gpu_bridge.setBackend(SimulationBridge::Backend::GPU);
        ASSERT_TRUE(g_gpu_bridge.loadNetwork("/tmp/audit_full.bion"));
        g_gpu_bridge.start();
        TickResult gpu_r = measure_one_tick(g_gpu_bridge, "Full_GPU");
        g_gpu_bridge.stop();

        float ratio = (cpu_r.delta_vm != 0) ? gpu_r.delta_vm / cpu_r.delta_vm : -1;
        printf("[AUDIT] Full set GPU/CPU dV ratio: %.3f\n", ratio);
        // Проверяем направление (оба должны деполяризовать или гиперполяризовать)
        EXPECT_GT(gpu_r.delta_vm * cpu_r.delta_vm, 0) << "Full: CPU и GPU dV разного знака!";
    }
    std::remove("/tmp/audit_full.bion");
}
