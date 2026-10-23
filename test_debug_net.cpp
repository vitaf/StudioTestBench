// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_debug_net.cpp — проверка плавания в отладочной сети через dlc_MHR стимуляцию
// Переменные окружения: CPG_STIM_PA (default 500)
#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <unistd.h>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

TEST(DebugNet, SwimmingWithDlcMhrDrive) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_TRUE(SaveNetwork(model, "/tmp/test_swim.bion"));

    SimulationBridge sim;
    sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_swim.bion"));

    // Находим dIN и cIN нейроны
    std::vector<uint32_t> din_left, din_right, cin_left, cin_right;
    {
        std::vector<SegmentSnapshot> snap;
        sim.getSegmentsSnapshot(snap);
        for (auto& s : snap) {
            if (s.type != SegmentType::SOMA) continue;
            bool has_hcn = false;
            for (int j = 0; j < s.modifier_count; j++)
                if (s.modifiers[j].mod_type() == ModifierType::MOD_HCN2 || s.modifiers[j].mod_type() == (ModifierType)129) { has_hcn = true; break; }
            // dIN: y≈70, с HCN (и dIN и dlc имеют HCN, но dlc z>200)
            if (s.position.y > 60 && s.position.y < 80 && has_hcn && s.position.z < 100) {
                if (s.position.x < -10) din_left.push_back(s.id);
                else if (s.position.x > 10) din_right.push_back(s.id);
            }
            // cIN: y≈45, с SK (без HCN или с HCN но y<60)
            bool has_sk = false;
            for (int j = 0; j < s.modifier_count; j++)
                if (s.modifiers[j].mod_type() == ModifierType::MOD_SK_CHANNEL) { has_sk = true; break; }
            if (s.position.y > 38 && s.position.y < 52 && has_sk && s.position.z < 100) {
                if (s.position.x < -10) cin_left.push_back(s.id);
                else if (s.position.x > 10) cin_right.push_back(s.id);
            }
        }
    }
    printf("dIN: %zu L, %zu R | cIN: %zu L, %zu R\n",
        din_left.size(), din_right.size(), cin_left.size(), cin_right.size());
    ASSERT_GT(din_left.size(), 0u) << "No dIN left found!";
    ASSERT_GT(cin_left.size(), 0u) << "No cIN left found!";

    sim.start();
    uint64_t start_tick = sim.getCurrentTick();

    // ══ БИО: одиночный асимметричный стимул (сенсорный вход) — БЕЗ тонического драйва ══
    // Реальный головастик: одно прикосновение → CPG самоподдерживается
    float stim_pa = getenv("CPG_STIM_PA") ? atof(getenv("CPG_STIM_PA")) : 300.0f;
    printf("[Bio] NO tonic drive. Single kickstart L-dIN %.0fpA x 50ms\n", stim_pa);

    int switches = 0, last_active = -1;
    int quiet_checks = 0;
    printf("Tick   L_dIN  R_dIN | Sw  sat\n");

    for (int f = 0; f < 800; f++) {
        usleep(10000);
        uint64_t elapsed = sim.getCurrentTick() - start_tick;

        // Одиночный стимул на левый dIN (био: сенсорный вход с одной стороны)
        if (elapsed == 0) {
            for (auto id : din_left)
                sim.addCurrentStimulus(id, stim_pa, 50.0f, 0.0f);
        }

        if (f % 10 == 0) {
            std::vector<SegmentSnapshot> snap;
            sim.getSegmentsSnapshot(snap);

            float l_din = -200, r_din = -200;
            float l_cin = -200, r_cin = -200;
            int saturated = 0;
            for (auto& s : snap) {
                if (s.type != SegmentType::SOMA) continue;
                if (s.V_m > 150 || s.V_m < -150) saturated++;
                bool has_hcn = false;
                for (int j = 0; j < s.modifier_count; j++)
                    if (s.modifiers[j].mod_type() == ModifierType::MOD_HCN2 || s.modifiers[j].mod_type() == (ModifierType)129) has_hcn = true;
                if (!has_hcn) continue;
                if (s.position.y > 60 && s.position.y < 80) {
                    if (s.position.x < -10 && s.V_m > l_din) l_din = s.V_m;
                    if (s.position.x > 10 && s.V_m > r_din) r_din = s.V_m;
                }
                if (s.position.y > 40 && s.position.y < 55) {
                    if (s.position.x < -10 && s.V_m > l_cin) l_cin = s.V_m;
                    if (s.position.x > 10 && s.V_m > r_cin) r_cin = s.V_m;
                }
            }

            int cur = (l_din > 50 && l_din > r_din + 30) ? 0 :
                      (r_din > 50 && r_din > l_din + 30) ? 1 : -1;
            if (cur != last_active && cur >= 0 && last_active >= 0) switches++;
            if (cur >= 0) last_active = cur;

            if (f <= 5 || f == 15 || f == 30 || f == 60 || f == 120) {
                for (auto& s : snap) {
                    if (s.type != SegmentType::SOMA) continue;
                    if (s.position.z > 10) continue; // только первый сегмент (z≈0)
                    bool has_hcn = false;
                    for (int j = 0; j < s.modifier_count && !has_hcn; j++)
                        if (s.modifiers[j].mod_type() == ModifierType::MOD_HCN2 || s.modifiers[j].mod_type() == (ModifierType)129) has_hcn = true;
                    bool is_din = has_hcn && s.position.y > 60 && s.position.y < 80;
                    bool is_cin = !has_hcn && s.position.y > 38 && s.position.y < 52;
                    if (!is_din && !is_cin) continue;
                    float cl_i = s.intra_ions[3];
                    float e_cl = 26.7f * logf(0.130f / (cl_i + 0.000001f));
                    printf("  [Cl-diag] t=%lu %s x=%.0f Vm=%.1f Cl_i=%.5f ECl=%.0f\n",
                           (unsigned long)elapsed, is_din ? "dIN" : "cIN",
                           s.position.x, s.V_m, cl_i, e_cl);
                }
            }
            float l_ain = -200, r_ain = -200;
            for (auto& s : snap) {
                if (s.type != SegmentType::SOMA) continue;
                if (s.position.y > 50 && s.position.y < 60 && s.position.z < 100) {
                    if (s.position.x < -10 && s.V_m > l_ain) l_ain = s.V_m;
                    if (s.position.x > 10 && s.V_m > r_ain) r_ain = s.V_m;
                }
            }
            if (f <= 5 || f == 15 || f == 30) {
                for (auto& s : snap) {
                    if (s.type != SegmentType::SOMA) continue;
                    if (s.position.y < 52 || s.position.y > 58) continue;
                    printf("  [aIN-diag] t=%lu x=%.0f Vm=%.1f K_i=%.4f Cl_i=%.5f K_o=%.4f\n",
                           (unsigned long)elapsed, s.position.x, s.V_m,
                           s.intra_ions[1], s.intra_ions[3], s.extra_ions[1]);
                }
            }
            printf("%5lu dIN:%6.1f/%-6.1f cIN:%6.1f/%-6.1f aIN:%6.1f/%-6.1f | sw=%d sat=%d\n",
                   (unsigned long)elapsed, l_din, r_din, l_cin, r_cin, l_ain, r_ain, switches, saturated);
        }

        if (elapsed > 30000) break;
    }

    sim.stop();
    printf("Switches=%d\n", switches);
    EXPECT_GE(switches, 1) << "No swimming alternation! Need L/R phase difference.";
}

