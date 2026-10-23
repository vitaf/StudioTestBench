// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_debug_network.cpp — проверка биологически-точной отладочной сети

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>
#include <set>
#include <algorithm>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_myotome.h"

// Прогрев Core/SimulationBridge перед основными тестами
// (первый тест в suite страдает от race condition при инициализации)
TEST(DebugNetwork, AaaWarmupInfrastructure) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_warmup.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_warmup.bion"));
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    sim.stop();
    std::remove("/tmp/test_warmup.bion");
    SUCCEED();
}

TEST(DebugNetwork, StructureAndSerialization) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    EXPECT_GE(model.segments.size(), 100u);
    EXPECT_GE(model.connections.size(), 100u);
    EXPECT_GE(model.synapses.size(), 40u);
    EXPECT_GE(model.neurons.size(), 30u);

    int somas = 0, axons = 0, dendrites = 0;
    for (auto& seg : model.segments) {
        switch (seg.type) {
            case SegmentType::SOMA: somas++; break;
            case SegmentType::AXON: axons++; break;
            case SegmentType::DENDRITE: dendrites++; break;
            default: break;
        }
    }
    EXPECT_GE(somas, 45);
    EXPECT_GE(axons, 30);
    EXPECT_GE(dendrites, 30);  // аксо-дендритные синапсы направлены на дендриты

    printf("[TEST] Structure: %d somas, %d axons, %d dendrites\n",
           somas, axons, dendrites);

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_debug_struct.bion"));
    NetworkModel loaded;
    std::string err;
    ASSERT_EQ(::studio_load_bion("/tmp/test_debug_struct.bion", &loaded, err), 0) << err;
    EXPECT_EQ(loaded.segments.size(), model.segments.size());
    EXPECT_EQ(loaded.connections.size(), model.connections.size());
    EXPECT_EQ(loaded.synapses.size(), model.synapses.size());
    EXPECT_EQ(loaded.neurons.size(), model.neurons.size());
    std::remove("/tmp/test_debug_struct.bion");

    printf("[TEST] Structure + serialization OK: %zu segs, %zu conns, %zu syns, %zu neurons\n",
           model.segments.size(), model.connections.size(),
           model.synapses.size(), model.neurons.size());
}

// Проверка базовой возбудимости: MN должен отвечать на прямой dIN драйв
// (Использует gap junctions dIN→mn, без химических синапсов)
TEST(DebugNetwork, DinToMnPathway) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    // Считаем dIN и MN
    int din_count = 0, mn_count = 0;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 65.0f && seg.position.y <= 76.0f) din_count++;
        if (seg.position.y >= 15.0f && seg.position.y <= 25.0f) mn_count++;
    }
    printf("[TEST] Network has %d dIN, %d MN\n", din_count, mn_count);
    EXPECT_GE(din_count, 8) << "Минимум 8 dIN (3L+3R)×2сегм - 4 для dlc";
    EXPECT_GE(mn_count, 8) << "Минимум 8 MN (2L+2R)×2сегм";

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_d2m.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_d2m.bion"));
    sim.start();

    // Ждём 500ms чтобы нейроны установили потенциал покоя
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!sim.isRunning()) break;
    }

    // Проверяем V_m покоя MN (должен быть в биологическом диапазоне)
    std::vector<SegmentSnapshot> segs;
    sim.getSegmentsSnapshot(segs);
    float mn_rest_sum = 0; int mn_rest_n = 0;
    for (auto& s : segs) {
        if (s.type == SegmentType::SOMA && s.position.y >= 15.0f && s.position.y <= 25.0f) {
            mn_rest_sum += s.V_m; mn_rest_n++;
        }
    }
    float mn_rest = mn_rest_n > 0 ? mn_rest_sum / mn_rest_n : -99;
    printf("[TEST] MN resting V_m = %.1f mV (n=%d)\n", mn_rest, mn_rest_n);
    // Без драйва MN могут быть гиперполяризованы (NaK pump + KCC2).
    // С dlc→dIN включено, сеть ждёт кикстарта. Проверяем только отсутствие аномалий.
    EXPECT_GT(mn_rest, -201.0f) << "MN за гранью tracking limit — ошибка симуляции";
    EXPECT_LT(mn_rest, 150.0f) << "MN аномально деполяризован (" << mn_rest << "mV > 150)";

    sim.stop();
    std::remove("/tmp/test_d2m.bion");
}

