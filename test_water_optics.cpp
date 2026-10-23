// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_habitat_3d.h"

TEST(WaterOptics, TurbidityAffectsClarity) {
    TadpoleHabitat3D h;
    h.Init();

    // Кристальная вода — clarity = 1.0
    EXPECT_FLOAT_EQ(h.water_turbidity, 0.1f);
    float clarity_default = 1.0f - h.water_turbidity;
    EXPECT_NEAR(clarity_default, 0.9f, 0.01f);

    // Мутная вода — clarity = 0.3
    h.water_turbidity = 0.7f;
    float clarity_murky = 1.0f - h.water_turbidity;
    EXPECT_NEAR(clarity_murky, 0.3f, 0.01f);
    EXPECT_LT(clarity_murky, clarity_default) << "Мутная вода пропускает меньше света";

    printf("[Optics] Clear: clarity=%.2f | Murky: clarity=%.2f\n", clarity_default, clarity_murky);
    h.Shutdown();
}

TEST(WaterOptics, SubstrateFriction) {
    TadpoleHabitat3D h;
    h.Init();

    EXPECT_FLOAT_EQ(h.substrate_friction, 0.8f);
    EXPECT_GT(h.substrate_friction, 0.0f) << "Трение о дно должно быть ненулевым";
    EXPECT_LT(h.substrate_friction, 1.0f) << "Но не полная остановка";

    printf("[Optics] Substrate friction=%.2f\n", h.substrate_friction);
    h.Shutdown();
}
