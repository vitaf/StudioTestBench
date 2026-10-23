// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// G5: Chaos/fault-injection тесты — kill-9, corrupted .bion, disk-full
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/data_model.h"

// ── Corrupted .bion (truncated, garbage) ──────────────────────
TEST(ChaosExt, Bion_Truncated) {
    // Write partial magic only
    FILE* f = fopen("/tmp/chaos_trunc.bion", "wb");
    ASSERT_NE(f, nullptr);
    uint32_t magic = 0x42494F4E;
    fwrite(&magic, 1, 2, f);  // only 2 bytes of magic
    fclose(f);

    NetworkModel model;
    std::string err;
    bool ok = studio_load_bion("/tmp/chaos_trunc.bion", &model, err);
    EXPECT_FALSE(ok) << "Should reject truncated .bion";
    unlink("/tmp/chaos_trunc.bion");
}

TEST(ChaosExt, Bion_ZeroSegments) {
    // Valid header but zero segments
    FILE* f = fopen("/tmp/chaos_zero.bion", "wb");
    ASSERT_NE(f, nullptr);
    NetworkHeader h = {};
    h.magic = 0x42494F4E; h.version = 9; h.mode = 0;
    h.num_neurons = 0; h.num_segments = 0;
    h.num_intra_connections = 0; h.num_synapses = 0;
    fwrite(&h, sizeof(h), 1, f);
    fclose(f);

    NetworkModel model;
    std::string err;
    bool ok = studio_load_bion("/tmp/chaos_zero.bion", &model, err);
    // Should either reject or return empty model
    if (ok) {
        EXPECT_EQ(model.segments.size(), 0u);
    }
    unlink("/tmp/chaos_zero.bion");
}

TEST(ChaosExt, Bion_RandomBytes) {
    // Completely random file
    FILE* f = fopen("/tmp/chaos_random.bion", "wb");
    ASSERT_NE(f, nullptr);
    uint8_t buf[1024];
    for (int i = 0; i < 1024; i++) buf[i] = (uint8_t)(i * 73 + 11);
    fwrite(buf, 1, sizeof(buf), f);
    fclose(f);

    NetworkModel model;
    std::string err;
    bool ok = studio_load_bion("/tmp/chaos_random.bion", &model, err);
    EXPECT_FALSE(ok) << "Should reject random bytes";
    unlink("/tmp/chaos_random.bion");
}

// ── SimulationBridge double-start / double-stop ──────────────────
TEST(ChaosExt, SimBridge_DoubleStart) {
    NetworkModel model;
    SegmentSnapshot seg;
    seg.id = 0; seg.neuron_id = 0; seg.type = SegmentType::SOMA;
    seg.radius = 5.0f; seg.length = 10.0f; seg.V_m = -65.0f;
    model.segments.push_back(seg);
    ASSERT_TRUE(SaveNetwork(model, "/tmp/chaos_dbl.bion"));

    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/chaos_dbl.bion"));

    sim.start();
    // Double start should not crash
    EXPECT_NO_THROW(sim.start());
    sim.stop();
    // Double stop should not crash
    EXPECT_NO_THROW(sim.stop());
    unlink("/tmp/chaos_dbl.bion");
}

TEST(ChaosExt, SimBridge_StopWithoutStart) {
    SimulationBridge sim;
    EXPECT_NO_THROW(sim.stop());
}

TEST(ChaosExt, SimBridge_LoadWhileRunning) {
    NetworkModel model;
    SegmentSnapshot seg;
    seg.id = 0; seg.neuron_id = 0; seg.type = SegmentType::SOMA;
    seg.radius = 5.0f; seg.length = 10.0f; seg.V_m = -65.0f;
    model.segments.push_back(seg);
    ASSERT_TRUE(SaveNetwork(model, "/tmp/chaos_loadrun.bion"));

    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/chaos_loadrun.bion"));
    sim.start();
    // Loading while running should handle gracefully (stop+reload or reject)
    EXPECT_NO_THROW(sim.loadNetwork("/tmp/chaos_loadrun.bion"));
    sim.stop();
    unlink("/tmp/chaos_loadrun.bion");
}

// ── BION Save/Load roundtrip resilience ─────────────────────────
TEST(ChaosExt, Bion_SaveLoadResilience) {
    NetworkModel model;
    for (int i = 0; i < 10; i++) {
        SegmentSnapshot seg;
        seg.id = i; seg.neuron_id = i / 2;
        seg.type = SegmentType::SOMA;
        seg.radius = 3.0f + i; seg.length = 6.0f + i * 2;
        seg.V_m = -65.0f + i;
        seg.position = {(float)i, (float)(i * 2), 0};
        seg.direction = {0, 1, 0};
        model.segments.push_back(seg);
    }

    ASSERT_TRUE(SaveNetwork(model, "/tmp/chaos_roundtrip.bion"));

    NetworkModel loaded;
    std::string err;
    ASSERT_TRUE(studio_load_bion("/tmp/chaos_roundtrip.bion", &loaded, &err));
    EXPECT_EQ(loaded.segments.size(), model.segments.size());

    unlink("/tmp/chaos_roundtrip.bion");
}