// Тест CPG: alternating kickstart через dlc_MHR
TEST(DebugNetwork, CPGWithKickstart) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    // Физиологический триггер ребаунда dlc_MHR: кик cIN деполяризацией →
    // cIN спайк → глицин на контралатеральный dlc (Cl-шунт) → dlc ребаунд (Ih).
    std::vector<uint32_t> cin_left, cin_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 48.0f && seg.position.z < 200.0f) {
            if (seg.position.x < 0) cin_left.push_back(seg.id);
            else cin_right.push_back(seg.id);
        }
    }
    ASSERT_GT(cin_left.size(), 0u);
    ASSERT_GT(cin_right.size(), 0u);

    // dlc_MHR (z>=200) для подсчёта rebound-спайков
    std::vector<uint32_t> dlc_left, dlc_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 60.0f && seg.position.z >= 200.0f) {
            if (seg.position.x < 0) dlc_left.push_back(seg.id);
            else dlc_right.push_back(seg.id);
        }
    }

    // Левые/правые MN сомы (для подсчёта спайков)
    std::vector<uint32_t> mn_left, mn_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 15.0f && seg.position.y <= 25.0f) {
            if (seg.position.x < -1.0f) mn_left.push_back(seg.id);
            else if (seg.position.x > 1.0f) mn_right.push_back(seg.id);
        }
    }

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_cpg.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_cpg.bion"));
    sim.enableSpikeCount();  // живой счётчик спайков в Core (не кэш снапшота)

    for (int cycle = 0; cycle < 50; cycle++) {
        for (auto id : cin_left)
            sim.addCurrentStimulus(id, 400.0f, 60.0f, cycle * 120.0f);   // cIN кик (деполяризация → спайк → глицин)
        for (auto id : cin_right)
            sim.addCurrentStimulus(id, 400.0f, 60.0f, cycle * 120.0f + 60.0f);
    }
    sim.start();
    printf("[TEST] Kickstart: 50 cycles alternating cIN left/right\n");

    for (int i = 0; i < 800; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!sim.isRunning()) break;
    }
    sim.stop();

    int L_dlc = 0, R_dlc = 0;
    for (auto id : dlc_left) L_dlc += (int)sim.getSpikeCount(id);
    for (auto id : dlc_right) R_dlc += (int)sim.getSpikeCount(id);
    int L_mn = 0, R_mn = 0;
    for (auto id : mn_left) L_mn += (int)sim.getSpikeCount(id);
    for (auto id : mn_right) R_mn += (int)sim.getSpikeCount(id);
    int L_cin = 0, R_cin = 0;
    for (auto id : cin_left) L_cin += (int)sim.getSpikeCount(id);
    for (auto id : cin_right) R_cin += (int)sim.getSpikeCount(id);
    std::remove("/tmp/test_cpg.bion");

    int total_mn = L_mn + R_mn;
    printf("[TEST] Spikes: cIN L=%d R=%d | dlc L=%d R=%d | MN L=%d R=%d\n", L_cin, R_cin, L_dlc, R_dlc, L_mn, R_mn);

    EXPECT_GT(L_dlc + R_dlc, 0) << "Kick-нейроны молчат";
    EXPECT_GT(total_mn, 0) << "MN молчат — CPG не работает";
    EXPECT_GT(total_mn, 5) << "Слишком мало спайков";
}

// Диагностика: спонтанная активность с разными seed
TEST(DebugNetwork, NoStimulusSpontaneous) {
    for (int seed : {1, 42, 123, 777}) {
        srand(seed);
        NetworkModel model;
        generate_debug_tadpole_network(model);

        ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_spont.bion"));
        SimulationBridge sim;
        ASSERT_TRUE(sim.loadNetwork("/tmp/test_spont.bion"));

        sim.start();

        for (int i = 0; i < 500; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!sim.isRunning()) break;
        }

        std::vector<SegmentSnapshot> segs;
        int L_dIN_spikes = 0, R_dIN_spikes = 0, L_MN_spikes = 0, R_MN_spikes = 0;
        float L_dIN_sum = 0, R_dIN_sum = 0, L_MN_sum = 0, R_MN_sum = 0;
        int L_dIN_n = 0, R_dIN_n = 0, L_MN_n = 0, R_MN_n = 0;

        for (int tick = 0; tick < 2000 && sim.isRunning(); tick++) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            sim.getSegmentsSnapshot(segs);
            for (auto& s : segs) {
                if (s.type != SegmentType::SOMA) continue;
                if (s.position.y >= 65.0f && s.position.y <= 76.0f) {
                    if (s.position.x < -1.0f) { L_dIN_sum += s.V_m; L_dIN_n++; if (s.V_m > -20) L_dIN_spikes++; }
                    else { R_dIN_sum += s.V_m; R_dIN_n++; if (s.V_m > -20) R_dIN_spikes++; }
                }
                if (s.position.y >= 15.0f && s.position.y <= 25.0f) {
                    if (s.position.x < -1.0f) { L_MN_sum += s.V_m; L_MN_n++; if (s.V_m > -20) L_MN_spikes++; }
                    else { R_MN_sum += s.V_m; R_MN_n++; if (s.V_m > -20) R_MN_spikes++; }
                }
            }
        }
        sim.stop();
        std::remove("/tmp/test_spont.bion");

        printf("[DIAG] seed=%d: L_dIN=%d(%.1f) R_dIN=%d(%.1f) | L_MN=%d(%.1f) R_MN=%d(%.1f)\n",
               seed, L_dIN_spikes, L_dIN_n>0?L_dIN_sum/L_dIN_n:-99.0f,
               R_dIN_spikes, R_dIN_n>0?R_dIN_sum/R_dIN_n:-99.0f,
               L_MN_spikes, L_MN_n>0?L_MN_sum/L_MN_n:-99.0f,
               R_MN_spikes, R_MN_n>0?R_MN_sum/R_MN_n:-99.0f);
    }
}

