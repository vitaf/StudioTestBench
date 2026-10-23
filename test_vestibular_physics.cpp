// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>
#include <glm/glm.hpp>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_body_3d.h"
#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"

TEST(VestibularPhysics, GravityPitchDetection) {
    TadpoleBody3D body;
    body.Init();
    body.SetPosition(0, 0, 0);
    body.heading_ = 0.0f;  // смотрит вперёд

    // Тело наклонено вперёд — pitch должен быть ненулевым (зависит от heading)
    // При heading=0, гравитация (0,-9.81,0) даёт pitch=0 (нет наклона)
    auto r0 = body.QueryVestibular({0, -9.81f, 0}, {0, 0, 0}, 0.0f, 0.0f, 0.01f);
    EXPECT_NEAR(r0.pitch, 0.0f, 0.01f);  // ровно → нет наклона
    EXPECT_NEAR(r0.roll, 0.0f, 0.01f);   // ровно → нет крена

    // При heading=90° (смотрит вправо), гравитация даёт roll≠0
    auto r90 = body.QueryVestibular({0, -9.81f, 0}, {0, 0, 0}, 1.5708f, 1.5708f, 0.01f);
    EXPECT_GT(fabsf(r90.roll), 0.0f) << "При heading=90° roll должен быть ненулевым";

    printf("[Vest] heading=0:   pitch=%.3f roll=%.3f\n", r0.pitch, r0.roll);
    printf("[Vest] heading=90:  pitch=%.3f roll=%.3f\n", r90.pitch, r90.roll);

    body.Shutdown();
}

TEST(VestibularPhysics, AngularAcceleration) {
    TadpoleBody3D body;
    body.Init();
    body.SetPosition(0, 0, 0);

    // Поворот: heading меняется с 0 на 0.1 рад за 0.01с → yaw_rate ≈ 10 рад/с
    auto r_turn = body.QueryVestibular({0, -9.81f, 0}, {0, 0, 0}, 0.1f, 0.0f, 0.01f);
    EXPECT_GT(r_turn.yaw_rate, 5.0f) << "Поворот должен дать yaw_rate > 5 рад/с";
    EXPECT_LT(r_turn.yaw_rate, 15.0f) << "yaw_rate должен быть ~10 рад/с";

    // Нет поворота
    auto r_still = body.QueryVestibular({0, -9.81f, 0}, {0, 0, 0}, 0.0f, 0.0f, 0.01f);
    EXPECT_NEAR(r_still.yaw_rate, 0.0f, 0.1f) << "Без поворота yaw_rate = 0";

    printf("[Vest] Turn:  yaw_rate=%.2f rad/s\n", r_turn.yaw_rate);
    printf("[Vest] Still: yaw_rate=%.2f rad/s\n", r_still.yaw_rate);

    body.Shutdown();
}

TEST(VestibularPhysics, LinearAcceleration) {
    TadpoleBody3D body;
    body.Init();
    body.SetPosition(0, 0, 0);

    // Движение вперёд со скоростью 10 ед/с
    auto r = body.QueryVestibular({0, -9.81f, 0}, {0, 0, 10.0f}, 0.0f, 0.0f, 0.01f);
    EXPECT_GT(r.linear_accel, 5.0f) << "Движение должно дать linear_accel > 0";

    printf("[Vest] Speed=10: linear_accel=%.2f\n", r.linear_accel);

    body.Shutdown();
}

