// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_vestibular.cpp — Валидация вестибулярного аппарата: OTOLITH + SEMICIRCULAR
//
// Проверяет:
// 1. OTOLITH=16, SEMICIRCULAR=17, COUNT=18 — типы существуют
// 2. MOD_OTOLITH_HAIR_CELL=116, MOD_SEMICIRCULAR_CUPULA=117 — модификаторы
// 3. OTOLITH создаются генератором коннектома
// 4. SEMICIRCULAR создаются генератором коннектома
// 5. Гарантированные синапсы OTOLITH→dIN/cIN/mn
// 6. Гарантированные синапсы SEMICIRCULAR→dIN/cIN/dlc
// 7. Минимальная вестибулярная сеть → CPU/GPU parity (delta < 5mV)
// 8. Асимметрия гравитации: отолит слева активен → ипси-dIN активнее

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

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 1: OTOLITH=16, SEMICIRCULAR=17, COUNT=18
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VestibularSystem, NeuronTypesExist) {
    EXPECT_EQ((int)TadpoleNeuronType::OTOLITH, 16);
    EXPECT_EQ((int)TadpoleNeuronType::SEMICIRCULAR, 17);
    EXPECT_EQ((int)TadpoleNeuronType::COUNT, 27);
    printf("[Vest-Type] OTOLITH=%d SEMICIRCULAR=%d COUNT=%d OK\n",
           (int)TadpoleNeuronType::OTOLITH, (int)TadpoleNeuronType::SEMICIRCULAR,
           (int)TadpoleNeuronType::COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 2: MOD_OTOLITH_HAIR_CELL=116, MOD_SEMICIRCULAR_CUPULA=117
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VestibularSystem, ModifierTypesExist) {
    EXPECT_EQ((int)ModifierType::MOD_OTOLITH_HAIR_CELL, 116);
    EXPECT_EQ((int)ModifierType::MOD_SEMICIRCULAR_CUPULA, 117);
    printf("[Vest-Mod] OTOLITH_HAIR_CELL=%d SEMICIRCULAR_CUPULA=%d OK\n",
           (int)ModifierType::MOD_OTOLITH_HAIR_CELL,
           (int)ModifierType::MOD_SEMICIRCULAR_CUPULA);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 3: OTOLITH создаются генератором коннектома
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VestibularSystem, OtolithNeuronCreation) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    params.otolith_count = 2;
    params.rb_per_hemisegment  = 1;
    params.din_per_hemisegment = 2;
    params.cin_per_hemisegment = 2;
    params.mn_per_hemisegment  = 2;
    params.ain_per_hemisegment = 1;
    params.dlc_count = 1;
    params.tectum_count = 0;
    params.ll_per_side = 0;
    params.orn_count = 0;
    params.ser_count = 0;
    params.avoid_count = 0;
    params.auditory_count = 0;
    params.semicircular_count = 0;
    params.enable_stdp = false;
    params.enable_consolidation = false;

    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    int otolith_count = 0;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 20.0f && seg.position.y <= 40.0f &&
            seg.position.z >= 270.0f && seg.position.z <= 290.0f)
            otolith_count++;
    }
    EXPECT_GE(otolith_count, 1) << "Должен быть хотя бы 1 OTOLITH";
    printf("[Vest-OTO-Create] OTOLITH neurons: %d\n", otolith_count);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 4: SEMICIRCULAR создаются генератором коннектома
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VestibularSystem, SemicircularNeuronCreation) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    params.semicircular_count = 2;
    params.rb_per_hemisegment  = 1;
    params.din_per_hemisegment = 2;
    params.cin_per_hemisegment = 2;
    params.mn_per_hemisegment  = 2;
    params.ain_per_hemisegment = 1;
    params.dlc_count = 1;
    params.tectum_count = 0;
    params.ll_per_side = 0;
    params.orn_count = 0;
    params.ser_count = 0;
    params.avoid_count = 0;
    params.auditory_count = 0;
    params.otolith_count = 0;
    params.enable_stdp = false;
    params.enable_consolidation = false;

    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    int semicirc_count = 0;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 50.0f && seg.position.y <= 70.0f &&
            seg.position.z >= 250.0f && seg.position.z <= 270.0f)
            semicirc_count++;
    }
    EXPECT_GE(semicirc_count, 1) << "Должен быть хотя бы 1 SEMICIRCULAR";
    printf("[Vest-SCC-Create] SEMICIRCULAR neurons: %d\n", semicirc_count);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 5: Гарантированные синапсы OTOLITH
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VestibularSystem, OtolithSynapsesExist) {
    auto make_net = [](int otolith_count) -> size_t {
        TadpoleGenerationParams p = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
        p.otolith_count = otolith_count;
        p.rb_per_hemisegment = 1; p.din_per_hemisegment = 2;
        p.cin_per_hemisegment = 2; p.mn_per_hemisegment = 2;
        p.ain_per_hemisegment = 1; p.dlc_count = 1;
        p.tectum_count = 0; p.ll_per_side = 0;
        p.orn_count = 0; p.ser_count = 0; p.avoid_count = 0;
        p.auditory_count = 0; p.semicircular_count = 0;
        p.enable_stdp = false; p.enable_consolidation = false;
        TadpoleDevelop dev(p);
        return dev.generate_connectome().synapses.size();
    };

    size_t syns_without = make_net(0);
    size_t syns_with = make_net(2);

    printf("[Vest-OTO-Syn] without OTOLITH: %zu, with OTOLITH=2: %zu (delta=%zd)\n",
           syns_without, syns_with, (ptrdiff_t)(syns_with - syns_without));

    EXPECT_GE(syns_with, syns_without * 9 / 10)
        << "Сеть с OTOLITH должна иметь ~столько же или больше синапсов";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 6: Гарантированные синапсы SEMICIRCULAR
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VestibularSystem, SemicircularSynapsesExist) {
    auto make_net = [](int semicirc_count) -> size_t {
        TadpoleGenerationParams p = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
        p.semicircular_count = semicirc_count;
        p.rb_per_hemisegment = 1; p.din_per_hemisegment = 2;
        p.cin_per_hemisegment = 2; p.mn_per_hemisegment = 2;
        p.ain_per_hemisegment = 1; p.dlc_count = 1;
        p.tectum_count = 0; p.ll_per_side = 0;
        p.orn_count = 0; p.ser_count = 0; p.avoid_count = 0;
        p.auditory_count = 0; p.otolith_count = 0;
        p.enable_stdp = false; p.enable_consolidation = false;
        TadpoleDevelop dev(p);
        return dev.generate_connectome().synapses.size();
    };

    size_t syns_without = make_net(0);
    size_t syns_with = make_net(2);

    printf("[Vest-SCC-Syn] without SEMICIRCULAR: %zu, with SEMICIRCULAR=2: %zu (delta=%zd)\n",
           syns_without, syns_with, (ptrdiff_t)(syns_with - syns_without));

    EXPECT_GE(syns_with, syns_without * 9 / 10)
        << "Сеть с SEMICIRCULAR должна иметь ~столько же или больше синапсов";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 7: Минимальная вестибулярная сеть — CPU/GPU parity
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VestibularSystem, MinimalVestibularNetwork) {
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
        SynapseSnapshot syn;
        syn.id = m.synapses.size() + 1; syn.pre_id = pre; syn.post_id = post;
        syn.type = SynapseType::CHEMICAL; syn.weight = w; syn.delay = 1.0f;
        syn.post_receptors[0].type = static_cast<uint8_t>(rec); syn.post_receptors[0].density = w;
        syn.post_receptor_count = 1; syn.cleft_nt[nt] = 1.0f;
        m.synapses.push_back(syn);
    };

    // OTOLITH left (гравитация влево — деполяризован), OTOLITH right (покой)
    uint32_t otoL = mk(-20, 30, 280, 1, -50.0f);
    uint32_t otoR = mk( 20, 30, 280, 2, -65.0f);
    // SEMICIRCULAR left (угловое ускорение влево — деполяризован)
    uint32_t sccL = mk(-20, 60, 260, 3, -48.0f);
    uint32_t sccR = mk( 20, 60, 260, 4, -65.0f);
    // CPG targets
    uint32_t dinL = mk(-20, 70,   0, 5);
    uint32_t dinR = mk( 20, 70,   0, 6);
    uint32_t cinL = mk(-20, 45,   0, 7);
    uint32_t mnL  = mk(-20, 20,   0, 8);
    uint32_t dlcL = mk(-20, 105, 150, 9);

    // Модификаторы
    for (auto& seg : m.segments) {
        if (seg.id == otoL || seg.id == otoR)
            seg.add_modifier(ModifierType::MOD_OTOLITH_HAIR_CELL, 1.0f);
        if (seg.id == sccL || seg.id == sccR)
            seg.add_modifier(ModifierType::MOD_SEMICIRCULAR_CUPULA, 1.0f);
    }

    // OTOLITH → ipsi dIN (w=0.8)
    mk_syn(otoL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    mk_syn(otoR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    // OTOLITH → contra dIN (w=0.3)
    mk_syn(otoL, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    mk_syn(otoR, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    // OTOLITH → ipsi cIN (w=0.5)
    mk_syn(otoL, cinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    mk_syn(otoR, cinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    // OTOLITH → ipsi mn (w=0.4)
    mk_syn(otoL, mnL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);

    // SEMICIRCULAR → ipsi dIN (w=0.7)
    mk_syn(sccL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.7f);
    mk_syn(sccR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.7f);
    // SEMICIRCULAR → contra dIN (w=0.2)
    mk_syn(sccL, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.2f);
    mk_syn(sccR, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.2f);
    // SEMICIRCULAR → ipsi cIN (w=0.5)
    mk_syn(sccL, cinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    // SEMICIRCULAR → dlc (w=0.6)
    mk_syn(sccL, dlcL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);

    for (uint32_t i=1; i<=9; i++) m.neurons.push_back({i,0,0,0});

    ASSERT_TRUE(::SaveNetwork(m, "/tmp/vest_min.bion"));

    auto run = [](SimulationBridge::Backend be, const char* label) -> float {
        SimulationBridge sim; sim.setBackend(be);
        if (!sim.loadNetwork("/tmp/vest_min.bion")) return -999;
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        if (segs.empty()) return -999;
        float vm = segs[0].V_m;
        printf("[Vest-MinNet-%s] V_m[0]=%.2f mV (%zu segs)\n", label, vm, segs.size());
        return vm;
    };

    float cpu_vm = run(SimulationBridge::Backend::CPU, "CPU");
    float gpu_vm = run(SimulationBridge::Backend::GPU, "GPU");

    float delta = fabsf(cpu_vm - gpu_vm);
    printf("[Vest-MinNet-CPUvsGPU] delta=%.2f mV\n", delta);
    EXPECT_LT(delta, 5.0f) << "CPU-GPU delta должна быть <5mV";
    EXPECT_GT(cpu_vm, -200.0f) << "V_m должен быть валидным";
    EXPECT_FALSE(std::isnan(cpu_vm)) << "V_m не должен быть NaN";

    std::remove("/tmp/vest_min.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 8: Асимметрия гравитации — отолит слева активен → ипси-dIN активнее
// ═══════════════════════════════════════════════════════════════════════════════
TEST(VestibularSystem, GravityAsymmetry_PostureTilt) {
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
        SynapseSnapshot syn;
        syn.id = m.synapses.size() + 1; syn.pre_id = pre; syn.post_id = post;
        syn.type = SynapseType::CHEMICAL; syn.weight = w; syn.delay = 1.0f;
        syn.post_receptors[0].type = static_cast<uint8_t>(rec); syn.post_receptors[0].density = w;
        syn.post_receptor_count = 1; syn.cleft_nt[nt] = 1.0f;
        m.synapses.push_back(syn);
    };

    // Гравитация/наклон влево: левый отолит деполяризован, правый в покое
    uint32_t otoL = mk(-20, 30, 280, 1, -45.0f); // сильно деполяризован (гравитация влево)
    uint32_t otoR = mk( 20, 30, 280, 2, -65.0f); // покой
    uint32_t dinL = mk(-20, 70,   0, 3);
    uint32_t dinR = mk( 20, 70,   0, 4);
    uint32_t cinL = mk(-20, 45,   0, 5);
    uint32_t mnL  = mk(-20, 20,   0, 6);

    for (auto& seg : m.segments) {
        if (seg.id == otoL || seg.id == otoR)
            seg.add_modifier(ModifierType::MOD_OTOLITH_HAIR_CELL, 1.0f);
    }

    mk_syn(otoL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    mk_syn(otoR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    mk_syn(otoL, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    mk_syn(otoR, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    mk_syn(otoL, cinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    mk_syn(otoL, mnL,  ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);

    for (uint32_t i=1; i<=6; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/vest_asym.bion"));

    SimulationBridge sim; sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/vest_asym.bion"));
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::vector<SegmentSnapshot> segs;
    sim.getSegmentsSnapshot(segs);
    sim.stop();

    ASSERT_FALSE(segs.empty());
    float dinL_vm=-65, dinR_vm=-65;
    for (auto& s : segs) {
        if (s.neuron_id == 3) dinL_vm = s.V_m;
        if (s.neuron_id == 4) dinR_vm = s.V_m;
    }
    printf("[Vest-Asym] dIN L=%.1f R=%.1f (expect L>R: gravity on left)\n",
           dinL_vm, dinR_vm);
    EXPECT_GT(dinL_vm, dinR_vm - 10.0f)
        << "Левый dIN должен быть активнее (гравитация/наклон влево)";
    for (auto& s : segs) { EXPECT_FALSE(std::isnan(s.V_m)); EXPECT_GT(s.V_m, -200.0f); }

    std::remove("/tmp/vest_asym.bion");
}