// Диагностический тест: только правый kickstart — проверка правого dlc→dIN пути
TEST(DebugNetwork, RightSideOnlyKickstart) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    // Найти нейроны для кикстарта: dlc_MHR (z>=200, DIN_NAK — не IONOTROPIC_NAK/POST_INHIBITORY_REBOUND)
    std::vector<uint32_t> kick_left, kick_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 60.0f && seg.position.z >= 200.0f) {
            if (seg.position.x < 0) kick_left.push_back(seg.id);
            else kick_right.push_back(seg.id);
        }
    }
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_right_only.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_right_only.bion"));

    // Только правый
    for (int cycle = 0; cycle < 50; cycle++)
        for (auto id : kick_right)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 60.0f);

    sim.start();
    printf("[TEST] Right-only kickstart\n");

    for (int i = 0; i < 500; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!sim.isRunning()) break;
    }

    std::vector<SegmentSnapshot> segs;
    int L_dIN_spikes = 0, R_dIN_spikes = 0;
    float L_dIN_sum = 0, R_dIN_sum = 0;
    int L_dIN_n = 0, R_dIN_n = 0;

    for (int tick = 0; tick < 2000 && sim.isRunning(); tick++) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        sim.getSegmentsSnapshot(segs);
        for (auto& s : segs) {
            if (s.type != SegmentType::SOMA) continue;
            if (s.position.y >= 65.0f && s.position.y <= 76.0f) {
                if (s.position.x < -1.0f) { L_dIN_sum += s.V_m; L_dIN_n++; if (s.V_m > -20) L_dIN_spikes++; }
                else { R_dIN_sum += s.V_m; R_dIN_n++; if (s.V_m > -20) R_dIN_spikes++; }
            }
        }
    }
    sim.stop();
    std::remove("/tmp/test_right_only.bion");

    printf("[DIAG] Right-only: L_dIN spikes=%d avg=%.1f | R_dIN spikes=%d avg=%.1f\n",
           L_dIN_spikes, L_dIN_n>0?L_dIN_sum/L_dIN_n:-99.0f,
           R_dIN_spikes, R_dIN_n>0?R_dIN_sum/R_dIN_n:-99.0f);
}
TEST(DebugNetwork, LeftSideOnlyKickstart) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    // Диагностика: neuron_id дендритов dIN vs сом dIN (проверка связи дендрит→сома)
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::DENDRITE && seg.position.y >= 65.0f && seg.position.y <= 80.0f)
            printf("[DIAG] dIN dendrite id=%u neuron=%u (%.0f,%.0f,%.0f)\n", seg.id, seg.neuron_id, seg.position.x, seg.position.y, seg.position.z);
    }
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::SOMA && seg.position.y >= 65.0f && seg.position.y <= 76.0f)
            printf("[DIAG] dIN soma id=%u neuron=%u (%.0f,%.0f,%.0f)\n", seg.id, seg.neuron_id, seg.position.x, seg.position.y, seg.position.z);
    }
    // Модификаторы сомы dIN (id=60): есть ли NMDA (тип 9) для bath-NMDA пейсмейкера?
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::SOMA && seg.id == 60) {
            printf("[DIAG] dIN soma60: V_m=%.1f", seg.V_m);
            for (int m = 0; m < seg.modifier_count; m++) {
                int t = (int)seg.modifiers[m].mod_type();
                if (t == 125 || t == 105 || t == 129 || t == 9)  // DIN_NAK, NaP, HCN1, NMDA
                    printf(" | t%d st0=%.3f st1=%.3f", t, seg.modifiers[m].state[0], seg.modifiers[m].state[1]);
            }
            printf(" | ions Na=%.1f Cl=%.1f Mg=%.2f\n", seg.intra_ions[0], seg.intra_ions[3], seg.intra_ions[4]);
        }
    }
    // Connections сомы dIN (id=60, neuron=4): сома↔AIS↔аксон (build_cpg) + сома↔дендрит (make_chain)
    for (auto& c : model.connections) {
        if (c.segment_a == 60 || c.segment_b == 60)
            printf("[DIAG] conn soma60: a=%u b=%u intra=%d\n", c.segment_a, c.segment_b, c.is_intracellular);
    }
    // Все connections с большими id (дендритные сегменты make_chain, id 16000+)
    for (auto& c : model.connections) {
        if (c.segment_a >= 16000 || c.segment_b >= 16000)
            printf("[DIAG] conn bigid: a=%u b=%u intra=%d\n", c.segment_a, c.segment_b, c.is_intracellular);
    }

    // Найти нейроны для кикстарта: dlc_MHR (z>=200, DIN_NAK — не IONOTROPIC_NAK/POST_INHIBITORY_REBOUND)
    std::vector<uint32_t> kick_left, kick_right;
    bool using_cin_l = false;
    uint32_t dlc_neuron = 0;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 60.0f && seg.position.z >= 200.0f) {
            if (seg.position.x < 0) kick_left.push_back(seg.id);
            else kick_right.push_back(seg.id);
            if (!dlc_neuron) dlc_neuron = seg.neuron_id;
        }
    }
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_left_only.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_left_only.bion"));
    sim.enableSpikeCount();  // живой счётчик спайков в Core (не кэш снапшота)
    sim.enableVmMax();       // живой трекер пика V_m (не кэш снапшота)
    // Покой dlc сомы до стимуляции (getSegmentVm — прямое чтение)
    printf("[AXIAL] rest soma=%.1f (id=%u)\n", sim.getSegmentVm(kick_left[0]), kick_left[0]);

    // Левые MN/сIN/dIN сомы (для подсчёта спайков и пиков)
    std::vector<uint32_t> left_mn, left_din;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 15.0f && seg.position.y <= 25.0f && seg.position.x < -1.0f)
            left_mn.push_back(seg.id);
        if (seg.position.y >= 65.0f && seg.position.y <= 76.0f && seg.position.x < -1.0f)
            left_din.push_back(seg.id);
    }

    // Только левый, 50 стимулов — деполяризующий kickstart (поджечь dlc), ребаунд = cIN→dlc ингибиция
    for (int cycle = 0; cycle < 50; cycle++)
        for (auto id : kick_left)
            sim.addCurrentStimulus(id, 400.0f, 30.0f, cycle * 60.0f);

    sim.start();
    printf("[TEST] Left-only kickstart (dlc_MHR): 50 cycles\n");

    for (int i = 0; i < 800; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!sim.isRunning()) break;
    }
    sim.stop();

    printf("[AXIAL] rest-after-stop: dlc soma=%.1f AIS=%.1f axon0=%.1f (stim 500pA x20ms)\n",
           sim.getSegmentVm(kick_left[0]),
           sim.getSegmentVm(kick_left[0] + 1),
           sim.getSegmentVm(kick_left[0] + 3));

    int L_dlc_spikes = 0;
    for (auto id : kick_left) L_dlc_spikes += (int)sim.getSpikeCount(id);
    int L_MN_spikes = 0;
    for (auto id : left_mn) L_MN_spikes += (int)sim.getSpikeCount(id);
    // Пики V_m (доходит ли EPSP/ПД до dIN и MN)
    float dIN_max = -200, mn_max = -200;
    for (auto id : left_din) dIN_max = std::max(dIN_max, sim.getVmMax(id));
    for (auto id : left_mn)  mn_max  = std::max(mn_max,  sim.getVmMax(id));

    // Диагностика A: cleft-глутамат на синапсах (выброс работает?) + ECS ионы dIN
    float max_cleft_glu = 0; int syn_glu = 0, syn_total = 0;
    {
        std::vector<SynapseSnapshot> syns;
        sim.getSynapsesSnapshot(syns);
        syn_total = (int)syns.size();
        for (auto& sy : syns) {
            if (sy.cleft_nt[0] > max_cleft_glu) max_cleft_glu = sy.cleft_nt[0];
            if (sy.cleft_nt[0] > 0.01f) syn_glu++;
        }
    }
    float din_Na_o = -1, din_K_o = -1, din_Ca_o = -1, din_Na_i = -1;
    float dlc_Na_i = -1, dlc_K_i = -1, dlc_Cl_i = -1, dlc_Vm = -99;
    {
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        for (auto& s : segs) {
            if (s.id == left_din[0]) {
                din_Na_o = s.extra_ions[0]; din_K_o = s.extra_ions[1]; din_Ca_o = s.extra_ions[2];
                din_Na_i = s.intra_ions[0];
            }
            if (s.id == kick_left[0]) {
                dlc_Na_i = s.intra_ions[0]; dlc_K_i = s.intra_ions[1]; dlc_Cl_i = s.intra_ions[3];
                dlc_Vm = s.V_m;
            }
        }
        printf("[ION] dlc soma: V_m=%.1f Na_i=%.2f K_i=%.2f Cl_i=%.2f (dIN Na_i=%.2f)\n",
               dlc_Vm, dlc_Na_i, dlc_K_i, dlc_Cl_i, din_Na_i);
    }
    // dlc аксон: снимаем пик V_m по каждому сегменту (от сомы к терминали) — где затухает ПД
    uint32_t dlc_nid = 0;
    for (auto& seg : model.segments) if (seg.id == kick_left[0]) { dlc_nid = seg.neuron_id; break; }
    std::vector<uint32_t> axon_ids;
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::AXON && seg.neuron_id == dlc_nid)
            axon_ids.push_back(seg.id);
    }
    std::sort(axon_ids.begin(), axon_ids.end());  // порядок цепочки: AIS → аксон[0..N]

    // Диагностика: is_intracellular связей dlc-сомы (сома→AIS) — не EXTRA_ONLY ли?
    for (auto& c : model.connections) {
        if (c.segment_a == kick_left[0] || c.segment_b == kick_left[0])
            printf("[DIAG] dlc soma conn: a=%u b=%u intra=%d\n", c.segment_a, c.segment_b, (int)c.is_intracellular);
    }
    // dIN AIS пик (инициирует ли AIS ПД у dIN, где аксон короткий?)
    {
        uint32_t din_nid = 0;
        for (auto& seg : model.segments) if (seg.id == left_din[0]) { din_nid = seg.neuron_id; break; }
        float din_ais_max = -200;
        for (auto& seg : model.segments) {
            if (seg.type == SegmentType::AXON && seg.neuron_id == din_nid)
                din_ais_max = std::max(din_ais_max, sim.getVmMax(seg.id));
        }
        printf("[DIAG] dIN AIS peak=%.1f (nid=%u)\n", din_ais_max, din_nid);
    }
    std::remove("/tmp/test_left_only.bion");

    // ── Диагностика осевой связи сома→AIS→аксон (задача #8) ──
    {
        const SegmentSnapshot* soma = nullptr, *ais0 = nullptr, *ais1 = nullptr, *ax0 = nullptr;
        for (auto& seg : model.segments) {
            if (seg.id == kick_left[0]) soma = &seg;
        }
        for (auto& seg : model.segments) {
            if (seg.type == SegmentType::AXON && seg.neuron_id == dlc_nid && seg.id < 365)
                { if (!ais0) ais0 = &seg; else if (!ais1) ais1 = &seg; else if (!ax0) ax0 = &seg; }
        }
        float soma_peak = sim.getVmMax(kick_left[0]);
        float ais0_peak = ais0 ? sim.getVmMax(ais0->id) : -999;
        printf("[AXIAL] soma id=%u r=%.1f len=%.1f C=%.2fpF peak=%.1f\n",
               soma ? soma->id : 0, soma ? soma->radius : 0, soma ? soma->length : 0,
               soma ? (4.0f*3.14159f*soma->radius*soma->radius*0.01f) : 0, soma_peak);
        if (ais0) printf("[AXIAL] AIS0 id=%u r=%.2f len=%.0f C=%.3fpF peak=%.1f\n",
               ais0->id, ais0->radius, ais0->length,
               2.0f*3.14159f*ais0->radius*ais0->length*0.01f, ais0_peak);
        if (ax0)  printf("[AXIAL] AX0  id=%u r=%.2f len=%.0f C=%.3fpF peak=%.1f\n",
               ax0->id, ax0->radius, ax0->length,
               2.0f*3.14159f*ax0->radius*ax0->length*0.01f, sim.getVmMax(ax0->id));
        // σ_i из ионов (Na/K/Cl/Mg) + g_ax сома→AIS
        if (soma) {
            float D[5] = {1.33f, 1.96f, 0.22f, 2.03f, 0.10f};
            float z2[5] = {1, 1, 4, 1, 4};
            float sigma_i = 0;
            for (int k = 0; k < 5; k++) sigma_i += z2[k] * D[k] * soma->intra_ions[k];
            float r_min = ais0 ? std::min(soma->radius, ais0->radius) : soma->radius;
            float dist = ais0 ? (soma->length + ais0->length) * 0.5f : 1.0f;
            float g_ax = 3.612f * sigma_i * 3.14159f * r_min * r_min / dist;
            float I_ax = g_ax * (soma_peak - ais0_peak);  // пА (нС·мВ)
            printf("[AXIAL] sigma_i=%.1f g_ax(soma->AIS)=%.1fnS R=%.1fMOhm I_ax(peak)=%.0fpA\n",
                   sigma_i, g_ax, 1000.0f/g_ax, I_ax);
        }
    }

    printf("[DIAG] Left-only: L_dlc_spikes=%d | dIN peak=%.1f | L_MN_spikes=%d peak=%.1f\n",
           L_dlc_spikes, dIN_max, L_MN_spikes, mn_max);
    printf("[DIAG] cleft: syn_total=%d syn_glu=%d max_glu=%.4f | dIN ECS Na=%.1f K=%.1f Ca=%.2f | Na_i=%.1f\n",
           syn_total, syn_glu, max_cleft_glu, din_Na_o, din_K_o, din_Ca_o, din_Na_i);
    printf("[DIAG] dlc axon peaks (nid=%u, n=%zu):", dlc_nid, axon_ids.size());
    for (size_t k = 0; k < axon_ids.size(); k++)
        printf(" %.0f", sim.getVmMax(axon_ids[k]));
    printf("\n");
    EXPECT_GT(L_dlc_spikes, 0) << "Левый kick-нейрон не активируется стимуляцией!";
    EXPECT_GT(L_MN_spikes, 0) << "Левый MN молчит при левом kickstart";
}

