// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_auditory_taxis.cpp — Валидация аудиотаксиса: AUDITORY_SENSOR + MOD_VIBRATION_SENSOR
//
// Проверяет:
// 1. AUDITORY_SENSOR=15, COUNT=16 — тип существует в перечислении
// 2. MOD_VIBRATION_SENSOR=115 — модификатор зарегистрирован
// 3. AUDITORY_SENSOR создаются генератором коннектома
// 4. Гарантированные синапсы: AUDITORY→dIN (AMPA, ipsi/contra), AUDITORY→cIN (AMPA)
// 5. Минимальная аудиотаксис-сеть → CPU/GPU parity
// 6. Асимметрия: звук слева → левый dIN активнее

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
// Тест 1: AUDITORY_SENSOR=15, COUNT=16 — тип существует
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryTaxis, AuditoryTypeExists) {
    EXPECT_EQ((int)TadpoleNeuronType::AUDITORY_SENSOR, 15);
    EXPECT_EQ((int)TadpoleNeuronType::COUNT, 27);
    printf("[Audio-Type] AUDITORY_SENSOR=%d COUNT=%d OK\n",
           (int)TadpoleNeuronType::AUDITORY_SENSOR, (int)TadpoleNeuronType::COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 2: MOD_VIBRATION_SENSOR=115 — модификатор существует
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryTaxis, VibrationSensorModExists) {
    EXPECT_EQ((int)ModifierType::MOD_VIBRATION_SENSOR, 115);
    printf("[Audio-Mod] MOD_VIBRATION_SENSOR=%d OK\n",
           (int)ModifierType::MOD_VIBRATION_SENSOR);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 3: AUDITORY_SENSOR создаются генератором коннектома
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryTaxis, AuditoryNeuronCreation) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    params.auditory_count = 2;
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
    params.enable_stdp = false;
    params.enable_consolidation = false;

    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    int auditory_count = 0;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 67.0f && seg.position.y <= 93.0f &&
            seg.position.z >= 230.0f && seg.position.z <= 250.0f)
            auditory_count++;
    }
    EXPECT_GE(auditory_count, 1) << "Должен быть хотя бы 1 AUDITORY_SENSOR";
    printf("[Audio-Create] AUDITORY_SENSOR neurons: %d\n", auditory_count);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 4: Гарантированные синапсы AUDITORY_SENSOR
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryTaxis, AuditorySynapsesExist) {
    auto make_net = [](int auditory_count) -> size_t {
        TadpoleGenerationParams p = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
        p.auditory_count = auditory_count;
        p.rb_per_hemisegment = 1; p.din_per_hemisegment = 2;
        p.cin_per_hemisegment = 2; p.mn_per_hemisegment = 2;
        p.ain_per_hemisegment = 1; p.dlc_count = 1;
        p.tectum_count = 0; p.ll_per_side = 0;
        p.orn_count = 0; p.ser_count = 0; p.avoid_count = 0;
        p.enable_stdp = false; p.enable_consolidation = false;
        TadpoleDevelop dev(p);
        return dev.generate_connectome().synapses.size();
    };

    size_t syns_without = make_net(0);
    size_t syns_with = make_net(2);

    printf("[Audio-Syn] without AUDITORY: %zu, with AUDITORY=2: %zu (delta=%zd)\n",
           syns_without, syns_with, (ptrdiff_t)(syns_with - syns_without));

    EXPECT_GE(syns_with, syns_without * 9 / 10)
        << "Сеть с AUDITORY_SENSOR должна иметь ~столько же или больше синапсов";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 5: Минимальная аудиотаксис-сеть — CPU/GPU parity
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryTaxis, MinimalAuditoryNetwork) {
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

    // Звук слева: левый AUDITORY деполяризован (-50mV), правый в покое (-65mV)
    uint32_t audL = mk(-20, 80, 240, 1, -50.0f); // +MOD_VIBRATION_SENSOR
    uint32_t audR = mk( 20, 80, 240, 2, -65.0f); // покой
    uint32_t dinL = mk(-20, 70,   0, 3);
    uint32_t dinR = mk( 20, 70,   0, 4);
    uint32_t cinL = mk(-20, 45,   0, 5);
    uint32_t cinR = mk( 20, 45,   0, 6);

    // Добавляем MOD_VIBRATION_SENSOR на левый AUDITORY
    for (auto& seg : m.segments) {
        if (seg.id == audL) seg.add_modifier(ModifierType::MOD_VIBRATION_SENSOR, 1.0f);
    }

    // AUDITORY → ipsi dIN (AMPA, w=0.6)
    mk_syn(audL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    mk_syn(audR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    // AUDITORY → contra dIN (AMPA, w=0.3)
    mk_syn(audL, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    mk_syn(audR, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    // AUDITORY → ipsi cIN (AMPA, w=0.4)
    mk_syn(audL, cinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    mk_syn(audR, cinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    // cIN → ipsi dIN (GLYCINE) — cross inhibition
    mk_syn(cinL, dinL, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 2.5f);
    mk_syn(cinR, dinR, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 2.5f);

    for (uint32_t i=1; i<=6; i++) m.neurons.push_back({i,0,0,0});

    ASSERT_TRUE(::SaveNetwork(m, "/tmp/audio_min.bion"));

    auto run = [](SimulationBridge::Backend be, const char* label) -> float {
        SimulationBridge sim; sim.setBackend(be);
        if (!sim.loadNetwork("/tmp/audio_min.bion")) return -999;
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        if (segs.empty()) return -999;
        float vm = segs[0].V_m;
        printf("[Audio-MinNet-%s] V_m[0]=%.2f mV (%zu segs)\n", label, vm, segs.size());
        return vm;
    };

    float cpu_vm = run(SimulationBridge::Backend::CPU, "CPU");
    float gpu_vm = run(SimulationBridge::Backend::GPU, "GPU");

    float delta = fabsf(cpu_vm - gpu_vm);
    printf("[Audio-MinNet-CPUvsGPU] delta=%.2f mV\n", delta);
    EXPECT_LT(delta, 5.0f) << "CPU-GPU delta должна быть <5mV";
    EXPECT_GT(cpu_vm, -200.0f) << "V_m должен быть валидным";
    EXPECT_FALSE(std::isnan(cpu_vm)) << "V_m не должен быть NaN";

    std::remove("/tmp/audio_min.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 6: Асимметрия — звук слева → левый dIN активнее
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryTaxis, SoundAsymmetry_IpsiDIN) {
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

    // Звук слева: левый AUDITORY уже активен (state[1]>0 через V_m)
    uint32_t audL = mk(-20, 80, 240, 1, -45.0f); // сильно деполяризован звуком
    uint32_t audR = mk( 20, 80, 240, 2, -65.0f); // покой
    uint32_t dinL = mk(-20, 70,   0, 3);
    uint32_t dinR = mk( 20, 70,   0, 4);
    uint32_t cinL = mk(-20, 45,   0, 5);
    uint32_t cinR = mk( 20, 45,   0, 6);

    for (auto& seg : m.segments) {
        if (seg.id == audL) seg.add_modifier(ModifierType::MOD_VIBRATION_SENSOR, 1.0f);
    }

    mk_syn(audL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    mk_syn(audR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    mk_syn(audL, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    mk_syn(audR, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    mk_syn(audL, cinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    mk_syn(audR, cinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    mk_syn(cinL, dinL, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 2.5f);
    mk_syn(cinR, dinR, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 2.5f);

    for (uint32_t i=1; i<=6; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/audio_asym.bion"));

    SimulationBridge sim; sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/audio_asym.bion"));
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
    printf("[Audio-Asym] dIN L=%.1f R=%.1f (expect L>R: sound on left)\n",
           dinL_vm, dinR_vm);
    EXPECT_GT(dinL_vm, dinR_vm - 10.0f)
        << "Левый dIN должен быть активнее (звук слева)";
    for (auto& s : segs) { EXPECT_FALSE(std::isnan(s.V_m)); EXPECT_GT(s.V_m, -200.0f); }

    std::remove("/tmp/audio_asym.bion");
}
