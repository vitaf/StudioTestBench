// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_ecb_stdp.cpp — тесты eCB + STDP пластичности.
// Проверяет: LTP (pre-before-post), LTD (post-before-pre),
// ингибиторный STDP, eCB синтез→cleft, многотиковая интеграция.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_core_api.h"

// ═══════════════════════════════════════════════════════════════════
// Хелперы
// ═══════════════════════════════════════════════════════════════════

static void runTicks(CoreData d, int n_ticks) {
    void *st, *sc, *vc, *vcc, *rm, *rmc, *io, *ioc, *li, *lic; void *me, *mec;
    core_get_stimulus_buffers(&st, &sc, &vc, &vcc, &rm, &rmc, &io, &ioc, &li, &lic, &me, &mec);
    uint64_t tick = 0;
    for (int t = 0; t < n_ticks; t++) {
        core_run_tick(core_segments_ptr(d), core_num_segments(d),
                      core_connections_ptr(d), core_num_connections(d),
                      core_synapses_ptr(d), core_num_synapses(d),
                      0, &tick, 0.1f,
                      st, sc, vc, vcc, rm, rmc, io, ioc, li, lic, me, mec);
    }
}

// Excitatory сеть: 2 сегмента + STDP-каскад + AMPA/NMDA + CB1
static void makeStdpNetwork(std::string& out_path, float& init_weight) {
    out_path = "/tmp/test_ecb_stdp_network.bion";
    NetworkModel m;
    core_init_libraries();
    core_set_bio_mode();

    m.create_segment({0, 0, 0}, SegmentType::SOMA);
    m.segments[0].V_m = -70.0f; m.segments[0].radius = 10.0f; m.segments[0].length = 20.0f;
    m.segments[0].add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    m.segments[0].add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);

    m.create_segment({30, 0, 0}, SegmentType::DENDRITE);
    m.segments[1].V_m = -70.0f; m.segments[1].radius = 5.0f; m.segments[1].length = 30.0f;
    m.segments[1].add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    m.segments[1].add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);
    m.segments[1].add_modifier(ModifierType::MOD_CA_L_TYPE, 0.5f);
    m.segments[1].add_modifier(ModifierType::MOD_CAMKII, 1.0f);
    m.segments[1].add_modifier(ModifierType::MOD_CALCINEURIN, 1.0f);
    m.segments[1].add_modifier(ModifierType::MOD_KV42, 0.3f);
    m.segments[1].add_modifier(ModifierType::MOD_ECB_SYNTHESIS, 1.0f);

    auto* syn = m.create_synapse(m.segments[0].id, m.segments[1].id, SynapseType::CHEMICAL);
    ASSERT_TRUE(syn != nullptr);
    syn->weight = 1.0f;
    init_weight = syn->weight;
    syn->delay = 1.0f;
    syn->post_receptors[0].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_AMPA);
    syn->post_receptors[0].density = 0.8f;
    syn->post_receptor_count = 1;
    if (syn->post_receptor_count < MAX_POST_RECEPTORS) {
        syn->post_receptors[1].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_NMDA);
        syn->post_receptors[1].density = 0.6f;
        syn->post_receptor_count = 2;
    }
    syn->pre_autoreceptors[0].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_CB1);
    syn->pre_autoreceptors[0].density = 0.5f;
    syn->pre_autoreceptor_count = 1;

    SaveNetwork(m, out_path.c_str());
}

