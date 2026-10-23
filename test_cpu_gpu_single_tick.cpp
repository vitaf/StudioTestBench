// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_cpu_gpu_single_tick.cpp — single-tick CPU vs GPU сравнение I_buffer/V_m/ионов

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
#define GPU_BACKEND_DIR MINDCopy_ROOT "/BioNeuronCore/build"

static void waitGpuInit(SimulationBridge& sim, int timeout_ms = 5000) {
    int waited = 0;
    while (!sim.isGpuInitDone() && waited < timeout_ms) {
        usleep(10000); waited += 10;
    }
}

// Single soma + Cl⁻ leak. Сравниваем CPU vs GPU — изоляция бага GpuComplex.
TEST(CpuGpuSingleTick, ClLeakExactCurrent) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = -70.0f; s.prev_V_m = -70.0f;
    // Cl⁻: внутри 5mM, снаружи 130mM → E_Cl ≈ -87mV
    s.intra_ions[0] = 0.015f;  s.intra_ions[1] = 0.140f;
    s.intra_ions[2] = 0.0001f; s.intra_ions[3] = 0.005f;  // [Cl⁻]i=5mM
    s.extra_ions[0] = 0.140f;  s.extra_ions[1] = 0.005f;
    s.extra_ions[2] = 0.002f;  s.extra_ions[3] = 0.130f;  // [Cl⁻]o=130mM
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);  // NaK_HH для стабильности
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);        // NaK-насос
    s.add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);
    m.segments.push_back(s);
    m.neurons.push_back({0, 0, 1, 0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/st_cl.bion"));

    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/st_cl.bion"));
    cpu_sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cpu_sim.stop();
    std::vector<SegmentSnapshot> cpu_segs;
    cpu_sim.getSegmentsSnapshot(cpu_segs);
    ASSERT_FALSE(cpu_segs.empty());
    printf("[CL-CPU] V_m=%.4f I_buf=%.6f Cl_i=%.6f Cl_o=%.6f\n",
           cpu_segs[0].V_m, cpu_segs[0].I_buffer,
           cpu_segs[0].intra_ions[3], cpu_segs[0].extra_ions[3]);

    SimulationBridge gpu_sim;
    gpu_sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(gpu_sim);
    if (!gpu_sim.isGpuAvailable()) { GTEST_SKIP() << "GPU not available"; }
    gpu_sim.setBackend(SimulationBridge::Backend::GPU);
    ASSERT_TRUE(gpu_sim.loadNetwork("/tmp/st_cl.bion"));
    gpu_sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    gpu_sim.stop();
    std::vector<SegmentSnapshot> gpu_segs;
    gpu_sim.getSegmentsSnapshot(gpu_segs);
    ASSERT_FALSE(gpu_segs.empty());
    printf("[CL-GPU] V_m=%.4f I_buf=%.6f Cl_i=%.6f Cl_o=%.6f\n",
           gpu_segs[0].V_m, gpu_segs[0].I_buffer,
           gpu_segs[0].intra_ions[3], gpu_segs[0].extra_ions[3]);

    float dV = fabsf(cpu_segs[0].V_m - gpu_segs[0].V_m);
    printf("[CL-PARITY] dV=%.4f mV\n", dV);
    // Био: Cl⁻ канал гиперполяризует (E_Cl=-87mV, V_m=-70mV → Cl⁻ influx)
    // NaK_HH + Pump держат V_rest ~-65..-70mV. Cl⁻ двигает к -87mV.
    EXPECT_LT(cpu_segs[0].V_m, -65.0f) << "CPU Cl⁻ должен гиперполяризовать (<-65mV)";
    EXPECT_LT(gpu_segs[0].V_m, -65.0f) << "GPU Cl⁻ должен гиперполяризовать (<-65mV)";
    EXPECT_GT(gpu_segs[0].V_m, -200.0f) << "GPU Cl⁻: V_m в физиологическом диапазоне";
    EXPECT_LT(dV, 15.0f) << "CPU/GPU Cl⁻ delta=" << dV << "mV (max био 15mV)";
    std::remove("/tmp/st_cl.bion");
}

// Single soma + K2P. Прогоняем ОДИН тик на CPU и GPU, сравниваем всё.
TEST(CpuGpuSingleTick, K2pExactCurrent) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = -65.0f; s.prev_V_m = -65.0f;
    s.intra_ions[0] = 0.015f;  s.intra_ions[1] = 0.140f;
    s.intra_ions[2] = 0.00005f; s.intra_ions[3] = 0.005f;
    s.extra_ions[0] = 0.140f;  s.extra_ions[1] = 0.005f;
    s.extra_ions[2] = 0.0012f; s.extra_ions[3] = 0.130f;
    s.add_modifier(ModifierType::MOD_K2P_LEAK, 0.03f);
    m.segments.push_back(s);
    m.neurons.push_back({0, 0, 1, 0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/st_k2p.bion"));

    // ── CPU: один тик ──
    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/st_k2p.bion"));
    cpu_sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 500 тиков для наблюдаемого эффекта
    cpu_sim.stop();
    std::vector<SegmentSnapshot> cpu_segs;
    cpu_sim.getSegmentsSnapshot(cpu_segs);
    ASSERT_FALSE(cpu_segs.empty());
    printf("[SINGLE] CPU: V_m=%.4f I_buf=%.6f K_i=%.6f K_o=%.6f\n",
           cpu_segs[0].V_m, cpu_segs[0].I_buffer,
           cpu_segs[0].intra_ions[1], cpu_segs[0].extra_ions[1]);

    // ── GPU: один тик ──
    SimulationBridge gpu_sim;
    gpu_sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(gpu_sim);
    if (!gpu_sim.isGpuAvailable()) { GTEST_SKIP() << "GPU not available"; }
    gpu_sim.setBackend(SimulationBridge::Backend::GPU);
    ASSERT_TRUE(gpu_sim.loadNetwork("/tmp/st_k2p.bion"));
    gpu_sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50000));
    gpu_sim.stop();
    std::vector<SegmentSnapshot> gpu_segs;
    gpu_sim.getSegmentsSnapshot(gpu_segs);
    ASSERT_FALSE(gpu_segs.empty());
    printf("[SINGLE] GPU: V_m=%.4f I_buf=%.6f K_i=%.6f K_o=%.6f\n",
           gpu_segs[0].V_m, gpu_segs[0].I_buffer,
           gpu_segs[0].intra_ions[1], gpu_segs[0].extra_ions[1]);

    float dv_cpu = cpu_segs[0].V_m - (-65.0f);
    float dv_gpu = gpu_segs[0].V_m - (-65.0f);
    printf("[SINGLE] dV: CPU=%.6f GPU=%.6f ratio=%.3f\n",
           dv_cpu, dv_gpu, (dv_cpu != 0 ? dv_gpu/dv_cpu : 0));

    // Нестрогая проверка — просто диагностика
    EXPECT_NE(cpu_segs[0].V_m, -65.0f) << "CPU: V_m должен измениться за тик";
    EXPECT_NE(gpu_segs[0].V_m, -65.0f) << "GPU: V_m должен измениться за тик";

    std::remove("/tmp/st_k2p.bion");
}

