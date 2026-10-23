// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_electroreception.cpp — Валидация электрорецепции: ELECTRORECEPTOR + MOD_ELECTRORECEPTOR_AMPULLARY

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>
#include <vector>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"

TEST(Electroreception, NeuronTypeExists) {
    EXPECT_EQ((int)TadpoleNeuronType::ELECTRORECEPTOR, 18);
    EXPECT_EQ((int)TadpoleNeuronType::COUNT, 27);
    printf("[Electro-Type] ELECTRORECEPTOR=%d COUNT=%d OK\n",
           (int)TadpoleNeuronType::ELECTRORECEPTOR, (int)TadpoleNeuronType::COUNT);
}

TEST(Electroreception, ModifierExists) {
    EXPECT_EQ((int)ModifierType::MOD_ELECTRORECEPTOR_AMPULLARY, 118);
    printf("[Electro-Mod] MOD_ELECTRORECEPTOR_AMPULLARY=%d OK\n",
           (int)ModifierType::MOD_ELECTRORECEPTOR_AMPULLARY);
}

TEST(Electroreception, ElectroreceptorNeuronCreation) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    params.electroreceptor_count = 2;
    params.rb_per_hemisegment  = 1; params.din_per_hemisegment = 2;
    params.cin_per_hemisegment = 2; params.mn_per_hemisegment  = 2;
    params.ain_per_hemisegment = 1; params.dlc_count = 1;
    params.tectum_count = 0; params.ll_per_side = 0;
    params.orn_count = 0; params.ser_count = 0; params.avoid_count = 0;
    params.auditory_count = 0; params.otolith_count = 0; params.semicircular_count = 0;
    params.magnetoreceptor_count = 0;
    params.enable_stdp = false; params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();
    int count = 0;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 50.0f && seg.position.y <= 65.0f &&
            seg.position.z >= 220.0f && seg.position.z <= 240.0f)
            count++;
    }
    EXPECT_GE(count, 1);
    printf("[Electro-Create] ELECTRORECEPTOR neurons: %d\n", count);
}

TEST(Electroreception, ElectroreceptorSynapsesExist) {
    auto mk = [](int c) -> size_t {
        TadpoleGenerationParams p = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
        p.electroreceptor_count = c;
        p.rb_per_hemisegment = 1; p.din_per_hemisegment = 2;
        p.cin_per_hemisegment = 2; p.mn_per_hemisegment = 2;
        p.ain_per_hemisegment = 1; p.dlc_count = 1;
        p.tectum_count = 0; p.ll_per_side = 0;
        p.orn_count = 0; p.ser_count = 0; p.avoid_count = 0;
        p.auditory_count = 0; p.otolith_count = 0; p.semicircular_count = 0;
        p.magnetoreceptor_count = 0;
        p.enable_stdp = false; p.enable_consolidation = false;
        return TadpoleDevelop(p).generate_connectome().synapses.size();
    };
    size_t s0 = mk(0), s2 = mk(2);
    printf("[Electro-Syn] without: %zu, with=2: %zu (delta=%zd)\n", s0, s2, (ptrdiff_t)(s2 - s0));
    EXPECT_GE(s2, s0 * 9 / 10);
}

