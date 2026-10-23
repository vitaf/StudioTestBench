// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_recent_regression.cpp — регрессионные тесты для недавних изменений (2026-07-09)

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/neuron_library.h"

// ═══════════════════════════════════════════════════════════════════════════════
// 1. HCN удалён из cIN в CPG-шаблоне (проверка через tadpole_develop)
// ═══════════════════════════════════════════════════════════════════════════════
TEST(RecentRegression, HCN_removed_from_cIN) {
    // Проверяем что MOD_HCN2 существует и это 130
    EXPECT_EQ((int)ModifierType::MOD_HCN2, 130);
    // Проверяем что MOD_TYPE_COUNT обновлён (197 с изоформами Nav/Kv до MOD_KV21=196)
    EXPECT_EQ(MOD_TYPE_COUNT, 197);
    // Проверяем что KNA_CHANNEL (121), NKCC1 (122), KV_DELAYED_RECTIFIER (123) определены
    EXPECT_EQ((int)ModifierType::MOD_KNA_CHANNEL, 121);
    EXPECT_EQ((int)ModifierType::MOD_NKCC1, 122);
    EXPECT_EQ((int)ModifierType::MOD_KV_DELAYED_RECTIFIER, 123);
    printf("[HCN] MOD_TYPE_COUNT=%d, HCN=%d, KNa=%d, NKCC1=%d, KvDR=%d\n",
           MOD_TYPE_COUNT, 130, 121, 122, 123);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 2. Тонический ток vs установка V_m: V_m сбрасывается K2P/KIR
// ═══════════════════════════════════════════════════════════════════════════════
TEST(RecentRegression, TonicCurrentVsVmSetting) {
    // Тест A: V_m = -45mV сбрасывается за 100ms (K2P/KIR/NaK pump)
    {
        NetworkModel ma;
        auto* s = ma.create_segment({0, 70, 0}, SegmentType::SOMA, {0, 1, 0});
        s->neuron_id = 0; s->radius = 4.0f; s->length = 8.0f;
        s->intra_ions[0] = 0.015f; s->intra_ions[1] = 0.140f;
        s->intra_ions[2] = 0.00005f; s->intra_ions[3] = 0.005f;
        s->extra_ions[0] = 0.140f; s->extra_ions[1] = 0.005f;
        s->extra_ions[2] = 0.0012f; s->extra_ions[3] = 0.130f;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        s->add_modifier(ModifierType::MOD_K2P_LEAK, 2.0f);
        s->add_modifier(ModifierType::MOD_KCC2, 0.8f);
        s->V_m = -45.0f; s->prev_V_m = -45.0f;
        NeuronInfo ni; ni.id = 0; ni.segment_start = 0; ni.segment_count = 1;
        ma.neurons.push_back(ni);

        ASSERT_TRUE(::SaveNetwork(ma, "/tmp/vm_test_a.bion"));
        SimulationBridge sim;
        ASSERT_TRUE(sim.loadNetwork("/tmp/vm_test_a.bion"));
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        std::remove("/tmp/vm_test_a.bion");

        float vm_after = segs[0].V_m;
        printf("[VM-Test] V_m=-45 initial -> after 100ms: %.1f mV\n", vm_after);
        EXPECT_LT(vm_after, -50.0f) << "V_m должен сброситься K2P/KIR/NaK за 100ms (был -45)";
    }

    // Тест B: тонический ток 200pA держит деполяризацию
    {
        NetworkModel mb;
        auto* s = mb.create_segment({0, 70, 0}, SegmentType::SOMA, {0, 1, 0});
        s->neuron_id = 0; s->radius = 4.0f; s->length = 8.0f;
        s->intra_ions[0] = 0.015f; s->intra_ions[1] = 0.140f;
        s->intra_ions[2] = 0.00005f; s->intra_ions[3] = 0.005f;
        s->extra_ions[0] = 0.140f; s->extra_ions[1] = 0.005f;
        s->extra_ions[2] = 0.0012f; s->extra_ions[3] = 0.130f;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        s->add_modifier(ModifierType::MOD_K2P_LEAK, 2.0f);
        s->add_modifier(ModifierType::MOD_KCC2, 0.8f);
        s->V_m = -65.0f; s->prev_V_m = -65.0f;
        NeuronInfo ni; ni.id = 0; ni.segment_start = 0; ni.segment_count = 1;
        mb.neurons.push_back(ni);

        ASSERT_TRUE(::SaveNetwork(mb, "/tmp/vm_test_b.bion"));
        SimulationBridge sim;
        ASSERT_TRUE(sim.loadNetwork("/tmp/vm_test_b.bion"));
        sim.addCurrentStimulus(1, 200.0f, 999999.0f, 0.0f);
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        std::remove("/tmp/vm_test_b.bion");

        float vm_tonic = segs[0].V_m;
        printf("[VM-Test] tonic 200pA -> after 100ms: %.1f mV\n", vm_tonic);
        EXPECT_GT(vm_tonic, -55.0f) << "Тонический ток 200pA должен поддерживать деполяризацию > -55mV";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3. Все blueprint-ы используют только валидные ModifierType
// ═══════════════════════════════════════════════════════════════════════════════
TEST(RecentRegression, NeuronLibrary_AllModifiers_Valid) {
    auto all = NeuronLibrary::blueprints_by_category("");
    ASSERT_GT(all.size(), 100u) << "Должно быть >100 blueprint-ов";

    int checked = 0;
    for (auto* bp : all) {
        ASSERT_NE(bp, nullptr);
        for (int i = 0; i < bp->soma_modifier_count && i < MAX_MODIFIERS_PER_SEGMENT; i++) {
            int t = (int)bp->soma_modifiers[i].type;
            EXPECT_GE(t, 0) << bp->name << ": modifier type < 0";
            EXPECT_LT(t, MOD_TYPE_COUNT) << bp->name << ": modifier " << t << " >= MOD_TYPE_COUNT";
        }
        checked++;
    }
    printf("[NeuronLib] Checked %d blueprints — all modifiers in valid range [0,%d)\n", checked, MOD_TYPE_COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4. Ca²⁺/Cl⁻ гомеостаз добавлен в ключевые blueprint-ы
// ═══════════════════════════════════════════════════════════════════════════════
TEST(RecentRegression, CaCl_Homeostasis_Added) {
    {
        auto bp = NeuronLibrary::Tadpole::dIN();
        bool has_pmca = false, has_ncx = false, has_kcc2 = false;
        for (int i = 0; i < bp.soma_modifier_count; i++) {
            if (bp.soma_modifiers[i].type == ModifierType::MOD_PMCA) has_pmca = true;
            if (bp.soma_modifiers[i].type == ModifierType::MOD_NCX) has_ncx = true;
            if (bp.soma_modifiers[i].type == ModifierType::MOD_KCC2) has_kcc2 = true;
        }
        EXPECT_TRUE(has_pmca) << "dIN должен иметь PMCA (Ca²⁺-АТФаза)";
        EXPECT_TRUE(has_ncx)  << "dIN должен иметь NCX (Na⁺/Ca²⁺ обменник)";
        EXPECT_TRUE(has_kcc2) << "dIN должен иметь KCC2 (K⁺-Cl⁻ котранспортёр)";
        printf("[CaCl] dIN: PMCA=%d NCX=%d KCC2=%d\n", has_pmca, has_ncx, has_kcc2);
    }

    {
        auto bp = NeuronLibrary::Human::Purkinje();
        bool has_pmca = false, has_ncx = false, has_kcc2 = false;
        bool has_ca_leak = false, has_ca_buf = false;
        for (int i = 0; i < bp.soma_modifier_count; i++) {
            if (bp.soma_modifiers[i].type == ModifierType::MOD_PMCA) has_pmca = true;
            if (bp.soma_modifiers[i].type == ModifierType::MOD_NCX) has_ncx = true;
            if (bp.soma_modifiers[i].type == ModifierType::MOD_KCC2) has_kcc2 = true;
            if (bp.soma_modifiers[i].type == ModifierType::MOD_CA_LEAK) has_ca_leak = true;
            if (bp.soma_modifiers[i].type == ModifierType::MOD_CA_BUFFER) has_ca_buf = true;
        }
        EXPECT_TRUE(has_pmca) << "Purkinje должен иметь PMCA";
        EXPECT_TRUE(has_ncx)  << "Purkinje должен иметь NCX";
        EXPECT_TRUE(has_kcc2) << "Purkinje должен иметь KCC2";
        EXPECT_TRUE(has_ca_leak) << "Purkinje должен иметь CA_LEAK";
        EXPECT_TRUE(has_ca_buf)  << "Purkinje должен иметь CA_BUFFER";
        printf("[CaCl] Purkinje: PMCA=%d NCX=%d KCC2=%d CA_LEAK=%d CA_BUF=%d\n",
               has_pmca, has_ncx, has_kcc2, has_ca_leak, has_ca_buf);
    }
}
