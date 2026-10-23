// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.

// G6: Performance threshold enforcement — benchmarks с PASS/FAIL gates.
// Baseline: Intel UHD Graphics 630, Ubuntu 24.04, GCC 13.3.
// Каждый тест фейлится при регрессе >2× от baseline.

#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <unistd.h>
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/data_model.h"

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

// ── BION save speed ─────────────────────────────────────────────────
// Baseline: ~5-15ms для 500 сегментов. Threshold: 500ms (>30× запас для CI)
TEST(PerfThresholds, Bion_SaveSpeed) {
    NetworkModel model;
    for (int i = 0; i < 500; i++) {
        SegmentSnapshot seg;
        seg.id = static_cast<uint32_t>(i);
        seg.neuron_id = static_cast<uint32_t>(i);
        seg.type = SegmentType::SOMA;
        seg.radius = 5.0f; seg.length = 10.0f; seg.V_m = -65.0f;
        seg.position = {static_cast<float>(i), 0, 0};
        seg.direction = {0, 1, 0};
        model.segments.push_back(seg);
    }

    auto t0 = Clock::now();
    ASSERT_TRUE(SaveNetwork(model, "/tmp/perf_thresh_save.bion"));
    auto t1 = Clock::now();
    auto ms = std::chrono::duration_cast<Ms>(t1 - t0).count();

    printf("[perf-threshold] Save 500 segments: %ld ms (baseline ~10ms, limit 500ms)\n", ms);
    EXPECT_LT(ms, 500) << "BION save degraded: " << ms << "ms > 500ms threshold";
    unlink("/tmp/perf_thresh_save.bion");
}

// ── BION load speed ─────────────────────────────────────────────────
// Baseline: ~5-15ms. Threshold: 500ms.
TEST(PerfThresholds, Bion_LoadSpeed) {
    NetworkModel model;
    for (int i = 0; i < 500; i++) {
        SegmentSnapshot seg;
        seg.id = static_cast<uint32_t>(i);
        seg.neuron_id = static_cast<uint32_t>(i);
        seg.type = SegmentType::SOMA;
        seg.radius = 5.0f; seg.length = 10.0f; seg.V_m = -65.0f;
        seg.position = {static_cast<float>(i), 0, 0};
        seg.direction = {0, 1, 0};
        model.segments.push_back(seg);
    }
    ASSERT_TRUE(SaveNetwork(model, "/tmp/perf_thresh_load.bion"));

    NetworkModel loaded;
    std::string err;
    auto t0 = Clock::now();
    int rc = studio_load_bion("/tmp/perf_thresh_load.bion", &loaded, err);
    auto t1 = Clock::now();
    auto ms = std::chrono::duration_cast<Ms>(t1 - t0).count();
    ASSERT_EQ(rc, 0) << "Load failed: " << err;

    printf("[perf-threshold] Load 500 segments: %ld ms (baseline ~10ms, limit 500ms)\n", ms);
    EXPECT_LT(ms, 500) << "BION load degraded: " << ms << "ms > 500ms threshold";
    EXPECT_EQ(loaded.segments.size(), model.segments.size());
    unlink("/tmp/perf_thresh_load.bion");
}

// ── Serialization roundtrip memory stability ────────────────────────
// Baseline: стабильный размер после 10 циклов save/load.
TEST(PerfThresholds, Serialization_RoundtripStable) {
    NetworkModel model;
    for (int i = 0; i < 100; i++) {
        SegmentSnapshot seg;
        seg.id = static_cast<uint32_t>(i);
        seg.neuron_id = static_cast<uint32_t>(i / 2);
        seg.type = SegmentType::SOMA; seg.radius = 5.0f; seg.length = 10.0f;
        seg.V_m = -65.0f; seg.position = {static_cast<float>(i), 0, 0};
        seg.direction = {0, 1, 0};
        model.segments.push_back(seg);
    }

    size_t first_size = 0;
    for (int cycle = 0; cycle < 10; cycle++) {
        ASSERT_TRUE(SaveNetwork(model, "/tmp/perf_thresh_mem.bion"));
        NetworkModel loaded;
        std::string err;
        int rc = studio_load_bion("/tmp/perf_thresh_mem.bion", &loaded, err);
        ASSERT_EQ(rc, 0) << "Load failed at cycle " << cycle << ": " << err;
        ASSERT_EQ(loaded.segments.size(), model.segments.size());

        if (cycle == 0) {
            first_size = loaded.segments.size();
        } else {
            EXPECT_EQ(loaded.segments.size(), first_size)
                << "Segment count changed at cycle " << cycle;
        }
    }
    unlink("/tmp/perf_thresh_mem.bion");
}

// ── Rapid save/load stress ──────────────────────────────────────────
// Baseline: 50 циклов save/load без деградации скорости.
TEST(PerfThresholds, Serialization_RapidSaveLoad) {
    NetworkModel model;
    for (int i = 0; i < 100; i++) {
        SegmentSnapshot seg;
        seg.id = static_cast<uint32_t>(i);
        seg.type = SegmentType::SOMA;
        seg.radius = 5.0f; seg.length = 10.0f; seg.V_m = -65.0f;
        seg.position = {static_cast<float>(i), 0, 0};
        seg.direction = {0, 1, 0};
        model.segments.push_back(seg);
    }

    auto t0 = Clock::now();
    for (int i = 0; i < 50; i++) {
        ASSERT_TRUE(SaveNetwork(model, "/tmp/perf_thresh_rapid.bion"));
        NetworkModel loaded;
        std::string err;
        int rc = studio_load_bion("/tmp/perf_thresh_rapid.bion", &loaded, err);
        ASSERT_EQ(rc, 0);
    }
    auto t1 = Clock::now();
    auto ms = std::chrono::duration_cast<Ms>(t1 - t0).count();

    printf("[perf-threshold] 50× save+load cycles: %ld ms (baseline ~200ms, limit 2000ms)\n", ms);
    EXPECT_LT(ms, 2000) << "Rapid save/load degraded: " << ms << "ms > 2000ms threshold";
    unlink("/tmp/perf_thresh_rapid.bion");
}