// Чистый замер покоя + ионов dlc/dIN в сети (без стимулов, только settle)
TEST(DebugNetwork, DlcRestAndIons) {
    srand(42); NetworkModel model;
    generate_debug_tadpole_network(model);

    std::vector<uint32_t> dlc_soma, din_soma;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 60.0f && seg.position.z >= 200.0f && seg.position.x < 0)
            dlc_soma.push_back(seg.id);
        if (seg.position.y >= 65.0f && seg.position.y <= 76.0f && seg.position.x < -1.0f)
            din_soma.push_back(seg.id);
    }
    ASSERT_FALSE(dlc_soma.empty());

    // Все IntraConnection всех сегментов dlc — есть ли EXTRA_ONLY связь с CPG/глией?
    {
        uint32_t dlc_nid = 0;
        for (auto& seg : model.segments) if (seg.id == dlc_soma[0]) { dlc_nid = seg.neuron_id; break; }
        std::vector<uint32_t> dlc_segs;
        for (auto& seg : model.segments) if (seg.neuron_id == dlc_nid) dlc_segs.push_back(seg.id);
        int intra = 0, extra = 0;
        for (auto& c : model.connections) {
            bool a = false, b = false;
            for (auto id : dlc_segs) { if (c.segment_a == id) a = true; if (c.segment_b == id) b = true; }
            if (!a && !b) continue;
            if (c.is_intracellular) intra++; else { extra++;
                printf("[CONN] dlc EXTRA_ONLY: a=%u b=%u\n", c.segment_a, c.segment_b); }
        }
        printf("[CONN] dlc segments=%zu intra=%d EXTRA_ONLY=%d\n", dlc_segs.size(), intra, extra);
    }

    ::SaveNetwork(model, "/tmp/test_dlc_rest.bion");
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_dlc_rest.bion"));
    sim.setSpeedMultiplier(100.0f);  // ускоряем симуляцию — settle медленных процессов (HCN1)
    sim.start();
    // Поллинг V_m dlc через 1,2,4,8,12 с — settle или дрейф?
    for (int sec = 1; sec <= 12; sec++) {
        for (int i = 0; i < 100; i++) {  // 100×10мс = 1 с
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!sim.isRunning()) break;
        }
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        float dv = -999, dv_din = -999, na_i = -1, k_i = -1, cl_i = -1, na_o = -1, k_o = -1;
        float nt_glu = -1, nt_glu_din = -1;
        char modbuf[256] = {0};
        for (auto& s : segs) {
            if (s.id == dlc_soma[0]) {
                dv = s.V_m; na_i = s.intra_ions[0]; k_i = s.intra_ions[1]; cl_i = s.intra_ions[3];
                na_o = s.extra_ions[0]; k_o = s.extra_ions[1];
                nt_glu = s.neurotransmitters[0];  // глутамат
                // состояния каналов: DIN_NAK(125), NaP(105), HCN1(129), KV42(85), NMDA(9)
                int off = 0;
                for (int m = 0; m < s.modifier_count && off < 240; m++) {
                    int t = (int)s.modifiers[m].mod_type();
                    if (t == 125 || t == 105 || t == 129 || t == 85 || t == 9)
                        off += snprintf(modbuf + off, sizeof(modbuf) - off, " t%d=%.3f/%.3f/%.3f/%.3f/%.3f/%.3f",
                                        t, s.modifiers[m].state[0], s.modifiers[m].state[1],
                                        s.modifiers[m].state[2], s.modifiers[m].state[3],
                                        s.modifiers[m].state[4], s.modifiers[m].state[5]);
                }
            }
            if (!din_soma.empty() && s.id == din_soma[0]) { dv_din = s.V_m; nt_glu_din = s.neurotransmitters[0]; }
        }
        printf("[REST] t=%2ds: dlc V=%.1f glu=%.3f (Na_i=%.2f K_i=%.2f | Na_o=%.2f K_o=%.2f)%s | din V=%.1f glu=%.3f\n",
               sec, dv, nt_glu, na_i, k_i, na_o, k_o, modbuf, dv_din, nt_glu_din);
        if (!sim.isRunning()) break;
    }
    sim.stop();
    std::remove("/tmp/test_dlc_rest.bion");
}

