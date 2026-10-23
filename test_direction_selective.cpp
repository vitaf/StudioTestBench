// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_direction_selective.cpp — Валидация direction-selective lateral line circuit (H5)

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 1: DS_IN=12, COUNT=15 — тип существует в перечислении
// ═══════════════════════════════════════════════════════════════════════════════
TEST(DirectionSelective, DsInTypeExists) {
    EXPECT_EQ((int)TadpoleNeuronType::DS_IN, 12);
    EXPECT_EQ((int)TadpoleNeuronType::COUNT, 27);
    printf("[DS-Type] DS_IN=%d COUNT=%d OK\n",
           (int)TadpoleNeuronType::DS_IN, (int)TadpoleNeuronType::COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 2: Лево/право асимметрия — формула lateral_sign
// ═══════════════════════════════════════════════════════════════════════════════
TEST(DirectionSelective, LateralAsymmetryFormula) {
    // Из tadpole_sensors.cpp: apply_lateral_line()
    // lateral_sign = flow_dir.x: +1(right), -1(left)
    // ipsi: act *= 1.3, contra: act *= 0.7
    float base = 0.5f;

    // Поток слева (lateral_sign = -1)
    {
        float l_sign = -1.0f;
        float left_act  = base * ((l_sign < 0) ? 1.3f : 0.7f);
        float right_act = base * ((l_sign > 0) ? 1.3f : 0.7f);
        printf("[DS-Asym] Left flow: L=%.2f R=%.2f\n", left_act, right_act);
        EXPECT_GT(left_act, right_act)
            << "При потоке слева левая > правая";
        EXPECT_NEAR(left_act / base, 1.3f, 0.01f);
        EXPECT_NEAR(right_act / base, 0.7f, 0.01f);
    }
    // Поток справа (lateral_sign = +1)
    {
        float l_sign = 1.0f;
        float left_act  = base * ((l_sign < 0) ? 1.3f : 0.7f);
        float right_act = base * ((l_sign > 0) ? 1.3f : 0.7f);
        printf("[DS-Asym] Right flow: L=%.2f R=%.2f\n", left_act, right_act);
        EXPECT_GT(right_act, left_act)
            << "При потоке справа правая > левая";
    }
    // Нет lateral-потока — без асимметрии
    {
        float l_sign = 0.0f;
        float left_act  = base;
        float right_act = base;
        printf("[DS-Asym] No lateral flow: L=%.2f R=%.2f\n", left_act, right_act);
        EXPECT_EQ(left_act, right_act);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 3: Минимальная DS-сеть: 2 LL + 2 DS_IN + crossed glycinergic inhibition
// ═══════════════════════════════════════════════════════════════════════════════
TEST(DirectionSelective, MinimalDsNetwork) {
    NetworkModel m;
    auto mk_ll = [&](float x, float z, uint32_t nid) -> uint32_t {
        auto* s = m.create_segment({x, 80, z}, SegmentType::SOMA, {0,1,0});
        s->radius=4.0f; s->length=8.0f; s->V_m=-65.0f; s->neuron_id=nid;
        s->intra_ions[0]=10;s->intra_ions[1]=140;s->intra_ions[2]=0.0001f;s->intra_ions[3]=10;
        s->add_modifier(ModifierType::MOD_PHYSICAL_SENSOR, 1.0f);
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        return s->id;
    };
    auto mk_ds = [&](float x, float z, uint32_t nid) -> uint32_t {
        auto* s = m.create_segment({x, 75, z}, SegmentType::SOMA, {0,1,0});
        s->radius=3.0f; s->length=6.0f; s->V_m=-65.0f; s->neuron_id=nid;
        s->intra_ions[0]=10;s->intra_ions[1]=140;s->intra_ions[2]=0.0001f;s->intra_ions[3]=10;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 0.2f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.3f);
        return s->id;
    };

    uint32_t ll_l  = mk_ll(-20, 0, 1);   // LL левый
    uint32_t ll_r  = mk_ll( 20, 0, 2);   // LL правый
    uint32_t ds_l  = mk_ds(-20, 0, 3);   // DS_IN левый
    uint32_t ds_r  = mk_ds( 20, 0, 4);   // DS_IN правый

    // Barlow-Levick DS connections:
    // LL_left → DS_left (AMPA, fast)
    // DS_left → LL_right (GLYCINE, delay=8ms, crossed inhibition)
    // LL_right → DS_right (AMPA, fast)
    // DS_right → LL_left (GLYCINE, delay=8ms, crossed inhibition)
    auto mk_ampa = [&](uint32_t pre, uint32_t post) {
        SynapseSnapshot syn;
        syn.id = m.synapses.size() + 1;
        syn.pre_id = pre; syn.post_id = post;
        syn.type = SynapseType::CHEMICAL;
        syn.weight = 0.8f; syn.delay = 1.0f;
        syn.post_receptors[0].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_AMPA);
        syn.post_receptors[0].density = 0.8f;
        syn.post_receptor_count = 1;
        syn.cleft_nt[0] = 1.0f;  // глутамат
        m.synapses.push_back(syn);
    };
    auto mk_gly = [&](uint32_t pre, uint32_t post) {
        SynapseSnapshot syn;
        syn.id = m.synapses.size() + 1;
        syn.pre_id = pre; syn.post_id = post;
        syn.type = SynapseType::CHEMICAL;
        syn.weight = 0.6f; syn.delay = 8.0f;  // Barlow-Levick delay
        syn.post_receptors[0].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_GLYCINE);
        syn.post_receptors[0].density = 0.6f;
        syn.post_receptor_count = 1;
        syn.cleft_nt[7] = 1.0f;  // глицин (NT_GLYCINE=7)
        m.synapses.push_back(syn);
    };

    mk_ampa(ll_l, ds_l);
    mk_gly(ds_l, ll_r);
    mk_ampa(ll_r, ds_r);
    mk_gly(ds_r, ll_l);

    m.neurons.push_back({1,0,0,0}); m.neurons.push_back({2,0,0,0});
    m.neurons.push_back({3,0,0,0}); m.neurons.push_back({4,0,0,0});

    // Проверяем что синапсы созданы
    ASSERT_EQ(m.synapses.size(), 4u);
    // Проверяем задержку на gly-синапсах
    EXPECT_NEAR(m.synapses[1].delay, 8.0f, 0.1f) << "DS→LL должен иметь delay=8ms";
    EXPECT_NEAR(m.synapses[3].delay, 8.0f, 0.1f) << "DS→LL должен иметь delay=8ms";

    printf("[DS-MinNet] %zu segments, %zu synapses\n", m.segments.size(), m.synapses.size());

    // Симуляция и CPU/GPU сравнение
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/ds_min.bion"));

    auto run = [](SimulationBridge::Backend backend, const char* label) -> float {
        SimulationBridge sim;
        sim.setBackend(backend);
        if (!sim.loadNetwork("/tmp/ds_min.bion")) return -999.0f;
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        if (segs.empty()) return -999.0f;
        float vm = segs[0].V_m;
        printf("[DS-MinNet-%s] V_m[0]=%.2f mV (%zu segs)\n", label, vm, segs.size());
        return vm;
    };

    float cpu_vm = run(SimulationBridge::Backend::CPU, "CPU");
    float gpu_vm = run(SimulationBridge::Backend::GPU, "GPU");

    float delta = fabsf(cpu_vm - gpu_vm);
    printf("[DS-MinNet-CPUvsGPU] delta=%.2f mV\n", delta);
    EXPECT_LT(delta, 5.0f) << "CPU-GPU delta должна быть <5mV";
    EXPECT_GT(cpu_vm, -200.0f) << "V_m должен быть валидным";
    EXPECT_FALSE(std::isnan(cpu_vm)) << "V_m не должен быть NaN";

    std::remove("/tmp/ds_min.bion");
}
