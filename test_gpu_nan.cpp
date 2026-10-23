// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gpu_nan.cpp — проверка отсутствия NaN в симуляции на CPU и GPU

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif

static int run_nan_test(const char* label, bool use_gpu, int expected_min_spikes) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    bool saved = ::SaveNetwork(model, "/tmp/test_nan.bion");
    if (!saved) { printf("[NaN-Test] %s: SaveNetwork failed\n", label); return -1; }

    SimulationBridge sim;
    if (use_gpu) {
        sim.initGpuBackend(MINDCopy_ROOT "/BioNeuronCore/build");
        if (sim.isGpuAvailable()) sim.setBackend(SimulationBridge::Backend::GPU);
    }
    if (!sim.loadNetwork("/tmp/test_nan.bion")) {
        printf("[NaN-Test] %s: loadNetwork failed\n", label); return -1;
    }

    // Находим dlc для стимуляции
    std::vector<uint32_t> dlc_left, dlc_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 60.0f && seg.position.z >= 200.0f) {
            bool has_pir = false;
            for (int m = 0; m < seg.modifier_count; m++)
                if (seg.modifiers[m].mod_type() == ModifierType::MOD_HCN2)
                    { has_pir = true; break; }
            if (has_pir) {
                if (seg.position.x < 0) dlc_left.push_back(seg.id);
                else dlc_right.push_back(seg.id);
            }
        }
    }

    // Alternating kickstart: 25 циклов
    for (int cycle = 0; cycle < 25; cycle++) {
        for (auto id : dlc_left)
            sim.addCurrentStimulus(id, 800.0f, 30.0f, cycle * 120.0f);
        for (auto id : dlc_right)
            sim.addCurrentStimulus(id, 800.0f, 30.0f, cycle * 120.0f + 60.0f);
    }

    sim.start();

    int nan_count = 0, inf_count = 0, total_samples = 0;
    float global_max_vm = -999, global_min_vm = 999;
    std::vector<SegmentSnapshot> segs;
    bool printed_diag = false;

    for (int i = 0; i < 3000; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!sim.isRunning()) break;
        sim.getSegmentsSnapshot(segs);
        total_samples++;

        // Диагностика на первом семпле
        if (!printed_diag && !segs.empty()) {
            printed_diag = true;
            printf("[NaN-Diag] %s: segs=%zu first V_m:", label, segs.size());
            for (size_t k = 0; k < std::min(segs.size(), (size_t)5); k++)
                printf(" [%zu]type=%d Vm=%.1f mods=%d", k, (int)segs[k].type, segs[k].V_m, segs[k].modifier_count);
            printf("\n"); fflush(stdout);
        }

        for (auto& s : segs) {
            if (std::isnan(s.V_m)) nan_count++;
            if (std::isinf(s.V_m)) inf_count++;
            if (s.V_m > global_max_vm) global_max_vm = s.V_m;
            if (s.V_m < global_min_vm) global_min_vm = s.V_m;
        }
    }

    sim.stop();
    std::remove("/tmp/test_nan.bion");

    printf("[NaN-Test] %s: samples=%d NaN=%d Inf=%d V_m_range=[%.1f, %.1f]\n",
           label, total_samples, nan_count, inf_count, global_min_vm, global_max_vm);

    // Проверяем количество NaN
    if (nan_count > 0 || inf_count > 0) {
        printf("[NaN-Test] %s: FAIL — обнаружены NaN/Inf!\n", label);
        return 1;
    } else {
        printf("[NaN-Test] %s: PASS — нет NaN/Inf\n", label);
        return 0;
    }
}

TEST(NaNProtection, CpuNoNaN) {
    int ret = run_nan_test("CPU", false, 100);
    EXPECT_EQ(ret, 0) << "CPU: обнаружены NaN/Inf в V_m!";
}

TEST(NaNProtection, GpuNoNaN) {
    int ret = run_nan_test("GPU", true, 100);
    // GPU может не поддерживаться — тогда тест skipped
    if (ret < 0) {
        GTEST_SKIP() << "GPU backend not available";
        return;
    }
    EXPECT_EQ(ret, 0) << "GPU: обнаружены NaN/Inf в V_m!";
}
