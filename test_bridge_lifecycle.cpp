// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_bridge_lifecycle.cpp — тесты жизненного цикла SimulationBridge.
// Проверяет: конструктор/деструктор, loadNetwork (valid/invalid),
// start/stop/step, setBackend, getSegmentsSnapshot,
// saveCheckpoint/loadCheckpoint roundtrip.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/simulation_bridge.h"

// ═══════════════════════════════════════════════════════════════════
// Хелпер: запись валидного .bion v5 (без Core хедеров — Vec3 конфликт).
// Структуры: NetworkHeader(84) + Neuron(24) + Segment(988) + Nucleus(204)
// ═══════════════════════════════════════════════════════════════════

static std::string write_valid_bion(const char* name) {
    std::string path = std::string("/tmp/") + name + ".bion";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "";

    auto w32 = [](unsigned char* buf, size_t off, uint32_t v) { memcpy(buf + off, &v, 4); };
    auto wf  = [](unsigned char* buf, size_t off, float v)    { memcpy(buf + off, &v, 4); };

    // ── NetworkHeader: 84 bytes ──
    unsigned char hdr[84] = {};
    w32(hdr, 0,  0x42494F4E);    // magic
    w32(hdr, 4,  5);             // version
    // mode=0, diffusion_type=0, reserved=0
    w32(hdr, 12, 1);             // num_neurons
    w32(hdr, 16, 1);             // num_segments
    // num_intra_connections=0, num_synapses=0
    w32(hdr, 68, 1);             // num_nuclei (offset 68)
    w32(hdr, 72, 1);             // nucleus_version (offset 72)
    fwrite(hdr, 1, 84, f);

    // ── Neuron: 24 bytes ──
    unsigned char neuron[24] = {};
    w32(neuron, 0, 1);           // id
    w32(neuron, 8, 1);           // segment_count
    wf(neuron, 16, -20.0f);      // threshold
    fwrite(neuron, 1, 24, f);

    // ── Segment: 988 bytes ──
    // Layout: id(4) + neuron_id(4) + pos(12) + dir(12) + len(4) + rad(4) + V_m(4) + ...
    unsigned char seg[988] = {};
    w32(seg, 0, 1);              // id = 1
    w32(seg, 4, 1);              // neuron_id = 1
    wf(seg, 24, 1.0f);           // direction.y = 1.0 (offset 8+12+4)
    wf(seg, 32, 10.0f);          // length    = 10.0 (offset 32)
    wf(seg, 36, 1.0f);           // radius    = 1.0  (offset 36)
    wf(seg, 40, -65.0f);         // V_m       = -65.0 (offset 40)
    fwrite(seg, 1, 988, f);

    // ── Nucleus: 204 bytes ──
    unsigned char nucleus[204] = {};
    w32(nucleus, 0, 1);          // neuron_id
    nucleus[4] = 1;              // enabled
    nucleus[5] = 3;              // consolidation_mode
    wf(nucleus, 196, 0.5f);     // amino_acid_level (offset 196)
    fwrite(nucleus, 1, 204, f);

    fclose(f);
    return path;
}

// ═══════════════════════════════════════════════════════════════════
// Тесты
// ═══════════════════════════════════════════════════════════════════

TEST(BridgeLifecycle, ConstructDestruct) {
    {
        SimulationBridge bridge;
        EXPECT_FALSE(bridge.isNetworkLoaded());
        EXPECT_FALSE(bridge.isRunning());
        EXPECT_EQ(bridge.getCurrentTick(), 0u);
        EXPECT_EQ(bridge.getBackend(), SimulationBridge::Backend::CPU);
    }
}

TEST(BridgeLifecycle, LoadValidNetwork) {
    SimulationBridge bridge;
    std::string path = write_valid_bion("lifecycle_valid");
    ASSERT_FALSE(path.empty());

    EXPECT_TRUE(bridge.loadNetwork(path.c_str()));
    EXPECT_TRUE(bridge.isNetworkLoaded());

    remove(path.c_str());
}

TEST(BridgeLifecycle, LoadNonexistentFileFails) {
    SimulationBridge bridge;
    EXPECT_FALSE(bridge.loadNetwork("/tmp/nonexistent_bridge_lifecycle_test.bion"));
    EXPECT_FALSE(bridge.isNetworkLoaded());
}

TEST(BridgeLifecycle, LoadNullptrDoesNotCrash) {
    SimulationBridge bridge;
    EXPECT_FALSE(bridge.loadNetwork(nullptr));
    EXPECT_FALSE(bridge.isNetworkLoaded());
}

TEST(BridgeLifecycle, StartStopStep) {
    SimulationBridge bridge;
    std::string path = write_valid_bion("lifecycle_startstop");
    ASSERT_FALSE(path.empty());
    ASSERT_TRUE(bridge.loadNetwork(path.c_str()));

    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_TRUE(bridge.isRunning());

    bridge.stop();
    EXPECT_FALSE(bridge.isRunning());
    EXPECT_FALSE(bridge.isPaused());

    bridge.step();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    bridge.stop();

    remove(path.c_str());
}

TEST(BridgeLifecycle, PauseResume) {
    SimulationBridge bridge;
    std::string path = write_valid_bion("lifecycle_pause");
    ASSERT_FALSE(path.empty());
    ASSERT_TRUE(bridge.loadNetwork(path.c_str()));

    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bridge.pause();
    EXPECT_TRUE(bridge.isPaused());

    bridge.pause();
    EXPECT_FALSE(bridge.isPaused());

    bridge.stop();
    remove(path.c_str());
}