// NaK_HH + Pump single tick
TEST(CpuGpuSingleTick, NaKHhPumpExactCurrent) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = -65.0f; s.prev_V_m = -65.0f;
    s.intra_ions[0] = 0.015f;  s.intra_ions[1] = 0.140f;
    s.intra_ions[2] = 0.00005f; s.intra_ions[3] = 0.005f;
    s.extra_ions[0] = 0.140f;  s.extra_ions[1] = 0.005f;
    s.extra_ions[2] = 0.0012f; s.extra_ions[3] = 0.130f;
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    m.segments.push_back(s);
    m.neurons.push_back({0, 0, 1, 0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/st_nak.bion"));

    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/st_nak.bion"));
    cpu_sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cpu_sim.stop();
    std::vector<SegmentSnapshot> cpu_segs;
    cpu_sim.getSegmentsSnapshot(cpu_segs);
    printf("[SINGLE] CPU NaK: V_m=%.4f I_buf=%.6f K_i=%.6f K_o=%.6f Na_i=%.6f\n",
           cpu_segs[0].V_m, cpu_segs[0].I_buffer,
           cpu_segs[0].intra_ions[1], cpu_segs[0].extra_ions[1],
           cpu_segs[0].intra_ions[0]);

    SimulationBridge gpu_sim;
    gpu_sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(gpu_sim);
    if (!gpu_sim.isGpuAvailable()) { GTEST_SKIP(); }
    gpu_sim.setBackend(SimulationBridge::Backend::GPU);
    ASSERT_TRUE(gpu_sim.loadNetwork("/tmp/st_nak.bion"));
    gpu_sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    gpu_sim.stop();
    std::vector<SegmentSnapshot> gpu_segs;
    gpu_sim.getSegmentsSnapshot(gpu_segs);
    printf("[SINGLE] GPU NaK: V_m=%.4f I_buf=%.6f K_i=%.6f K_o=%.6f Na_i=%.6f\n",
           gpu_segs[0].V_m, gpu_segs[0].I_buffer,
           gpu_segs[0].intra_ions[1], gpu_segs[0].extra_ions[1],
           gpu_segs[0].intra_ions[0]);

    std::remove("/tmp/st_nak.bion");
}
