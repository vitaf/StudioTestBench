// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gpu_raw_segment.cpp — читаем raw Segment память на GPU до/после тика
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

TEST(GpuRawSegment, K2pRawMemory) {
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
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/raw_k2p.bion"));

    // GPU load
    SimulationBridge sim;
    sim.initGpuBackend(MINDCopy_ROOT "/BioNeuronCore/build");
    if (!sim.isGpuAvailable()) { GTEST_SKIP(); }
    sim.setBackend(SimulationBridge::Backend::GPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/raw_k2p.bion"));

    std::vector<SegmentSnapshot> before;
    sim.getSegmentsSnapshot(before);
    ASSERT_FALSE(before.empty());

    printf("[RAW] BEFORE tick: V_m=%.6f I_buf=%.6f mod_count=%d mod[0].type=%d mod[0].density=%.4f\n",
           before[0].V_m, before[0].I_buffer, (int)before[0].modifier_count,
           (int)before[0].modifiers[0].type, before[0].modifiers[0].density);
    printf("[RAW] BEFORE: K_i=%.6f K_o=%.6f Na_i=%.6f Na_o=%.6f\n",
           before[0].intra_ions[1], before[0].extra_ions[1],
           before[0].intra_ions[0], before[0].extra_ions[0]);

    // ОДИН тик
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sim.stop();

    std::vector<SegmentSnapshot> after;
    sim.getSegmentsSnapshot(after);
    ASSERT_FALSE(after.empty());

    printf("[RAW] AFTER tick: V_m=%.6f I_buf=%.6f mod_count=%d\n",
           after[0].V_m, after[0].I_buffer, (int)after[0].modifier_count);
    printf("[RAW] AFTER: K_i=%.6f K_o=%.6f Na_i=%.6f Na_o=%.6f\n",
           after[0].intra_ions[1], after[0].extra_ions[1],
           after[0].intra_ions[0], after[0].extra_ions[0]);

    printf("[RAW] dV=%.6f dK_i=%.6f\n",
           after[0].V_m - before[0].V_m,
           after[0].intra_ions[1] - before[0].intra_ions[1]);

    std::remove("/tmp/raw_k2p.bion");
}