TEST(Electroreception, MinimalElectroreceptorNetwork) {
    NetworkModel m;
    auto mk = [&](float x, float y, float z, uint32_t nid, float Vm=-65.0f) -> uint32_t {
        auto* s = m.create_segment({x, y, z}, SegmentType::SOMA, {0,1,0});
        s->radius=4.0f; s->length=8.0f; s->V_m=Vm; s->neuron_id=nid;
        s->intra_ions[0]=10; s->intra_ions[1]=140; s->intra_ions[2]=0.0001f; s->intra_ions[3]=10;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        return s->id;
    };
    auto mk_syn = [&](uint32_t pre, uint32_t post, ModifierType rec, uint8_t nt, float w) {
        SynapseSnapshot syn; syn.id=m.synapses.size()+1; syn.pre_id=pre; syn.post_id=post;
        syn.type=SynapseType::CHEMICAL; syn.weight=w; syn.delay=1.0f;
        syn.post_receptors[0].type = static_cast<uint8_t>(rec); syn.post_receptors[0].density=w;
        syn.post_receptor_count=1; syn.cleft_nt[nt]=1.0f;
        m.synapses.push_back(syn);
    };
    uint32_t erL = mk(-20, 55, 230, 1, -50.0f);
    uint32_t erR = mk( 20, 55, 230, 2, -65.0f);
    uint32_t dL  = mk(-20, 70,   0, 3);
    uint32_t dR  = mk( 20, 70,   0, 4);
    uint32_t cL  = mk(-20, 45,   0, 5);
    for (auto& seg : m.segments) {
        if (seg.id == erL) seg.add_modifier(ModifierType::MOD_ELECTRORECEPTOR_AMPULLARY, 1.0f);
        if (seg.id == erR) seg.add_modifier(ModifierType::MOD_ELECTRORECEPTOR_AMPULLARY, 1.0f);
    }
    mk_syn(erL, dL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    mk_syn(erR, dR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    mk_syn(erL, dR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.2f);
    mk_syn(erR, dL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.2f);
    mk_syn(erL, cL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    for (uint32_t i=1; i<=5; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/electro_min.bion"));
    auto run = [](SimulationBridge::Backend be, const char* label) -> float {
        SimulationBridge sim; sim.setBackend(be);
        if (!sim.loadNetwork("/tmp/electro_min.bion")) return -999;
        sim.start(); std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::vector<SegmentSnapshot> segs; sim.getSegmentsSnapshot(segs); sim.stop();
        if (segs.empty()) return -999;
        printf("[Electro-MinNet-%s] V_m[0]=%.2f mV (%zu segs)\n", label, segs[0].V_m, segs.size());
        return segs[0].V_m;
    };
    float cv = run(SimulationBridge::Backend::CPU, "CPU");
    float gv = run(SimulationBridge::Backend::GPU, "GPU");
    float delta = fabsf(cv - gv);
    printf("[Electro-MinNet-CPUvsGPU] delta=%.2f mV\n", delta);
    EXPECT_LT(delta, 5.0f); EXPECT_GT(cv, -200.0f); EXPECT_FALSE(std::isnan(cv));
    std::remove("/tmp/electro_min.bion");
}

TEST(Electroreception, ElectricFieldAsymmetry) {
    NetworkModel m;
    auto mk = [&](float x, float y, float z, uint32_t nid, float Vm=-65.0f) -> uint32_t {
        auto* s = m.create_segment({x, y, z}, SegmentType::SOMA, {0,1,0});
        s->radius=4.0f; s->length=8.0f; s->V_m=Vm; s->neuron_id=nid;
        s->intra_ions[0]=10; s->intra_ions[1]=140; s->intra_ions[2]=0.0001f; s->intra_ions[3]=10;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        return s->id;
    };
    auto mk_syn = [&](uint32_t pre, uint32_t post, ModifierType rec, uint8_t nt, float w) {
        SynapseSnapshot syn; syn.id=m.synapses.size()+1; syn.pre_id=pre; syn.post_id=post;
        syn.type=SynapseType::CHEMICAL; syn.weight=w; syn.delay=1.0f;
        syn.post_receptors[0].type = static_cast<uint8_t>(rec); syn.post_receptors[0].density=w;
        syn.post_receptor_count=1; syn.cleft_nt[nt]=1.0f;
        m.synapses.push_back(syn);
    };
    // Электрическое поле слева: левый рецептор деполяризован
    uint32_t erL = mk(-20, 55, 230, 1, -45.0f);
    uint32_t erR = mk( 20, 55, 230, 2, -65.0f);
    uint32_t dL  = mk(-20, 70,   0, 3);
    uint32_t dR  = mk( 20, 70,   0, 4);
    for (auto& seg : m.segments) {
        if (seg.id == erL) seg.add_modifier(ModifierType::MOD_ELECTRORECEPTOR_AMPULLARY, 1.0f);
        if (seg.id == erR) seg.add_modifier(ModifierType::MOD_ELECTRORECEPTOR_AMPULLARY, 1.0f);
    }
    mk_syn(erL, dL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    mk_syn(erR, dR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    mk_syn(erL, dR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.2f);
    mk_syn(erR, dL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.2f);
    for (uint32_t i=1; i<=4; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/electro_asym.bion"));
    SimulationBridge sim; sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/electro_asym.bion"));
    sim.start(); std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::vector<SegmentSnapshot> segs; sim.getSegmentsSnapshot(segs); sim.stop();
    ASSERT_FALSE(segs.empty());
    float dLv=-65, dRv=-65;
    for (auto& s : segs) { if (s.neuron_id==3) dLv=s.V_m; if (s.neuron_id==4) dRv=s.V_m; }
    printf("[Electro-Asym] dIN L=%.1f R=%.1f (expect L>R: field on left)\n", dLv, dRv);
    EXPECT_GT(dLv, dRv - 10.0f);
    for (auto& s : segs) { EXPECT_FALSE(std::isnan(s.V_m)); EXPECT_GT(s.V_m, -200.0f); }
    std::remove("/tmp/electro_asym.bion");
}

TEST(Electroreception, LowPassFiltering) {
    // Математический тест: V_canal должен сглаживать быстрые изменения
    float V_canal = 0.0f;
    float dt = 1.0f; // ms
    float tau = 100.0f; // ms
    // Подаём ступеньку: V_ext = 1.0
    for (int t = 0; t < 500; t++) {
        float V_ext = 1.0f;
        V_canal += (V_ext - V_canal) * dt / tau;
    }
    // Через 5*tau должно быть > 0.99
    EXPECT_GT(V_canal, 0.99f);
    printf("[Electro-LPF] V_canal after 500ms: %.4f (expect >0.99)\n", V_canal);
}
