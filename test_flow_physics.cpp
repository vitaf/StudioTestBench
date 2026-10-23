// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_habitat_3d.h"

TEST(FlowPhysics, VortexCreation) {
    TadpoleHabitat3D h;
    h.Init();

    EXPECT_EQ(h.vortices.size(), 0u);
    h.ShedVortex({0, 0, -60}, {0, 0, 5.0f});  // tail moving at speed 5
    EXPECT_EQ(h.vortices.size(), 1u);
    EXPECT_GT(h.vortices[0].strength, 0.0f);

    printf("[Flow] Vortex created: pos=(%.1f,%.1f,%.1f) strength=%.3f\n",
           h.vortices[0].pos.x, h.vortices[0].pos.y, h.vortices[0].pos.z, h.vortices[0].strength);
    h.Shutdown();
}

TEST(FlowPhysics, VortexDecay) {
    TadpoleHabitat3D h;
    h.Init();

    h.ShedVortex({0, 0, -60}, {0, 0, 10.0f});
    float s0 = h.vortices[0].strength;
    h.UpdateVortices(1.0f);  // 1 second
    EXPECT_LT(h.vortices[0].strength, s0) << "Vortex should decay over time";
    // After 2 more seconds it should be gone
    h.UpdateVortices(2.0f);
    h.UpdateVortices(2.0f);
    EXPECT_EQ(h.vortices.size(), 0u) << "Vortex should vanish after decay";

    printf("[Flow] Vortex: s0=%.3f -> after 1s: %.3f -> gone after 5s\n",
           s0, s0 * 0.5f);
    h.Shutdown();
}

TEST(FlowPhysics, LocalFlowWithVortices) {
    TadpoleHabitat3D h;
    h.Init();
    h.flow_active = true;
    h.flow_velocity = 1.0f;
    h.flow_dir = {0, 0, -1};

    // Without vortices — uniform flow
    auto f0 = h.GetFlowAt({0, 0, 0});
    EXPECT_GT(glm::length(f0), 0.9f);

    // Add a vortex
    h.ShedVortex({10, 0, 0}, {0, 0, 8.0f});
    auto f_near = h.GetFlowAt({10, 0, 0});  // right at vortex
    auto f_far  = h.GetFlowAt({100, 0, 0}); // far away

    // Near vortex should be different from far
    float diff = glm::length(f_near - f_far);
    EXPECT_GT(diff, 0.0f) << "Flow near vortex should differ from far field";

    printf("[Flow] Uniform: (%.2f,%.2f,%.2f) | Near vortex: (%.2f,%.2f,%.2f) | Far: (%.2f,%.2f,%.2f)\n",
           f0.x, f0.y, f0.z, f_near.x, f_near.y, f_near.z, f_far.x, f_far.y, f_far.z);
    h.Shutdown();
}