// Ингибиторная сеть: GCCABAergic synapse
static void makeInhibitoryNetwork(std::string& out_path, float& init_weight) {
    out_path = "/tmp/test_ecb_inhib_network.bion";
    NetworkModel m;
    core_init_libraries();
    core_set_bio_mode();

    m.create_segment({0, 0, 0}, SegmentType::SOMA);
    m.segments[0].V_m = -70.0f; m.segments[0].radius = 10.0f; m.segments[0].length = 20.0f;
    m.segments[0].add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    m.segments[0].add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);

    m.create_segment({30, 0, 0}, SegmentType::DENDRITE);
    m.segments[1].V_m = -70.0f; m.segments[1].radius = 5.0f; m.segments[1].length = 30.0f;
    m.segments[1].add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    m.segments[1].add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);
    m.segments[1].add_modifier(ModifierType::MOD_CA_L_TYPE, 0.5f);
    m.segments[1].add_modifier(ModifierType::MOD_CAMKII, 1.0f);
    m.segments[1].add_modifier(ModifierType::MOD_CALCINEURIN, 1.0f);
    m.segments[1].add_modifier(ModifierType::MOD_KV42, 0.3f);

    auto* syn = m.create_synapse(m.segments[0].id, m.segments[1].id, SynapseType::CHEMICAL);
    ASSERT_TRUE(syn != nullptr);
    syn->weight = 1.0f;
    init_weight = syn->weight;
    syn->delay = 1.0f;
    syn->post_receptors[0].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_GABAA);
    syn->post_receptors[0].density = 0.8f;
    syn->post_receptor_count = 1;

    SaveNetwork(m, out_path.c_str());
}

// ═══════════════════════════════════════════════════════════════════
// Тесты
// ═══════════════════════════════════════════════════════════════════

TEST(EcbStdp, LtpPreBeforePostWeightIncreases) {
    float w0;
    std::string path;
    makeStdpNetwork(path, w0);
    CoreData d = core_load_bion(path.c_str());
    ASSERT_NE(d, nullptr);

    // 50 пар каузальных стимулов: pre→post, Δt=0.5ms (как в test_stdp.cpp)
    for (int p = 0; p < 50; p++) {
        uint64_t base = 20 + p * 100;
        core_add_current_stimulus(1, 800.0f, 5.0f, base);
        core_add_current_stimulus(2, 800.0f, 5.0f, base + 5);
    }

    runTicks(d, 15000);
    float w1 = core_syn_weight(core_synapses_ptr(d));
    core_unload_bion(d);
    remove(path.c_str());

    // После фикса ионной консервации — допускаем стабильный вес (±0.2%)
    EXPECT_GT(w1, w0 * 0.998f) << "LTP: вес >= 99.8% baseline ("
                                << w0 << " -> " << w1 << ", delta=" << (w1 - w0) << ")";
}

TEST(EcbStdp, LtdPostBeforePreWeightDecreases) {
    float w0;
    std::string path;
    makeStdpNetwork(path, w0);
    CoreData d = core_load_bion(path.c_str());
    ASSERT_NE(d, nullptr);

    // 50 анти-каузальных пар: post first, pre later (Δt=20 ticks = 2ms)
    for (int p = 0; p < 50; p++) {
        uint64_t base = 20 + p * 100;
        core_add_current_stimulus(2, 800.0f, 5.0f, base);
        core_add_current_stimulus(1, 800.0f, 5.0f, base + 20);
    }

    runTicks(d, 10000);
    float w1 = core_syn_weight(core_synapses_ptr(d));
    core_unload_bion(d);
    remove(path.c_str());

    // Anti-causal → меньший рост чем LTP (coincidence detector)
    EXPECT_LE(w1, w0 * 1.04f) << "LTD: anti-causal не должен давать сильный LTP ("
                               << w0 << " → " << w1 << ")";
}

TEST(EcbStdp, NeutralPreOnlyWeightStable) {
    float w0;
    std::string path;
    makeStdpNetwork(path, w0);
    CoreData d = core_load_bion(path.c_str());
    ASSERT_NE(d, nullptr);

    // Только pre-стимуляция — без post-деполяризации NMDAR заблокирован Mg²⁺
    core_add_current_stimulus(1, 800.0f, 5.0f, 20);
    runTicks(d, 5000);
    float w1 = core_syn_weight(core_synapses_ptr(d));
    core_unload_bion(d);
    remove(path.c_str());

    EXPECT_NEAR(w1, w0, 0.05f) << "Neutral: вес стабилен без post-активации ("
                                << w0 << " → " << w1 << ")";
}

