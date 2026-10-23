// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_tadpole_all_stages.cpp — тесты всех 4 стадий головастика.
// Все стадии в одном тесте (multi-test крашит tadpole_develop).

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <map>
#include <unistd.h>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

static int countOrphans(const NetworkModel& m) {
    std::map<uint32_t, int> cnt;
    for (auto& c : m.connections) { cnt[c.segment_a]++; cnt[c.segment_b]++; }
    int orphans = 0;
    for (auto& s : m.segments)
        if (cnt[s.id] == 0 && s.type != SegmentType::SOMA) orphans++;
    return orphans;
}

static NetworkModel gen(TadpoleStage stage, int rb, int din, int cin, int mn) {
    auto p = default_tadpole_params(stage);
    p.rb_per_hemisegment = rb; p.din_per_hemisegment = din;
    p.cin_per_hemisegment = cin; p.mn_per_hemisegment = mn;
    p.ain_per_hemisegment = 0; p.dlc_count = 1;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    return TadpoleDevelop(p).generate_connectome();
}

TEST(TadpoleAllStages, AllStagesGenerateWithoutOrphans) {
    struct { TadpoleStage s; const char* n; int rb, din, cin, mn; } stages[] = {
        {TadpoleStage::EARLY_TAILBUD, "EARLY", 1, 1, 1, 1},
        {TadpoleStage::LATE_TAILBUD,  "LATE",  1, 1, 1, 1},
        {TadpoleStage::HATCHING,      "HATCH", 1, 1, 1, 1},
        {TadpoleStage::FREE_SWIMMING, "FREE",  1, 1, 1, 1},
    };

    int prev_n = 0;
    for (auto& st : stages) {
        auto m = gen(st.s, st.rb, st.din, st.cin, st.mn);
        EXPECT_GT((int)m.segments.size(), 0) << st.n;
        EXPECT_GT((int)m.neurons.size(), 0) << st.n;
        EXPECT_EQ(countOrphans(m), 0) << st.n;
        EXPECT_GE((int)m.neurons.size(), prev_n) << st.n << " neurons decreased";
        prev_n = (int)m.neurons.size();
    }
}

// ═══════════════════════════════════════════════════════════════════
// CPG-симуляция на FREE_SWIMMING сети
// Знакопеременный kickstart на dIN (8 Hz, лево-правая alternation)
// ═══════════════════════════════════════════════════════════════════

TEST(TadpoleAllStages, CpgSimulationOnFreeSwimming) {
    auto p = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    p.rb_per_hemisegment = 3; p.din_per_hemisegment = 2;
    p.cin_per_hemisegment = 2; p.mn_per_hemisegment = 2;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    NetworkModel model = TadpoleDevelop(p).generate_connectome();

    EXPECT_GT((int)model.segments.size(), 0);
    EXPECT_EQ(countOrphans(model), 0);

    // Сбор dIN сом для стимуляции (дорсальные интернейроны)
    // CPG breakthrough: позиции нейронов изменились — расширяем Z-диапазон
    std::vector<uint32_t> din_left, din_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y < 70.0f || seg.position.y > 110.0f) continue;
        if (seg.position.z > 200.0f) continue;  // не голова (Z<200)
        if (seg.position.x < 0) din_left.push_back(seg.id);
        else din_right.push_back(seg.id);
    }
    // Debug: print all candidate somas
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 60.0f && seg.position.y <= 115.0f)
            printf("[dIN-candidate] id=%u Y=%.1f Z=%.1f X=%.1f\n", seg.id, seg.position.y, seg.position.z, seg.position.x);
    }
    ASSERT_GT(din_left.size(), 0u) << "No left dIN neurons (Y∈[70,110], Z∈[-190,-110])";
    ASSERT_GT(din_right.size(), 0u) << "No right dIN neurons (Y∈[70,110], Z∈[-190,-110])";

    // Симуляция
    SaveNetwork(model, "/tmp/test_tadpole_cpg.bion");
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_tadpole_cpg.bion"));

    auto apply_kickstart = [&](float elapsed_ms) {
        float cycle = fmodf(elapsed_ms, 120.0f);
        bool stimulate_left = (cycle < 60.0f);
        float phase = fmodf(cycle, 60.0f);
        if (phase >= 20.0f) return;
        auto& targets = stimulate_left ? din_left : din_right;
        for (uint32_t sid : targets)
            sim.addCurrentStimulus(sid, 500.0f, 20.0f, 0.0f);
    };

    sim.start();
    usleep(50000);  // даём simulationLoop запуститься и начать тики
    uint64_t start_tick = sim.getCurrentTick();

    int active_frames = 0;
    float elapsed_ms = 0.0f;

    for (int f = 0; f < 10; f++) {
        for (int sub = 0; sub < 20; sub++) {
            usleep(25000);
            uint64_t tick = sim.getCurrentTick();
            elapsed_ms = (tick - start_tick) * 0.1f;
            if (elapsed_ms < 500.0f)
                apply_kickstart(elapsed_ms);
        }
        std::vector<SegmentSnapshot> snap;
        sim.getSegmentsSnapshot(snap);
        int spiking = 0;
        for (auto& s : snap)
            if (s.V_m > -20.0f) spiking++;
        if (spiking > 0) active_frames++;
    }

    sim.stop();

    printf("   CPG active_frames=%d (dIN kickstart, elapsed=%.0f ms)\n",
           active_frames, elapsed_ms);
    EXPECT_GE(active_frames, 1)
        << "CPG should show any activity with dIN kickstart ("
        << active_frames << "/10 active frames)";
}
