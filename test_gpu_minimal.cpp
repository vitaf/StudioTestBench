// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gpu_minimal.cpp — минимальная сеть на GPU: проверка вычисления токов

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif

TEST(GpuDebug, MinimalNetworkVMRest) {
    NetworkModel model;
    SegmentSnapshot soma;
    soma.id = 0; soma.neuron_id = 0; soma.type = SegmentType::SOMA;
    soma.position = {0, 50, 0}; soma.direction = {0, 1, 0};
    soma.radius = 5.0f; soma.length = 10.0f; soma.V_m = -65.0f; soma.prev_V_m = -65.0f;
    soma.intra_ions[0] = 0.015f; soma.intra_ions[1] = 0.140f;
    soma.intra_ions[2] = 0.00005f; soma.intra_ions[3] = 0.005f;
    soma.extra_ions[0] = 0.140f; soma.extra_ions[1] = 0.005f;
    soma.extra_ions[2] = 0.0012f; soma.extra_ions[3] = 0.130f;
    soma.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    soma.add_modifier(ModifierType::MOD_K2P_LEAK, 0.03f);
    soma.add_modifier(ModifierType::MOD_KIR_CHANNEL, 0.008f);
    soma.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    model.segments.push_back(soma);
    model.neurons.push_back({0, 0, 1, 0});

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_gpu_min.bion"));

    SimulationBridge sim;
    sim.initGpuBackend(MINDCopy_ROOT "/BioNeuronCore/build");
    if (sim.isGpuAvailable()) {
        sim.setBackend(SimulationBridge::Backend::GPU);
        printf("[MinGPU] Using GPU\n");
    } else {
        printf("[MinGPU] GPU not available, using CPU\n");
    }
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_gpu_min.bion"));
    sim.start();

    std::vector<SegmentSnapshot> segs;
    float sum_vm = 0, min_vm = 99, max_vm = -99;
    int samples = 0;
    for (int i = 0; i < 500; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!sim.isRunning()) break;
        sim.getSegmentsSnapshot(segs);
        for (auto& s : segs) {
            sum_vm += s.V_m; samples++;
            if (s.V_m < min_vm) min_vm = s.V_m;
            if (s.V_m > max_vm) max_vm = s.V_m;
        }
    }
    sim.stop();

    float avg_vm = samples > 0 ? sum_vm / samples : -99;
    printf("[MinGPU] V_m: avg=%.2f min=%.2f max=%.2f (init=-65.0) samples=%d\n", avg_vm, min_vm, max_vm, samples);
    EXPECT_NE(avg_vm, -65.0f) << "V_m не изменился — токи не вычисляются!";
    EXPECT_FALSE(std::isnan(avg_vm)) << "V_m = NaN!";
}