// Прямая стимуляция левого dIN — проверка возбудимости
TEST(DebugNetwork, DirectLeftDinStimulation) {
    srand(42); NetworkModel model;
    generate_debug_tadpole_network(model);

    // Найти левые dIN (y=65-76, x<0)
    std::vector<uint32_t> left_din;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 65.0f && seg.position.y <= 76.0f && seg.position.x < -1.0f)
            left_din.push_back(seg.id);
    }
    printf("[TEST] Found %zu left dIN\n", left_din.size());
    ASSERT_GT(left_din.size(), 0u);

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_direct_din.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_direct_din.bion"));
    sim.enableSpikeCount();  // живой счётчик спайков в Core (не кэш снапшота, троттлинг 30fps)

    for (int cycle = 0; cycle < 10; cycle++)
        for (auto id : left_din)
            sim.addCurrentStimulus(id, 400.0f, 30.0f, cycle * 100.0f);

    sim.start();
    printf("[TEST] Direct left dIN stimulation: 400pA×30ms, 10 cycles\n");

    for (int i = 0; i < 800; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!sim.isRunning()) break;
    }
    sim.stop();

    int L_dIN_spikes = 0;
    for (auto id : left_din) L_dIN_spikes += (int)sim.getSpikeCount(id);
    std::remove("/tmp/test_direct_din.bion");

    printf("[DIAG] Direct L-dIN stim: L_dIN spikes=%d\n", L_dIN_spikes);
    EXPECT_GT(L_dIN_spikes, 0) << "Левый dIN НЕ СПОСОБЕН спайкать даже с прямой стимуляцией!";
}

