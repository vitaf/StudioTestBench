// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gpu_dual_switch.cpp — переключение бекендов CPU↔Intel↔NVIDIA
#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/simulation_core_api.h"
#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/serialization.h"

using Backend = SimulationBridge::Backend;

static void waitGpuInit(SimulationBridge& sim, int timeout_ms = 5000) {
    int waited = 0;
    while (!sim.isGpuInitDone() && waited < timeout_ms) { usleep(10000); waited += 10; }
}

static NetworkModel make_test_network() {
    NetworkModel m;
    SegmentSnapshot soma;
    soma.id = 0; soma.type = SegmentType::SOMA;
    soma.radius = 5.0f; soma.length = 0.0f;
    soma.position = {0, 0, 0};
    soma.direction = {0, 1, 0};
    soma.intra_ions[0] = 10.0f;  soma.intra_ions[1] = 140.0f;
    soma.intra_ions[2] = 0.0001f; soma.intra_ions[3] = 5.0f; soma.intra_ions[4] = 1.0f;
    soma.extra_ions[0] = 140.0f; soma.extra_ions[1] = 5.0f;
    soma.extra_ions[2] = 2.0f;   soma.extra_ions[3] = 120.0f; soma.extra_ions[4] = 1.0f;
    soma.V_m = -65.0f;
    m.segments.push_back(soma);
    return m;
}

TEST(GpuDualSwitch, CycleCpuIntelNvidiaCpu) {
    SimulationBridge sim;
    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);
    core_init_libraries();

    EXPECT_EQ(sim.getBackend(), Backend::CPU);

    if (sim.isIntelAvailable()) {
        sim.setBackend(Backend::GPU_INTEL);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_INTEL);
    }

    if (sim.isCudaAvailable()) {
        sim.setBackend(Backend::GPU_CUDA);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_CUDA);
    }

    sim.setBackend(Backend::CPU);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
}

TEST(GpuDualSwitch, LegacyGpuAlias) {
    SimulationBridge sim;
    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);
    core_init_libraries();

    sim.setBackend(Backend::GPU);
    auto bk = sim.getBackend();
    if (sim.isGpuAvailable()) {
        EXPECT_TRUE(bk == Backend::GPU_INTEL || bk == Backend::GPU_CUDA);
    } else {
        EXPECT_EQ(bk, Backend::CPU);
    }
}

TEST(GpuDualSwitch, UnavailableBackendIsNoOp) {
    SimulationBridge sim;
    core_init_libraries();
    // GPU availability is static (set by previous tests). If GPUs were loaded,
    // they're already available. Without initGpuBackend(), this instance
    // shouldn't have local GPU libs but static state still reports availability.
    if (sim.isGpuAvailable()) {
        GTEST_SKIP() << "GPU already loaded by previous test";
    }

    sim.setBackend(Backend::GPU_INTEL);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
    sim.setBackend(Backend::GPU_CUDA);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
}

TEST(GpuDualSwitch, SwitchWithoutNetwork) {
    SimulationBridge sim;
    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);
    core_init_libraries();

    sim.setBackend(Backend::CPU);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
    if (sim.isIntelAvailable()) {
        sim.setBackend(Backend::GPU_INTEL);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_INTEL);
    }
    sim.setBackend(Backend::CPU);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
}

TEST(GpuDualSwitch, SimulateOnCpuAfterSwitch) {
    auto model = make_test_network();
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/dual_switch.bion"));

    SimulationBridge sim;
    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);
    core_init_libraries();

    // Покрутили бекенды — вернулись на CPU — симуляция работает
    if (sim.isIntelAvailable()) sim.setBackend(Backend::GPU_INTEL);
    if (sim.isCudaAvailable())  sim.setBackend(Backend::GPU_CUDA);
    sim.setBackend(Backend::CPU);

    ASSERT_TRUE(sim.loadNetwork("/tmp/dual_switch.bion"));
    sim.start(); usleep(100000); sim.stop();  // 100ms для надёжного старта sim-потока
    EXPECT_GT(sim.getCurrentTick(), 0u);

    std::remove("/tmp/dual_switch.bion");
}