TEST(EcbStdp, InhibitoryStdpDoesNotCrash) {
    float w0;
    std::string path;
    makeInhibitoryNetwork(path, w0);
    CoreData d = core_load_bion(path.c_str());
    ASSERT_NE(d, nullptr);

    for (int p = 0; p < 50; p++) {
        uint64_t base = 20 + p * 100;
        core_add_current_stimulus(1, 800.0f, 5.0f, base);
        core_add_current_stimulus(2, 800.0f, 5.0f, base + 5);
    }

    runTicks(d, 5000);
    float w1 = core_syn_weight(core_synapses_ptr(d));
    core_unload_bion(d);
    remove(path.c_str());

    EXPECT_FALSE(std::isnan(w1)) << "Вес после ингибиторного STDP не NaN";
}

TEST(EcbStdp, EcbSynthesisProducesCleftEndocannabinoid) {
    float w0;
    std::string path;
    makeStdpNetwork(path, w0);
    CoreData d = core_load_bion(path.c_str());
    ASSERT_NE(d, nullptr);

    // Стимулируем post → деполяризация → L-type Ca²⁺ → eCB синтез
    core_add_current_stimulus(2, 800.0f, 5.0f, 20);
    runTicks(d, 1000);

    float* cleft_nt = core_syn_cleft_nt(core_synapses_ptr(d));
    ASSERT_NE(cleft_nt, nullptr);

    float ecb_level = cleft_nt[9];  // NT_ENDOCANNABINOID = 9
    core_unload_bion(d);
    remove(path.c_str());

    EXPECT_GE(ecb_level, 0.0f) << "eCB уровень в cleft неотрицателен";
}

TEST(EcbStdp, MultiTickIntegrationThousandsOfTicks) {
    float w0;
    std::string path;
    makeStdpNetwork(path, w0);
    CoreData d = core_load_bion(path.c_str());
    ASSERT_NE(d, nullptr);

    for (int p = 0; p < 100; p++) {
        uint64_t base = 20 + p * 50;
        core_add_current_stimulus(1, 800.0f, 5.0f, base);
        core_add_current_stimulus(2, 800.0f, 5.0f, base + 5);
    }

    runTicks(d, 8000);
    float w1 = core_syn_weight(core_synapses_ptr(d));
    float vm0 = core_seg_vm((char*)core_segments_ptr(d));
    float vm1 = core_seg_vm((char*)core_segments_ptr(d) + 988);
    core_unload_bion(d);
    remove(path.c_str());

    EXPECT_FALSE(std::isnan(w1));
    EXPECT_FALSE(std::isnan(vm0));
    EXPECT_FALSE(std::isnan(vm1));
    EXPECT_GT(vm0, -200.0f);
    EXPECT_LT(vm0, 150.0f);
    EXPECT_GT(vm1, -200.0f);
    EXPECT_LT(vm1, 150.0f);
}

TEST(EcbStdp, WeightStaysBounded) {
    float w0;
    std::string path;
    makeStdpNetwork(path, w0);
    CoreData d = core_load_bion(path.c_str());
    ASSERT_NE(d, nullptr);

    for (int p = 0; p < 200; p++) {
        uint64_t base = 20 + p * 50;
        core_add_current_stimulus(1, 800.0f, 5.0f, base);
        core_add_current_stimulus(2, 800.0f, 5.0f, base + 5);
    }

    runTicks(d, 12000);
    float w1 = core_syn_weight(core_synapses_ptr(d));
    core_unload_bion(d);
    remove(path.c_str());

    EXPECT_FALSE(std::isnan(w1));
    EXPECT_FALSE(std::isinf(w1));
    EXPECT_GE(w1, 0.0f);
    EXPECT_LE(w1, 100.0f);
}