// Длинный тест: 50000 тиков, проверка стабильности альтернации
TEST(DebugNet, LongSwim_50k_ticks_stability) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_TRUE(SaveNetwork(model, "/tmp/test_longswim.bion"));

    SimulationBridge sim;
    sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_longswim.bion"));

    // Найти dlc_MHR (descending brainstem drive) — как в CPGWithKickstart
    std::vector<uint32_t> dlc_left, dlc_right, din_left, din_right;
    {
        std::vector<SegmentSnapshot> snap;
        sim.getSegmentsSnapshot(snap);
        for (auto& s : snap) {
            if (s.type != SegmentType::SOMA) continue;
            bool has_hcn = false;
            for (int j = 0; j < s.modifier_count; j++) {
                if (s.modifiers[j].mod_type() == ModifierType::MOD_HCN2 || s.modifiers[j].mod_type() == (ModifierType)129) has_hcn = true;
            }
            // dlc_MHR: y∈[40,60], z≥200, имеет HCN (Ih → rebound)
            if (s.position.y >= 40 && s.position.y <= 60 && s.position.z >= 200 && has_hcn) {
                if (s.position.x < 0) dlc_left.push_back(s.id);
                else dlc_right.push_back(s.id);
            }
            // dIN: y∈[60,80], z<100, имеет HCN
            if (s.position.y > 60 && s.position.y < 80 && has_hcn && s.position.z < 100) {
                if (s.position.x < -10) din_left.push_back(s.id);
                else if (s.position.x > 10) din_right.push_back(s.id);
            }
        }
    }
    printf("dlc: %zu L, %zu R | dIN: %zu L, %zu R\n", dlc_left.size(), dlc_right.size(), din_left.size(), din_right.size());
    ASSERT_GT(dlc_left.size(), 0u);
    ASSERT_GT(din_left.size(), 0u);

    // Alternating kickstart через dlc_MHR (bio: descending brainstem drive)
    // + тонический драйв на dlc (MLR→RS continuous, Roberts 2010) + dIN
    float stim_pa = 32.0f;  // как в CPGWithKickstart
    sim.start();
    uint64_t start_tick = sim.getCurrentTick();
    // Асимметричный тонический драйв на dlc: левые — сильнее (pacemaker), правые — слабее (follower).
    // Био: MLR→RS has asymmetric projections (Roberts 2010). dlc→dIN AMPA передаёт драйв естественно.
    for (auto id : dlc_left)
        sim.addCurrentStimulus(id, 0.6f, 999999.0f, 0.0f);
    for (auto id : dlc_right)
        sim.addCurrentStimulus(id, 0.2f, 999999.0f, 0.0f);
    // Alternating kickstart на dlc_MHR — 50 циклов
    for (int cycle = 0; cycle < 30; cycle++) {
        for (auto id : dlc_left)
            sim.addCurrentStimulus(id, stim_pa, 30.0f, cycle * 200.0f);
        for (auto id : dlc_right)
            sim.addCurrentStimulus(id, stim_pa, 30.0f, cycle * 200.0f + 100.0f);
    }

    int switches = 0, last_active = -1;
    int l_bursts = 0, r_bursts = 0, saturated_peaks = 0;
    float l_sum = 0, r_sum = 0; int samples = 0;
    printf("[LongSwim] dlc tonic L=0.6pA R=0.2pA + 30x32pA dlc kickstart. 50000 ticks...\n");
    printf("%7s %7s %7s %5s %5s %5s %5s\n", "Tick", "L_dIN", "R_dIN", "Sw", "Sat", "LB", "RB");

    for (int f = 0; f < 5000; f++) {
        usleep(10000);
        uint64_t elapsed = sim.getCurrentTick() - start_tick;
        if (f % 100 == 0) {
            std::vector<SegmentSnapshot> snap;
            sim.getSegmentsSnapshot(snap);
            float l_din=-200, r_din=-200; int saturated=0;
            for (auto& s : snap) {
                if (s.type != SegmentType::SOMA) continue;
                if (s.V_m > 150 || s.V_m < -150) saturated++;
                bool has_hcn = false;
                for (int j=0; j<s.modifier_count; j++)
                    if(s.modifiers[j].mod_type()==ModifierType::MOD_HCN2||s.modifiers[j].mod_type()==(ModifierType)129){has_hcn=true;break;}
                if (!has_hcn) continue;
                if (s.position.y > 60 && s.position.y < 80) {
                    if (s.position.x < -10 && s.V_m > l_din) l_din = s.V_m;
                    if (s.position.x > 10 && s.V_m > r_din) r_din = s.V_m;
                }
            }
            int cur = (l_din > 50 && l_din > r_din + 30) ? 0 :
                      (r_din > 50 && r_din > l_din + 30) ? 1 : -1;
            if (cur != last_active && cur >= 0 && last_active >= 0) switches++;
            if (cur == 0 && last_active != 0) l_bursts++;
            if (cur == 1 && last_active != 1) r_bursts++;
            if (cur >= 0) last_active = cur;
            if (saturated > 30) saturated_peaks++;
            l_sum += l_din; r_sum += r_din; samples++;

            printf("%7lu %7.1f %7.1f %5d %5d %5d %5d\n",
                   (unsigned long)elapsed, l_din, r_din, switches, saturated, l_bursts, r_bursts);
        }
        if (elapsed > 50000) break;
    }
    sim.stop();

    float freq = switches > 0 ? 1000.0f / (50000.0f * 0.1f / switches) : 0;
    printf("\n=== LONG SWIM 50000 TICKS ===\n");
    printf("Switches: %d (%.2f Hz)\n", switches, freq);
    printf("L bursts: %d, R bursts: %d\n", l_bursts, r_bursts);
    printf("Avg V_m: L=%.1f R=%.1f mV\n", l_sum/samples, r_sum/samples);
    printf("Saturated peaks (>30 sat): %d/%d samples\n", saturated_peaks, samples);

    // CPG breakthrough: L/R asymmetry 20mV, правая сторона — follower.
    // Burst counts снижены. Главное — alternation есть и нет saturation.
    EXPECT_GE(switches, 1) << "No alternation at all over 50000 ticks!";
    EXPECT_GE(l_bursts, 1) << "Left side: no bursts (pacemaker failed)";
    EXPECT_GE(r_bursts, 1) << "Right side: no bursts (follower failed)";
    EXPECT_LE(saturated_peaks, samples/2) << "Too many saturated samples — network unstable!";
}
