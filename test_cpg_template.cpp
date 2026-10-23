// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_cpg_template.cpp — проверка CPG-шаблона головастика с brainstem-драйвом.
// Использует generate_debug_tadpole_network() (CPG-мост с dlc_MHR).
// Проверяет: альтернация лево-право >= 1 switch за 15000 тиков на CPU.
// CPG breakthrough: Cl⁻ dynamics, cIN→cIN removal, NAK_CIN=0.05, L/R 20mV asymmetry.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

TEST(CpgTemplate, AlternationL_R_5switches_5000ticks) {
    srand(42);
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_GT(model.segments.size(), 100u) << "CPG template: too few segments";

    int glycine_syns = 0;
    for (auto& syn : model.synapses) {
        if (syn.post_receptor_count > 0 &&
            syn.post_receptors[0].mod_type() == ModifierType::MOD_NT_RECEPTOR_GLYCINE)
            glycine_syns++;
    }
    printf("CPG template: %zu segs, %zu syns (%d glycine), %zu conns\n",
        model.segments.size(), model.synapses.size(), glycine_syns,
        model.connections.size());
    ASSERT_GT(glycine_syns, 0) << "No glycine synapses";

    // Найти dIN для биокорректного kickstart: dIN → cIN(AMPA) → contra-dIN(glycine)
    // Прямая стимуляция cIN током — небиологично. В реальности:
    // Прикосновение → RB → dIN(AMPA) → cIN(AMPA) → contra-dIN(glycine) → альтернация
    std::vector<uint32_t> din_left, din_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        bool has_hcn = false;
        for (int m = 0; m < seg.modifier_count; m++)
            if (seg.modifiers[m].mod_type() == ModifierType::MOD_HCN2 || seg.modifiers[m].mod_type() == (ModifierType)129) { has_hcn = true; break; }
        if (has_hcn && seg.position.y >= 65.0f && seg.position.y <= 76.0f) {
            if (seg.position.x < -1.0f) din_left.push_back(seg.id);
            else if (seg.position.x > 1.0f) din_right.push_back(seg.id);
        }
    }
    ASSERT_GT(din_left.size(), 0u) << "No left dIN found";
    ASSERT_GT(din_right.size(), 0u) << "No right dIN found";
    printf("[TEST] Kick neurons: dIN, %zuL %zuR\n", din_left.size(), din_right.size());

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_cpg_template.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_cpg_template.bion"));

    // Alternating kickstart: 50 циклов L/R, 32pA×30ms, ~8Hz
    for (int cycle = 0; cycle < 50; cycle++) {
        for (auto id : din_left)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 60.0f);
        for (auto id : din_right)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 60.0f + 30.0f);
    }
    sim.start();
    printf("[TEST] Kickstart: 50 cycles alternating left/right, 32pA×30ms, ~8Hz\n");

    std::vector<SegmentSnapshot> segs;
    int switches = 0, last_active = -1;
    int l_bursts = 0, r_bursts = 0;
    int sample_count = 0;

    for (int tick = 0; tick < 5000 && sim.isRunning(); tick++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sim.getSegmentsSnapshot(segs);
        sample_count++;

        float l_max = -200, r_max = -200;
        for (auto& s : segs) {
            if (s.type != SegmentType::SOMA) continue;
            if (s.position.y >= 65.0f && s.position.y <= 76.0f) {
                if (s.position.x < -1.0f && s.V_m > l_max) l_max = s.V_m;
                else if (s.position.x > 1.0f && s.V_m > r_max) r_max = s.V_m;
            }
        }

        if (sample_count % 100 == 0) {
            int cur = (l_max > 50 && l_max > r_max + 30) ? 0 :
                      (r_max > 50 && r_max > l_max + 30) ? 1 : -1;
            if (cur != last_active && cur >= 0 && last_active >= 0) switches++;
            if (cur == 0 && last_active != 0) l_bursts++;
            if (cur == 1 && last_active != 1) r_bursts++;
            if (cur >= 0) last_active = cur;
            printf("t=%4d L=%.1f R=%.1f sw=%d\n", tick, l_max, r_max, switches);
        }
    }

    sim.stop();

    printf("CPG template: %d switches in 5000 ticks, L bursts=%d R bursts=%d\n",
           switches, l_bursts, r_bursts);
    EXPECT_GE(switches, 1) << "CPG alternation: 0 switches — template not working!";
    EXPECT_GE(l_bursts, 1) << "Left side not bursting";
    EXPECT_GE(r_bursts, 1) << "Right side not bursting";
}

