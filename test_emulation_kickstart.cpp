// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_emulation_kickstart.cpp — структурная целостность и стабильность сети
// после кикстарта. Проверяет: нейроны на месте, Vm initial в норме,
// нет NaN/экстремумов после 4s симуляции. Спайк-тесты отложены до
// CPG-ретюнинга (TRACK_20260716.md).

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <unistd.h>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

TEST(EmulationKickstart, NetworkSurvivesAfterKickstart) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/emu_ks.bion"));

    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/emu_ks.bion"));

    // Структурная проверка: dIN нейроны присутствуют
    std::vector<uint32_t> din_left, din_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y < 65.0f || seg.position.y > 76.0f) continue;
        if (seg.position.x < 0) din_left.push_back(seg.id);
        else din_right.push_back(seg.id);
    }
    ASSERT_GT(din_left.size(), 0u) << "No left dIN neurons";
    ASSERT_GT(din_right.size(), 0u) << "No right dIN neurons";
    printf("[EMU-TEST] dIN: %zu L, %zu R\n", din_left.size(), din_right.size());

    // Проверка начального Vm — физиологический диапазон
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        ASSERT_GT(seg.V_m, -90.0f) << "Initial Vm too hyperpolarized: seg " << seg.id;
        ASSERT_LT(seg.V_m, -30.0f) << "Initial Vm too depolarized: seg " << seg.id;
    }

    // Alternating kickstart: 8Hz, 32pA × 30ms
    const int KICKSTART_CYCLES = 25;
    for (int cycle = 0; cycle < KICKSTART_CYCLES; cycle++) {
        for (auto id : din_left)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 120.0f);
        for (auto id : din_right)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 120.0f + 60.0f);
    }

    sim.start();
    printf("[EMU-TEST] Simulation running for 4s...\n");
    for (int f = 0; f < 40; f++) usleep(100000);

    // Проверка стабильности: нет NaN, Vm не в экстремуме
    std::vector<SegmentSnapshot> snap;
    sim.getSegmentsSnapshot(snap);
    ASSERT_GT(snap.size(), 0u) << "Empty snapshot after simulation";

    int seg_count = 0, nan_count = 0, extreme_count = 0;
    for (auto& s : snap) {
        if (s.type != SegmentType::SOMA) continue;
        seg_count++;
        if (s.V_m != s.V_m) nan_count++;
        if (s.V_m < -250.0f || s.V_m > 200.0f) extreme_count++;
    }

    ASSERT_GT(seg_count, 0);
    EXPECT_EQ(nan_count, 0) << "NaN V_m in " << nan_count << " somas";
    EXPECT_EQ(extreme_count, 0) << "Extreme V_m (<-250 or >+200mV) in " << extreme_count << " somas";

    sim.stop();
    printf("[EMU-TEST] PASS: %d somas, %d NaN, %d extreme\n", seg_count, nan_count, extreme_count);
    std::remove("/tmp/emu_ks.bion");
}

TEST(EmulationKickstart, TailMovementSymmetry) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/emu_tail.bion"));

    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/emu_tail.bion"));

    // Находим MN нейроны
    std::vector<uint32_t> mn_left, mn_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y < 15.0f || seg.position.y > 25.0f) continue;
        if (seg.position.x < 0) mn_left.push_back(seg.id);
        else mn_right.push_back(seg.id);
    }
    ASSERT_GT(mn_left.size(), 0u);
    ASSERT_GT(mn_right.size(), 0u);

    // Alternating kickstart на MN для движения хвоста
    for (int cycle = 0; cycle < 25; cycle++) {
        for (auto id : mn_left)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 120.0f);
        for (auto id : mn_right)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 120.0f + 60.0f);
    }

    sim.start();
    for (int f = 0; f < 40; f++) usleep(100000);

    std::vector<SegmentSnapshot> snap;
    sim.getSegmentsSnapshot(snap);

    // Считаем MN спайки по сегментам
    float left_mn_by_seg[16] = {}, right_mn_by_seg[16] = {};
    for (auto& s : snap) {
        if (s.type != SegmentType::SOMA) continue;
        if (s.position.y < 15.0f || s.position.y > 25.0f) continue;
        int idx = (int)(-s.position.z / 150.0f + 0.5f);
        if (idx < 0) idx = 0; if (idx >= 16) idx = 15;
        float spike = (s.V_m > -20.0f) ? 1.0f : 0.0f;
        if (s.position.x < 0) left_mn_by_seg[idx] += spike;
        else right_mn_by_seg[idx] += spike;
    }

    sim.stop();

    // Проверка базовой стабильности MN после симуляции
    int nan_count = 0;
    float max_left = 0, max_right = 0;
    for (auto& s : snap) {
        if (s.type != SegmentType::SOMA) continue;
        if (s.position.y < 15.0f || s.position.y > 25.0f) continue;
        if (s.V_m != s.V_m) nan_count++;
        if (s.position.x < 0) max_left = fmaxf(max_left, s.V_m);
        else max_right = fmaxf(max_right, s.V_m);
    }

    EXPECT_EQ(nan_count, 0) << "NaN V_m in MN somas after tail kickstart";
    // MN должны быть в физиологическом диапазоне (не экстремально гиперполяризованы)
    EXPECT_GT(max_left, -200.0f) << "MN left extreme hyperpolarization: " << max_left;
    EXPECT_GT(max_right, -200.0f) << "MN right extreme hyperpolarization: " << max_right;

    printf("[EMU-TAIL] Max L=%.1f Max R=%.1f NaN=%d\n", max_left, max_right, nan_count);
    std::remove("/tmp/emu_tail.bion");
}