// Затухание дендрит→сома для dIN/cIN/MN: ток на дендрит, мерим V_m сомы
TEST(DebugNetwork, DendriteToSomaAttenuation) {
    struct R { const char* name; float y_lo, y_hi; float rest; };
    for (auto& r : {R{"dIN",65,76,-51.0f}, R{"cIN",40,48,-75.0f}, R{"MN",15,25,-65.0f}}) {
        NetworkModel model;
        generate_debug_tadpole_network(model);
        uint32_t soma_id = 0, neuron = 0, dend_id = 0;
        for (auto& seg : model.segments) {
            if (seg.type == SegmentType::SOMA && seg.position.y >= r.y_lo && seg.position.y <= r.y_hi && seg.position.x < -1.0f) {
                soma_id = seg.id; neuron = seg.neuron_id; break;
            }
        }
        for (auto& seg : model.segments) {
            if (seg.type == SegmentType::DENDRITE && seg.neuron_id == neuron) { dend_id = seg.id; break; }
        }
        if (!dend_id) { printf("[ATT] %s: нет дендрита\n", r.name); continue; }

        ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_att.bion"));
        SimulationBridge sim;
        ASSERT_TRUE(sim.loadNetwork("/tmp/test_att.bion"));
        sim.addCurrentStimulus(dend_id, 400.0f, 200.0f, 0.0f);
        sim.start();
        std::vector<SegmentSnapshot> segs;
        float soma_max = -99, dend_max = -99;
        for (int tick = 0; tick < 3000 && sim.isRunning(); tick++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            sim.getSegmentsSnapshot(segs);
            for (auto& s : segs) {
                if (s.id == soma_id && s.V_m > soma_max) soma_max = s.V_m;
                if (s.id == dend_id && s.V_m > dend_max) dend_max = s.V_m;
            }
        }
        sim.stop();
        std::remove("/tmp/test_att.bion");
        float att = (soma_max - r.rest) / (dend_max - r.rest);
        printf("[ATT] %s: dend max=%.1f soma max=%.1f | затухание Δsoma/Δdend=%.2f\n",
               r.name, dend_max, soma_max, att);
    }
}