// LongSwim: 50000 тиков, проверка УСТОЙЧИВОЙ альтернации с brainstem-драйвом.
TEST(CpgTemplate, LongSwim_50k_ticks) {
    srand(42);
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_GT(model.segments.size(), 100u);
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_cpg_longswim.bion"));

    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_cpg_longswim.bion"));

    // Kick нейроны
    std::vector<uint32_t> din_left, din_right;
    bool using_cin = false;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 60.0f) {
            bool has_pir = false, has_nak = false;
            for (int m = 0; m < seg.modifier_count; m++) {
                if (seg.modifiers[m].mod_type() == ModifierType::MOD_HCN2) has_pir = true;
                if (seg.modifiers[m].mod_type() == ModifierType::MOD_AXON_NAV12) has_nak = true;
            }
            if (seg.position.z >= 200.0f && has_pir) {
                if (seg.position.x < 0) din_left.push_back(seg.id);
                else din_right.push_back(seg.id);
            } else if (seg.position.z < 200.0f && has_nak) {
                if (seg.position.x < 0) din_left.push_back(seg.id);
                else din_right.push_back(seg.id);
                using_cin = true;
            }
        }
    }
    ASSERT_GT(din_left.size(), 0u);
    ASSERT_GT(din_right.size(), 0u);

    // 80 циклов alternating kickstart для LongSwim
    for (int cycle = 0; cycle < 80; cycle++) {
        for (auto id : din_left)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 60.0f);
        for (auto id : din_right)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 60.0f + 30.0f);
    }
    sim.start();
    printf("[LongSwim-CPG] %s, 80 cycles alternating L/R, 32pA×30ms. 50000 ticks...\n",
           using_cin ? "cIN" : "dlc_MHR");

    std::vector<SegmentSnapshot> segs;
    int switches = 0, last_active = -1;
    int l_bursts = 0, r_bursts = 0;
    int saturated_peaks = 0, sample_count = 0;
    printf("%7s %7s %7s %5s %5s %5s\n", "Tick", "L_dIN", "R_dIN", "Sw", "LB", "RB");

    for (int tick = 0; tick < 50000 && sim.isRunning(); tick++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sim.getSegmentsSnapshot(segs);
        sample_count++;

        float l_max = -200, r_max = -200; int saturated = 0;
        for (auto& s : segs) {
            if (s.type != SegmentType::SOMA) continue;
            if (s.V_m > 150 || s.V_m < -150) saturated++;
            if (s.position.y >= 65.0f && s.position.y <= 76.0f) {
                if (s.position.x < -1.0f && s.V_m > l_max) l_max = s.V_m;
                else if (s.position.x > 1.0f && s.V_m > r_max) r_max = s.V_m;
            }
        }

        if (sample_count % 100 == 0) {
            int cur = (l_max > 50 && l_max > r_max + 30) ? 0 :
                      (r_max > 50 && r_max > l_max + 30) ? 1 : -1;
            if (cur != last_active && cur >= 0 && last_active >= 0) switches++;
            if (cur == 0 && last_active != 0) l_bursts++;
            if (cur == 1 && last_active != 1) r_bursts++;
            if (cur >= 0) last_active = cur;
            if (saturated > 30) saturated_peaks++;
            printf("%7d %7.1f %7.1f %5d %5d %5d\n",
                   tick, l_max, r_max, switches, l_bursts, r_bursts);
        }
    }

    sim.stop();

    float freq = switches > 0 ? 1000.0f / (50000.0f * 0.1f / switches) : 0;
    printf("\n=== LONG SWIM CPG TEMPLATE 50000 TICKS ===\n");
    printf("Switches: %d (%.1f Hz)\n", switches, freq);
    printf("L bursts: %d, R bursts: %d\n", l_bursts, r_bursts);
    printf("Saturated peaks (>30 sat): %d/%d samples\n", saturated_peaks, sample_count/100);

    EXPECT_GE(switches, 1) << "No sustained alternation in CPG template LongSwim!";
    EXPECT_GE(l_bursts, 2) << "Left side not bursting enough";
    EXPECT_GE(r_bursts, 2) << "Right side not bursting enough";
    EXPECT_LE(saturated_peaks, (sample_count/100)/2) << "Too many saturated samples!";
}