TEST(BridgeLifecycle, GetSegmentsSnapshot) {
    SimulationBridge bridge;
    std::string path = write_valid_bion("lifecycle_snapshot");
    ASSERT_FALSE(path.empty());
    ASSERT_TRUE(bridge.loadNetwork(path.c_str()));

    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    bridge.stop();

    std::vector<SegmentSnapshot> segs;
    bridge.getSegmentsSnapshot(segs);
    EXPECT_GE(segs.size(), 1u);
    if (!segs.empty()) {
        EXPECT_EQ(segs[0].id, 1u);
        EXPECT_NEAR(segs[0].V_m, -65.0f, 15.0f);
    }

    remove(path.c_str());
}

TEST(BridgeLifecycle, CheckpointRoundtrip) {
    SimulationBridge bridge;
    std::string path = write_valid_bion("lifecycle_checkpoint");
    ASSERT_FALSE(path.empty());
    ASSERT_TRUE(bridge.loadNetwork(path.c_str()));

    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    bridge.stop();
    uint32_t tick = bridge.getCurrentTick();
    EXPECT_GT(tick, 0u);

    const char* ckpt_path = "/tmp/lifecycle_checkpoint_ckpt.bion";
    EXPECT_TRUE(bridge.saveCheckpoint(ckpt_path));

    SimulationBridge bridge2;
    EXPECT_TRUE(bridge2.loadCheckpoint(ckpt_path, tick));
    EXPECT_EQ(bridge2.getTickCounter(), (uint64_t)tick);

    remove(path.c_str());
    remove(ckpt_path);
}

TEST(BridgeLifecycle, SaveCheckpointFailsWhenNoNetwork) {
    SimulationBridge bridge;
    EXPECT_FALSE(bridge.saveCheckpoint("/tmp/empty_checkpoint.bion"));
}

TEST(BridgeLifecycle, BackendDefaultsToCpu) {
    SimulationBridge bridge;
    EXPECT_EQ(bridge.getBackend(), SimulationBridge::Backend::CPU);
}

TEST(BridgeLifecycle, StartWithoutNetworkDoesNothing) {
    SimulationBridge bridge;
    EXPECT_FALSE(bridge.isNetworkLoaded());

    bridge.start();
    EXPECT_FALSE(bridge.isRunning());
}

TEST(BridgeLifecycle, StepWithoutNetworkDoesNothing) {
    SimulationBridge bridge;
    EXPECT_FALSE(bridge.isNetworkLoaded());

    bridge.step();
    EXPECT_FALSE(bridge.isRunning());
}

TEST(BridgeLifecycle, ResetClearsState) {
    SimulationBridge bridge;
    std::string path = write_valid_bion("lifecycle_reset");
    ASSERT_FALSE(path.empty());
    ASSERT_TRUE(bridge.loadNetwork(path.c_str()));

    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bridge.stop();

    bridge.reset();
    EXPECT_FALSE(bridge.isRunning());

    remove(path.c_str());
}

TEST(BridgeLifecycle, MultipleLoadNetworkCycles) {
    SimulationBridge bridge;

    std::string path1 = write_valid_bion("lifecycle_multi1");
    std::string path2 = write_valid_bion("lifecycle_multi2");
    ASSERT_FALSE(path1.empty());
    ASSERT_FALSE(path2.empty());

    EXPECT_TRUE(bridge.loadNetwork(path1.c_str()));
    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bridge.stop();

    EXPECT_TRUE(bridge.loadNetwork(path2.c_str()));
    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bridge.stop();

    EXPECT_TRUE(bridge.loadNetwork(path1.c_str()));
    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bridge.stop();

    remove(path1.c_str());
    remove(path2.c_str());
}

// Регрессионный тест: consolidation на не-CPU бэкенде не должен вызывать SEGFAULT.
// Баг: убран guard `if (backend_ == Backend::CPU)` → consolidation thread
// писал в USM-память GPU .so без синхронизации → memory corruption → SIGSEGV
// в simulateOneTick(). Фикс: вернуть guard (коммит f1fe4ec).
TEST(BridgeLifecycle, NoCrashOnStartStopWithGpuBackend) {
    SimulationBridge bridge;
    std::string path = write_valid_bion("lifecycle_gpu_safe");
    ASSERT_FALSE(path.empty());
    ASSERT_TRUE(bridge.loadNetwork(path.c_str()));

    // Пытаемся установить GPU-бэкенд (может быть недоступен — не страшно)
    bridge.setBackend(SimulationBridge::Backend::GPU_CUDA);
    // Если GPU недоступен, setBackend молча возвращает CPU — это нормально.
    // Главное: start/stop не должны крашиться.

    for (int i = 0; i < 5; i++) {
        bridge.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        bridge.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // После краш-теста загружаем сеть заново и проверяем что симуляция жива
    ASSERT_TRUE(bridge.loadNetwork(path.c_str()));
    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(bridge.isRunning());
    bridge.stop();

    std::vector<SegmentSnapshot> segs;
    bridge.getSegmentsSnapshot(segs);
    EXPECT_GE(segs.size(), 1u);

    remove(path.c_str());
}

// Регрессионный тест: частые start/stop не должны вызывать SEGFAULT
// (ловит гонки consolidation init/deinit).
TEST(BridgeLifecycle, NoCrashOnRapidStartStopCycles) {
    SimulationBridge bridge;
    std::string path = write_valid_bion("lifecycle_rapid");
    ASSERT_FALSE(path.empty());
    ASSERT_TRUE(bridge.loadNetwork(path.c_str()));

    // 10 быстрых циклов start/stop — проверка на гонки consolidation
    for (int i = 0; i < 10; i++) {
        bridge.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bridge.stop();
    }

    // Финальная проверка: симуляция всё ещё работает
    bridge.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    bridge.stop();
    EXPECT_GT(bridge.getCurrentTick(), 0u);

    remove(path.c_str());
}
