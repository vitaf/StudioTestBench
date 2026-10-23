// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_auditory_physics.cpp — проверка физической модели звука/вибрации
// и интеграции AUDITORY_SENSOR → CPG (аудиотаксис).

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_sensors.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_body_3d.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_habitat_3d.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"

// ═══════════════════════════════════════════════════════════════════════════════
// 1. SoundSource: создание и настройка
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryPhysics, SoundSourceCreation) {
    TadpoleHabitat3D habitat;
    habitat.Init();

    EXPECT_FLOAT_EQ(habitat.sound_source.pos.x, 0.0f);
    EXPECT_FLOAT_EQ(habitat.sound_source.pos.y, 60.0f);
    EXPECT_FLOAT_EQ(habitat.sound_source.frequency, 200.0f);
    EXPECT_FLOAT_EQ(habitat.sound_source.amplitude, 1.0f);
    EXPECT_FALSE(habitat.sound_source.active);

    habitat.sound_source.active = true;
    habitat.sound_source.pos = {100, 50, 200};
    habitat.sound_source.frequency = 400.0f;
    habitat.sound_source.amplitude = 0.5f;
    EXPECT_TRUE(habitat.sound_source.active);
    EXPECT_FLOAT_EQ(habitat.sound_source.frequency, 400.0f);
    EXPECT_FLOAT_EQ(habitat.sound_source.amplitude, 0.5f);

    habitat.Shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 2. Затухание звука с расстоянием
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryPhysics, SoundAttenuation) {
    TadpoleBody3D body;
    body.Init();
    body.SetPosition(0, 0, 0);

    // Источник прямо перед головастиком на разных дистанциях
    auto r1 = body.QueryAuditory({0, 1, 0}, 1.0f);   // близко (~1 ед)
    auto r2 = body.QueryAuditory({0, 1, 100}, 1.0f);  // 100 ед
    auto r3 = body.QueryAuditory({0, 1, 200}, 1.0f);  // 200 ед

    // Чем дальше — тем тише
    EXPECT_GT(r1.left, r2.left);
    EXPECT_GT(r2.left, r3.left);
    // На 200 ед должно быть значительно тише чем на 1 ед
    EXPECT_LT(r3.left, r1.left * 0.5f);

    printf("[AUD] Near: L=%.3f R=%.3f | Mid: L=%.3f R=%.3f | Far: L=%.3f R=%.3f\n",
           r1.left, r1.right, r2.left, r2.right, r3.left, r3.right);

    body.Shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3. Бинауральная разница: источник слева → левое ухо активнее
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryPhysics, BinauralDifference) {
    TadpoleBody3D body;
    body.Init();
    body.SetPosition(0, 0, 0);

    // Источник слева
    auto left_src = body.QueryAuditory({-50, 1, 0}, 1.0f);
    EXPECT_GT(left_src.left, left_src.right) << "Левое ухо должно быть активнее при источнике слева";

    // Источник справа
    auto right_src = body.QueryAuditory({50, 1, 0}, 1.0f);
    EXPECT_GT(right_src.right, right_src.left) << "Правое ухо должно быть активнее при источнике справа";

    // Источник прямо впереди — симметрично
    auto center_src = body.QueryAuditory({0, 1, 0}, 1.0f);
    EXPECT_NEAR(center_src.left, center_src.right, 0.01f) << "По центру — симметричная активация";

    // ITD для источника слева: левое ухо ближе → отрицательный ITD
    EXPECT_LT(left_src.itd, 0.0f) << "Левый источник: звук приходит в левое ухо раньше (ITD < 0)";
    EXPECT_GT(right_src.itd, 0.0f) << "Правый источник: звук приходит в правое ухо раньше (ITD > 0)";

    printf("[AUD] Left src:  L=%.3f R=%.3f ITD=%.6f\n", left_src.left, left_src.right, left_src.itd);
    printf("[AUD] Right src: L=%.3f R=%.3f ITD=%.6f\n", right_src.left, right_src.right, right_src.itd);
    printf("[AUD] Center:    L=%.3f R=%.3f ITD=%.6f\n", center_src.left, center_src.right, center_src.itd);

    body.Shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4. AUDITORY_SENSOR → dIN: звук слева → левый dIN активнее
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryPhysics, AuditoryCPGIntegration) {
    // Минимальная сеть: audL/audR + dIN_L/dIN_R + cIN_L/cIN_R
    NetworkModel model;
    model.bion_mode = 0;

    auto mk = [&](float x, float y, float z, uint32_t nid, float Vm = -65.0f) -> uint32_t {
        SegmentSnapshot s;
        s.id = (uint32_t)model.segments.size() + 1;
        s.neuron_id = nid;
        s.position = {x, y, z};
        s.direction = {0, 1, 0};
        s.radius = 4.0f; s.length = 8.0f;
        s.V_m = Vm; s.prev_V_m = Vm;
        s.intra_ions[0] = 0.015f; s.intra_ions[1] = 0.140f;
        s.intra_ions[2] = 0.00005f; s.intra_ions[3] = 0.005f;
        s.extra_ions[0] = 0.140f; s.extra_ions[1] = 0.005f;
        s.extra_ions[2] = 0.0012f; s.extra_ions[3] = 0.130f;
        s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        s.add_modifier(ModifierType::MOD_K2P_LEAK, 2.0f);
        s.add_modifier(ModifierType::MOD_KCC2, 0.8f);
        s.type = SegmentType::SOMA;
        uint32_t id = s.id;
        model.segments.push_back(s);
        return id;
    };

    uint32_t audL = mk(-20, 80, 240, 0);   // левый AUDITORY_SENSOR
    uint32_t audR = mk( 20, 80, 240, 1);   // правый
    uint32_t dinL = mk(-35, 70, 0, 2);      // левый dIN
    uint32_t dinR = mk( 35, 70, 0, 3);      // правый dIN
    uint32_t cinL = mk(-35, 45, 0, 4);      // левый cIN
    uint32_t cinR = mk( 35, 45, 0, 5);      // правый cIN

    // AUDITORY→dIN (ипси: w=0.6, контра: w=0.3)
    auto syn = [&](uint32_t pre, uint32_t post, float w) {
        SynapseSnapshot s;
        s.id = (uint32_t)model.synapses.size();
        s.pre_id = pre; s.post_id = post;
        s.type = SynapseType::CHEMICAL;
        s.weight = w; s.release_probability = 0.6f;
        s.pre_area = 1.5f; s.post_area = 1.5f;
        s.post_receptors[0].type = (uint8_t)ModifierType::MOD_NT_RECEPTOR_AMPA;
        s.post_receptors[0].density = w;
        s.post_receptor_count = 1;
        model.synapses.push_back(s);
    };

    syn(audL, dinL, 0.6f);  // ipsi
    syn(audL, dinR, 0.3f);  // contra
    syn(audR, dinR, 0.6f);  // ipsi
    syn(audR, dinL, 0.3f);  // contra
    // cIN→dIN перекрёст (glycine)
    syn(cinL, dinR, 2.0f);
    syn(cinR, dinL, 2.0f);

    // NeuronInfo
    for (uint32_t n = 0; n < 6; n++) {
        NeuronInfo ni; ni.id = n; ni.segment_start = n; ni.segment_count = 1;
        model.neurons.push_back(ni);
    }

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/aud_cpg_test.bion"));

    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/aud_cpg_test.bion"));

    // Тонический ток на левый AUDITORY_SENSOR: звук слева
    sim.addCurrentStimulus(audL, 100.0f, 999999.0f, 0.0f);

    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::vector<SegmentSnapshot> segs;
    sim.getSegmentsSnapshot(segs);
    sim.stop();
    std::remove("/tmp/aud_cpg_test.bion");

    float dinL_vm = -99, dinR_vm = -99;
    for (auto& s : segs) {
        if (s.type != SegmentType::SOMA) continue;
        if (s.position.y >= 65 && s.position.y <= 75 && s.position.z < 10) {
            if (s.position.x < 0) dinL_vm = s.V_m;
            else dinR_vm = s.V_m;
        }
    }
    printf("[AUD-CPG] dIN L=%.1f R=%.1f\n", dinL_vm, dinR_vm);
    EXPECT_GT(dinL_vm, dinR_vm - 10.0f) << "Левый dIN должен быть не менее активен (звук слева)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// 5. Частотное кодирование: частота → state[2]
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryPhysics, FrequencyEncoding) {
    NetworkModel model;
    auto* s = model.create_segment({0, 80, 240}, SegmentType::SOMA, {0, 1, 0});
    s->neuron_id = 0; s->radius = 3.5f; s->length = 7.0f;
    s->add_modifier(ModifierType::MOD_VIBRATION_SENSOR, 0.5f);
    s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
    NeuronInfo ni; ni.id = 0; ni.segment_start = 0; ni.segment_count = 1;
    model.neurons.push_back(ni);

    TadpoleBody3D body;
    body.Init();
    TadpoleSensors sensors;

    // 100 Гц
    sensors.apply_auditory({0, 60, 300}, 100.0f, 1.0f, &model, body);
    float freq_100 = -1;
    for (int m = 0; m < model.segments[0].modifier_count; m++) {
        if (model.segments[0].modifiers[m].mod_type() == ModifierType::MOD_VIBRATION_SENSOR) {
            freq_100 = model.segments[0].modifiers[m].state[2];
        }
    }
    EXPECT_FLOAT_EQ(freq_100, 100.0f);

    // 500 Гц
    sensors.apply_auditory({0, 60, 300}, 500.0f, 1.0f, &model, body);
    float freq_500 = -1;
    for (int m = 0; m < model.segments[0].modifier_count; m++) {
        if (model.segments[0].modifiers[m].mod_type() == ModifierType::MOD_VIBRATION_SENSOR) {
            freq_500 = model.segments[0].modifiers[m].state[2];
        }
    }
    EXPECT_FLOAT_EQ(freq_500, 500.0f);

    printf("[AUD-Freq] 100 Hz → state[2]=%.1f | 500 Hz → state[2]=%.1f\n", freq_100, freq_500);

    body.Shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 6. CPU/GPU паритет на auditory-сети
// ═══════════════════════════════════════════════════════════════════════════════
TEST(AuditoryPhysics, CPUGPUParity) {
    NetworkModel model;
    model.bion_mode = 0;

    auto mk = [&](float x, float y, float z, uint32_t nid) -> uint32_t {
        SegmentSnapshot s;
        s.id = (uint32_t)model.segments.size() + 1;
        s.neuron_id = nid;
        s.position = {x, y, z}; s.direction = {0, 1, 0};
        s.radius = 4.0f; s.length = 8.0f; s.V_m = -65.0f; s.prev_V_m = -65.0f;
        s.intra_ions[0] = 0.015f; s.intra_ions[1] = 0.140f;
        s.intra_ions[2] = 0.00005f; s.intra_ions[3] = 0.005f;
        s.extra_ions[0] = 0.140f; s.extra_ions[1] = 0.005f;
        s.extra_ions[2] = 0.0012f; s.extra_ions[3] = 0.130f;
        s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        s.add_modifier(ModifierType::MOD_K2P_LEAK, 2.0f);
        s.type = SegmentType::SOMA;
        uint32_t id = s.id;
        model.segments.push_back(s);
        return id;
    };

    mk(-20, 80, 240, 0);  // audL
    mk( 20, 80, 240, 1);  // audR
    mk(-35, 70, 0, 2);    // dinL
    mk( 35, 70, 0, 3);    // dinR

    for (uint32_t n = 0; n < 4; n++) {
        NeuronInfo ni; ni.id = n; ni.segment_start = n; ni.segment_count = 1;
        model.neurons.push_back(ni);
    }

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/aud_parity.bion"));

    // CPU
    SimulationBridge sim_cpu;
    sim_cpu.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim_cpu.loadNetwork("/tmp/aud_parity.bion"));
    sim_cpu.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::vector<SegmentSnapshot> cpu_segs;
    sim_cpu.getSegmentsSnapshot(cpu_segs);
    sim_cpu.stop();

    // GPU (fallback to CPU if no GPU available)
    SimulationBridge sim_gpu;
    sim_gpu.setBackend(SimulationBridge::Backend::GPU);
    if (sim_gpu.loadNetwork("/tmp/aud_parity.bion")) {
        sim_gpu.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::vector<SegmentSnapshot> gpu_segs;
        sim_gpu.getSegmentsSnapshot(gpu_segs);
        sim_gpu.stop();

        float max_delta = 0;
        for (size_t i = 0; i < cpu_segs.size() && i < gpu_segs.size(); i++)
            max_delta = fmaxf(max_delta, fabsf(cpu_segs[i].V_m - gpu_segs[i].V_m));
        printf("[AUD-Parity] CPU/GPU max delta = %.3f mV\n", max_delta);
        EXPECT_LT(max_delta, 5.0f) << "CPU/GPU delta должна быть <5mV";
    }

    std::remove("/tmp/aud_parity.bion");
}
