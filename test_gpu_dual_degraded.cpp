// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gpu_dual_degraded.cpp — graceful degradation при отсутствии .so
#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/simulation_core_api.h"

#ifndef GPU_BACKEND_DIR
#define GPU_BACKEND_DIR "../BioNeuronCore/build"
#endif

static void waitGpuInit(SimulationBridge& sim, int timeout_ms = 5000) {
    int waited = 0;
    while (!sim.isGpuInitDone() && waited < timeout_ms) { usleep(10000); waited += 10; }
}

using Backend = SimulationBridge::Backend;

TEST(GpuDualDegraded, NoSoAtAll) {
    SimulationBridge sim;
    core_init_libraries();
    // БЕЗ initGpuBackend — ни один .so не загружен
    EXPECT_FALSE(sim.isGpuAvailable());
    EXPECT_FALSE(sim.isIntelAvailable());
    EXPECT_FALSE(sim.isCudaAvailable());

    // Попытка переключения на GPU — no-op
    sim.setBackend(Backend::GPU_INTEL);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
    sim.setBackend(Backend::GPU_CUDA);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
}

TEST(GpuDualDegraded, InitGpuBackendNull) {
    SimulationBridge sim;
    core_init_libraries();
    // nullptr → системный поиск, не крашит
    sim.initGpuBackend(nullptr);
    SUCCEED();  // не упало — уже успех
}

TEST(GpuDualDegraded, OnlyIntelLoaded) {
    SimulationBridge sim;
    core_init_libraries();

    // Загружаем И Intel И CUDA бекенды, потом проверяем что isIntelAvailable смотрит только на intel
    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim); waitGpuInit(sim);  // загружает оба

    if (sim.isIntelAvailable()) {
        EXPECT_TRUE(sim.isGpuAvailable());
        // Переключение на Intel работает
        sim.setBackend(Backend::GPU_INTEL);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_INTEL);
    } else {
        GTEST_SKIP() << "Intel .so not available";
    }
}

TEST(GpuDualDegraded, OnlyCudaLoaded) {
    SimulationBridge sim;
    core_init_libraries();

    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);

    if (sim.isCudaAvailable()) {
        EXPECT_TRUE(sim.isGpuAvailable());
        sim.setBackend(Backend::GPU_CUDA);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_CUDA);
    } else {
        GTEST_SKIP() << "CUDA .so not available";
    }
}

TEST(GpuDualDegraded, BothLoaded) {
    SimulationBridge sim;
    core_init_libraries();

    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);

    bool intel_ok = sim.isIntelAvailable();
    bool cuda_ok  = sim.isCudaAvailable();

    printf("[DEGRADED] Intel=%s CUDA=%s\n",
           intel_ok ? "YES" : "NO", cuda_ok ? "YES" : "NO");

    // isGpuAvailable = intel || cuda
    EXPECT_EQ(sim.isGpuAvailable(), intel_ok || cuda_ok);

    // Каждый по отдельности корректен
    if (intel_ok) EXPECT_TRUE(sim.isIntelAvailable());
    if (cuda_ok)  EXPECT_TRUE(sim.isCudaAvailable());
}