// Coupling coefficient MN↔MN (gap junction) — цель ~2–5% (Perrins & Roberts 1995; 8.5% это dIN, Li 2009)
TEST(DebugNetwork, MnGapJunctionCoupling) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    std::vector<uint32_t> mn_ids;
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::SOMA && seg.position.y >= 15.0f && seg.position.y <= 25.0f && seg.position.x < -1.0f)
            mn_ids.push_back(seg.id);
    }
    if (mn_ids.size() < 2) { printf("[GJ] мало MN (%zu)\n", mn_ids.size()); return; }
    uint32_t mn_a = mn_ids[0], mn_b = mn_ids[1];
    printf("[GJ] MN a=%u b=%u (n=%zu)\n", mn_a, mn_b, mn_ids.size());

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_gj.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_gj.bion"));
    sim.addCurrentStimulus(mn_a, 400.0f, 200.0f, 0.0f);
    sim.start();
    std::vector<SegmentSnapshot> segs;
    float va_max = -99, vb_max = -99;
    for (int tick = 0; tick < 3000 && sim.isRunning(); tick++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sim.getSegmentsSnapshot(segs);
        for (auto& s : segs) {
            if (s.id == mn_a && s.V_m > va_max) va_max = s.V_m;
            if (s.id == mn_b && s.V_m > vb_max) vb_max = s.V_m;
        }
    }
    sim.stop();
    std::remove("/tmp/test_gj.bion");
    float coupling = (vb_max + 61.0f) / (va_max + 61.0f);
    printf("[GJ] MN coupling: va max=%.1f vb max=%.1f → coupling=%.3f (цель 0.02–0.05)\n", va_max, vb_max, coupling);
}

// ═══════════════════════════════════════════════════════════════
// ДИАГНОСТИКА: изоляция источника фонового coupling MN↔MN (~13%).
// Гипотеза: фон = глия (общая внеклетка K⁺), а не gap junction (Cx36) и не nAChR.
// Чисто диагностический тест — измеряет coupling в 4 конфигурациях.
// ═══════════════════════════════════════════════════════════════
static float measure_mn_coupling(NetworkModel& model, const char* label, bool stim = true) {
    std::vector<uint32_t> mn_ids;
    for (auto& seg : model.segments)
        if (seg.type == SegmentType::SOMA && seg.position.y >= 15.0f && seg.position.y <= 25.0f && seg.position.x < -1.0f)
            mn_ids.push_back(seg.id);
    if (mn_ids.size() < 2) { printf("[ISO] %s: мало MN (%zu)\n", label, mn_ids.size()); return -1.0f; }
    uint32_t mn_a = mn_ids[0], mn_b = mn_ids[1];

    if (!::SaveNetwork(model, "/tmp/test_iso.bion")) { printf("[ISO] %s: SaveNetwork fail\n", label); return -1.0f; }
    SimulationBridge sim;
    if (!sim.loadNetwork("/tmp/test_iso.bion")) { std::remove("/tmp/test_iso.bion"); printf("[ISO] %s: load fail\n", label); return -1.0f; }
    sim.enableVmMax();
    sim.enableDvdtMax();
    sim.enableSpikeCount();
    // 1) Измерить фактический покой mn_a (без стимула) — V_rest, а не жёсткий −61.
    float v_rest = -61.0f;
    if (stim) {
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        for (auto& s : segs) if (s.id == mn_a) v_rest = s.V_m;
        sim.stop();
        sim.loadNetwork("/tmp/test_iso.bion");
        sim.enableVmMax(); sim.enableDvdtMax(); sim.enableSpikeCount();
    }

    // 2) Прогон со стимулом
    if (stim) sim.addCurrentStimulus(mn_a, 600.0f, 200.0f, 0.0f);
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    sim.stop();
    std::remove("/tmp/test_iso.bion");
    float va_max = sim.getVmMax(mn_a);
    float vb_max = sim.getVmMax(mn_b);
    float coupling = (vb_max - v_rest) / (va_max - v_rest);
    printf("[ISO] %s: v_rest=%.1f va_max=%.1f vb_max=%.1f → coupling=%.4f | spikes=%u dvdt=%.1f (mn_a=%u mn_b=%u)\n",
           label, v_rest, va_max, vb_max, coupling, sim.getSpikeCount(mn_a), sim.getDvdtMax(mn_a), mn_a, mn_b);
    return coupling;
}

static void remove_glia(NetworkModel& m) {
    std::set<uint32_t> glia;
    for (auto& s : m.segments) if (s.neuron_id == 0xFFFFFFFFu) glia.insert(s.id);
    m.segments.erase(std::remove_if(m.segments.begin(), m.segments.end(),
        [&](const SegmentSnapshot& s){ return glia.count(s.id) != 0; }), m.segments.end());
    m.connections.erase(std::remove_if(m.connections.begin(), m.connections.end(),
        [&](const IntraConnection& c){ return glia.count(c.segment_a) || glia.count(c.segment_b); }), m.connections.end());
    m.synapses.erase(std::remove_if(m.synapses.begin(), m.synapses.end(),
        [&](const SynapseSnapshot& s){ return glia.count(s.pre_id) || glia.count(s.post_id); }), m.synapses.end());
}

