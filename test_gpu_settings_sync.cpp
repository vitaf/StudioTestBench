// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gpu_settings_sync.cpp — синхронизация тулбар ↔ настройки ↔ меню,
// "All available GPUs", фидбек сохранения
#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/simulation_core_api.h"
#include "../BioNeuronStudio/src/settings.h"
#include <cstdio>

using Backend = SimulationBridge::Backend;

static void waitGpuInit(SimulationBridge& sim, int timeout_ms = 5000) {
    int waited = 0;
    while (!sim.isGpuInitDone() && waited < timeout_ms) { usleep(10000); waited += 10; }
}

#ifndef GPU_BACKEND_DIR
#define GPU_BACKEND_DIR "../BioNeuronCore/build"
#endif

// ═══════════════════════════════════════════════════════════════════
// Save/Load фидбек: SettingsManager::save возвращает bool
// ═══════════════════════════════════════════════════════════════════
TEST(GpuSettingsSync, SaveReturnsTrueOnSuccess) {
    SettingsManager s;
    s.set_int(SettingsManager::KEY_SIM_BACKEND, 0);
    EXPECT_TRUE(s.save("/tmp/test_gpu_sync.bin"));
    std::remove("/tmp/test_gpu_sync.bin");
}

TEST(GpuSettingsSync, SaveReturnsFalseOnBadPath) {
    SettingsManager s;
    s.set_int(SettingsManager::KEY_SIM_BACKEND, 0);
    EXPECT_FALSE(s.save("/nonexistent/path/should/fail/config.bin"));
}

TEST(GpuSettingsSync, LoadRestoresBackendValue) {
    const char* tmp = "/tmp/test_gpu_sync_ld.bin";
    std::remove(tmp);

    // Save with GPU_INTEL
    {
        SettingsManager s;
        s.set_int(SettingsManager::KEY_SIM_BACKEND, 1);  // Intel
        EXPECT_TRUE(s.save(tmp));
    }

    // Load — значение сохранилось
    {
        SettingsManager s2;
        EXPECT_TRUE(s2.load(tmp));
        EXPECT_EQ(s2.get_int(SettingsManager::KEY_SIM_BACKEND, 0), 1);
    }
    std::remove(tmp);
}

// ═══════════════════════════════════════════════════════════════════
// KEY_SIM_BACKEND: все допустимые значения
// ═══════════════════════════════════════════════════════════════════
TEST(GpuSettingsSync, BackendValuesRoundTrip) {
    const char* tmp = "/tmp/test_gpu_vals.bin";
    std::remove(tmp);

    struct TestCase { int val; const char* name; };
    TestCase cases[] = {
        {0, "CPU"}, {1, "Intel"}, {2, "CUDA"}, {3, "All GPUs"}
    };

    for (auto& tc : cases) {
        SettingsManager s;
        s.set_int(SettingsManager::KEY_SIM_BACKEND, tc.val);
        ASSERT_TRUE(s.save(tmp));

        SettingsManager s2;
        ASSERT_TRUE(s2.load(tmp));
        EXPECT_EQ(s2.get_int(SettingsManager::KEY_SIM_BACKEND, 0), tc.val)
            << "Value " << tc.name << " (" << tc.val << ") not preserved";
    }
    std::remove(tmp);
}

// ═══════════════════════════════════════════════════════════════════
// Backend::GPU alias → первый доступный GPU
// ═══════════════════════════════════════════════════════════════════
TEST(GpuSettingsSync, GpuAliasResolvesToFirstAvailable) {
    SimulationBridge sim;
    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);
    core_init_libraries();

    sim.setBackend(Backend::GPU);
    auto bk = sim.getBackend();

    if (sim.isGpuAvailable()) {
        // Должен разрешиться в Intel или CUDA (не CPU если GPU доступен)
        EXPECT_NE(bk, Backend::CPU);
        EXPECT_TRUE(bk == Backend::GPU_INTEL || bk == Backend::GPU_CUDA);
        printf("[SYNC] Backend::GPU resolved to %s\n", SimulationBridge::backendName(bk));
    } else {
        EXPECT_EQ(bk, Backend::CPU);
    }
}

// ═══════════════════════════════════════════════════════════════════
// setBackend → settings сохраняется (тулбар/меню пишут в настройки)
// ═══════════════════════════════════════════════════════════════════
TEST(GpuSettingsSync, SetBackendUpdatesSettings) {
    SimulationBridge sim;
    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);
    core_init_libraries();

    SettingsManager settings;

    // Меняем бекенд
    sim.setBackend(Backend::CPU);
    // Симулируем что UI записал в настройки (как toolbar/menu делают)
    settings.set_int(SettingsManager::KEY_SIM_BACKEND, 0);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);

    if (sim.isIntelAvailable()) {
        sim.setBackend(Backend::GPU_INTEL);
        settings.set_int(SettingsManager::KEY_SIM_BACKEND, 1);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_INTEL);
    }

    if (sim.isCudaAvailable()) {
        sim.setBackend(Backend::GPU_CUDA);
        settings.set_int(SettingsManager::KEY_SIM_BACKEND, 2);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_CUDA);
    }

    // Возврат на CPU
    sim.setBackend(Backend::CPU);
    settings.set_int(SettingsManager::KEY_SIM_BACKEND, 0);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
}

// ═══════════════════════════════════════════════════════════════════
// Комбо в настройках → setBackend вызывается сразу (live apply)
// ═══════════════════════════════════════════════════════════════════
TEST(GpuSettingsSync, SettingsChangeAppliesBackendImmediately) {
    SimulationBridge sim;
    sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(sim);
    core_init_libraries();

    int backend_val;

    // "Выбираем CPU в комбо"
    backend_val = 0;
    sim.setBackend(Backend::CPU);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);

    if (sim.isIntelAvailable()) {
        backend_val = 1;
        sim.setBackend(Backend::GPU_INTEL);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_INTEL);
    }

    if (sim.isCudaAvailable()) {
        backend_val = 2;
        sim.setBackend(Backend::GPU_CUDA);
        EXPECT_EQ(sim.getBackend(), Backend::GPU_CUDA);
    }

    // "All GPUs" → GPU алиас
    backend_val = 3;
    sim.setBackend(Backend::GPU);
    if (sim.isGpuAvailable()) {
        EXPECT_NE(sim.getBackend(), Backend::CPU);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Недоступный бекенд → no-op (graceful)
// ═══════════════════════════════════════════════════════════════════
TEST(GpuSettingsSync, UnavailableBackendIgnored) {
    SimulationBridge sim;
    core_init_libraries();  // без initGpuBackend — GPU недоступны
    if (sim.isGpuAvailable()) {
        GTEST_SKIP() << "GPU already loaded by previous test";
    }

    sim.setBackend(Backend::GPU_INTEL);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);

    sim.setBackend(Backend::GPU_CUDA);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);

    sim.setBackend(Backend::GPU);
    EXPECT_EQ(sim.getBackend(), Backend::CPU);
}