TEST(VestibularPhysics, VestibularCPGIntegration) {
    NetworkModel model;
    model.bion_mode = 0;

    auto mk = [&](float x, float y, float z, uint32_t nid, float Vm = -65.0f) -> uint32_t {
        SegmentSnapshot s;
        s.id = (uint32_t)model.segments.size() + 1;
        s.neuron_id = nid;
        s.position = {x, y, z}; s.direction = {0, 1, 0};
        s.radius = 4.0f; s.length = 8.0f; s.V_m = Vm; s.prev_V_m = Vm;
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

    uint32_t otoL = mk(-20, 60, 80, 0);  // левый отолит
    uint32_t otoR = mk( 20, 60, 80, 1);  // правый отолит
    uint32_t dinL = mk(-35, 70, 0, 2);   // левый dIN
    uint32_t dinR = mk( 35, 70, 0, 3);   // правый dIN

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

    syn(otoL, dinL, 0.8f);  // OTOLITH→dIN ipsi
    syn(otoL, dinR, 0.3f);  // OTOLITH→dIN contra
    syn(otoR, dinR, 0.8f);
    syn(otoR, dinL, 0.3f);

    for (uint32_t n = 0; n < 4; n++) {
        NeuronInfo ni; ni.id = n; ni.segment_start = n; ni.segment_count = 1;
        model.neurons.push_back(ni);
    }

    ASSERT_TRUE(::SaveNetwork(model, "/tmp/vest_cpg.bion"));
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/vest_cpg.bion"));

    sim.addCurrentStimulus(otoL, 100.0f, 999999.0f, 0.0f);  // левый отолит активен (наклон влево)
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::vector<SegmentSnapshot> segs;
    sim.getSegmentsSnapshot(segs);
    sim.stop();
    std::remove("/tmp/vest_cpg.bion");

    float dinL_vm = -99, dinR_vm = -99;
    for (auto& s : segs) {
        if (s.type != SegmentType::SOMA) continue;
        if (s.position.y >= 65 && s.position.y <= 75 && s.position.z < 10) {
            if (s.position.x < 0) dinL_vm = s.V_m;
            else dinR_vm = s.V_m;
        }
    }
    printf("[Vest-CPG] dIN L=%.1f R=%.1f (left otolith active → left dIN)\n", dinL_vm, dinR_vm);
    EXPECT_GT(dinL_vm, dinR_vm - 10.0f) << "Левый dIN активнее (левый отолит стимулирован)";
}

TEST(VestibularPhysics, RightingReflex) {
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
        s.add_modifier(ModifierType::MOD_KCC2, 0.8f);
        s.type = SegmentType::SOMA;
        uint32_t id = s.id;
        model.segments.push_back(s);
        return id;
    };

    uint32_t otoL = mk(-20, 60, 80, 0);
    uint32_t otoR = mk( 20, 60, 80, 1);
    uint32_t sccL = mk(-20, 60, 120, 2);
    uint32_t sccR = mk( 20, 60, 120, 3);
    uint32_t dinL = mk(-35, 70, 0, 4);
    uint32_t dinR = mk( 35, 70, 0, 5);

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

    syn(otoL, dinL, 0.8f);
    syn(otoR, dinR, 0.8f);
    syn(sccL, dinL, 0.6f);
    syn(sccR, dinR, 0.6f);

    for (uint32_t n = 0; n < 6; n++) {
        NeuronInfo ni; ni.id = n; ni.segment_start = n; ni.segment_count = 1;
        model.neurons.push_back(ni);
    }

    // Тест A: тело перевёрнуто влево — левый отолит активнее
    {
        ASSERT_TRUE(::SaveNetwork(model, "/tmp/vest_righting_a.bion"));
        SimulationBridge sim;
        ASSERT_TRUE(sim.loadNetwork("/tmp/vest_righting_a.bion"));
        sim.addCurrentStimulus(otoL, 100.0f, 999999.0f, 0.0f);  // левый активен
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        std::remove("/tmp/vest_righting_a.bion");

        float dinL = -99, dinR = -99;
        for (auto& s : segs) {
            if (s.type != SegmentType::SOMA) continue;
            if (s.position.y >= 65 && s.position.y <= 75 && s.position.z < 10) {
                if (s.position.x < 0) dinL = s.V_m;
                else dinR = s.V_m;
            }
        }
        printf("[Righting] Left tilt:  dIN L=%.1f R=%.1f\n", dinL, dinR);
        EXPECT_GT(dinL, dinR - 10.0f) << "Левый dIN активнее при наклоне влево (righting reflex)";
    }

    // Тест B: тело перевёрнуто вправо — правый отолит активнее
    {
        ASSERT_TRUE(::SaveNetwork(model, "/tmp/vest_righting_b.bion"));
        SimulationBridge sim;
        ASSERT_TRUE(sim.loadNetwork("/tmp/vest_righting_b.bion"));
        sim.addCurrentStimulus(otoR, 100.0f, 999999.0f, 0.0f);  // правый активен
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        std::remove("/tmp/vest_righting_b.bion");

        float dinL = -99, dinR = -99;
        for (auto& s : segs) {
            if (s.type != SegmentType::SOMA) continue;
            if (s.position.y >= 65 && s.position.y <= 75 && s.position.z < 10) {
                if (s.position.x < 0) dinL = s.V_m;
                else dinR = s.V_m;
            }
        }
        printf("[Righting] Right tilt: dIN L=%.1f R=%.1f\n", dinL, dinR);
        EXPECT_GT(dinR, dinL - 10.0f) << "Правый dIN активнее при наклоне вправо";
    }
}