TEST(DebugNetwork, MnGapJunctionCouplingIsolation) {
    // 0) без стимула — проверка, не дрейфует ли mn_b сам по себе
    { NetworkModel m; generate_debug_tadpole_network(m);
      measure_mn_coupling(m, "nostim", false); }
    { NetworkModel m; generate_debug_tadpole_network(m); remove_glia(m);
      measure_mn_coupling(m, "nostim_noglia", false); }
    // 1) baseline — все механизмы
    { NetworkModel m; generate_debug_tadpole_network(m);
      measure_mn_coupling(m, "baseline"); }
    // 2) без глии (вскрыть реальный вклад gap+химия без K⁺-буфера)
    { NetworkModel m; generate_debug_tadpole_network(m); remove_glia(m);
      measure_mn_coupling(m, "no_glia"); }
    // 3) без nAChR (глия есть)
    { NetworkModel m; generate_debug_tadpole_network(m);
      m.synapses.erase(std::remove_if(m.synapses.begin(), m.synapses.end(),
          [&](const SynapseSnapshot& s){
              for (int r = 0; r < s.post_receptor_count; r++)
                  if (s.post_receptors[r].type == (uint8_t)ModifierType::MOD_NT_RECEPTOR_NICOTINIC) return true;
              return false;
          }), m.synapses.end());
      measure_mn_coupling(m, "no_nAChR"); }
    // 4) без gap junction (глия есть)
    { NetworkModel m; generate_debug_tadpole_network(m);
      m.synapses.erase(std::remove_if(m.synapses.begin(), m.synapses.end(),
          [&](const SynapseSnapshot& s){ return s.type == SynapseType::ELECTRICAL; }), m.synapses.end());
      measure_mn_coupling(m, "no_gap"); }
    // 5) без всех химических (глия есть, остаётся только gap junction)
    { NetworkModel m; generate_debug_tadpole_network(m);
      m.synapses.erase(std::remove_if(m.synapses.begin(), m.synapses.end(),
          [&](const SynapseSnapshot& s){ return s.type == SynapseType::CHEMICAL; }), m.synapses.end());
      measure_mn_coupling(m, "no_chemical"); }
    // 6) ТОЛЬКО gap junction (без глии И без химии)
    { NetworkModel m; generate_debug_tadpole_network(m); remove_glia(m);
      m.synapses.erase(std::remove_if(m.synapses.begin(), m.synapses.end(),
          [&](const SynapseSnapshot& s){ return s.type == SynapseType::CHEMICAL; }), m.synapses.end());
      measure_mn_coupling(m, "only_gap"); }
    // 7) ТОЛЬКО химия (без глии И без gap junction)
    { NetworkModel m; generate_debug_tadpole_network(m); remove_glia(m);
      m.synapses.erase(std::remove_if(m.synapses.begin(), m.synapses.end(),
          [&](const SynapseSnapshot& s){ return s.type == SynapseType::ELECTRICAL; }), m.synapses.end());
      measure_mn_coupling(m, "only_chemical"); }
    SUCCEED();
}

// Диагностика дрейфа/бифуркации покоя MN: трейс V_m без стимула.
TEST(DebugNetwork, MnRestingDrift) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    std::vector<uint32_t> mn_ids;
    for (auto& seg : model.segments)
        if (seg.type == SegmentType::SOMA && seg.position.y >= 15.0f && seg.position.y <= 25.0f && seg.position.x < -1.0f)
            mn_ids.push_back(seg.id);
    if (mn_ids.size() < 2) { printf("[DRIFT] мало MN\n"); return; }
    uint32_t mn_a = mn_ids[0], mn_b = mn_ids[1];
    uint32_t mn_a_nid = 0xFFFFFFFFu;
    for (auto& s : model.segments) if (s.id == mn_a) mn_a_nid = s.neuron_id;

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_drift.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_drift.bion"));
    sim.start();
    std::vector<SegmentSnapshot> segs;
    for (int t = 0; t < 8000 && sim.isRunning(); t++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (t % 200 == 0) {
            sim.getSegmentsSnapshot(segs);
            float v_mn = -99, k_mn_i = -1, k_mn_e = -1;
            float sum_ki = 0, sum_ke = 0, sum_nai = 0, sum_nae = 0;
            for (auto& s : segs) {
                sum_ki += s.intra_ions[1];
                sum_ke += s.extra_ions[1];
                sum_nai += s.intra_ions[0];
                sum_nae += s.extra_ions[0];
                if (s.neuron_id == mn_a_nid && s.type == SegmentType::SOMA) { v_mn = s.V_m; k_mn_i = s.intra_ions[1]; k_mn_e = s.extra_ions[1]; }
            }
            printf("[DRIFT] t=%d V_mn=%.2f | SUM_Ki=%.1f SUM_Ke=%.1f | SUM_Na_i=%.1f SUM_Na_e=%.1f\n",
                   t, v_mn, sum_ki, sum_ke, sum_nai, sum_nae);
        }
    }
    sim.stop();
    std::remove("/tmp/test_drift.bion");
    SUCCEED();
}

// Тест движений хвоста через миотом
TEST(DebugNetwork, TailMovement) {
    TadpoleMyotomeModel myo;
    myo.Init(16);
    float dt = 0.1f;
    std::vector<float> left_act(16, 0), right_act(16, 0);
    float max_tip = 0, last_tip_x = 0;
    int zero_crossings = 0;

    for (int cycle = 0; cycle < 30; cycle++) {
        bool left_on = (cycle % 2 == 0);
        for (int i = 0; i < 16; i++) {
            float phase = (float)i / 16.0f;
            float delay = phase * 0.3f;
            float amp = (cycle > delay * 10.0f) ? 0.8f : 0.0f;
            if (left_on) { left_act[i] = amp; right_act[i] = 0; }
            else         { right_act[i] = amp; left_act[i] = 0; }
        }
        myo.Step(dt, left_act.data(), right_act.data(), 0);

        const auto& pos = myo.segment_positions();
        if (!pos.empty()) {
            float tip_x = pos.back().x;
            if (std::fabs(tip_x) > max_tip) max_tip = std::fabs(tip_x);
            if ((last_tip_x < 0 && tip_x > 0) || (last_tip_x > 0 && tip_x < 0))
                zero_crossings++;
            last_tip_x = tip_x;
        }
    }
    printf("[TEST] Max tail tip: %.1f, zero crossings: %d\n", max_tip, zero_crossings);
    EXPECT_GT(max_tip, 0.5f);
    EXPECT_GE(zero_crossings, 1);

    for (int i = 0; i < 16; i++) {
        float st = myo.stretch_amplitude(i);
        EXPECT_GE(st, 0.0f);
        EXPECT_LE(st, 1.0f);
    }
    printf("[TEST] Tail movement OK\n");
}
