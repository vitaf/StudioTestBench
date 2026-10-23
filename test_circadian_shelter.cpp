// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_habitat_3d.h"

TEST(CircadianShelter, DaylightIntensity) {
    TadpoleHabitat3D h;
    h.Init();
    h.circadian_enabled = true;

    h.time_of_day = 12.0f;  // полдень
    EXPECT_NEAR(h.GetDaylightIntensity(), 1.0f, 0.01f);

    h.time_of_day = 0.0f;   // полночь
    EXPECT_NEAR(h.GetDaylightIntensity(), 0.0f, 0.01f);

    h.time_of_day = 6.0f;   // рассвет — половина
    float dawn = h.GetDaylightIntensity();
    EXPECT_GT(dawn, 0.0f); EXPECT_LT(dawn, 1.0f);

    printf("[Circ] Noon=%.2f Midnight=%.2f Dawn=%.2f\n",
           h.GetDaylightIntensity(), 0.0f, dawn);
    h.Shutdown();
}

TEST(CircadianShelter, ShelterShadow) {
    TadpoleHabitat3D h;
    h.Init();

    h.AddShelter({10, 0, 0}, 20.0f, 0.9f);
    EXPECT_EQ(h.shelters.size(), 1u);

    // Внутри укрытия — тень
    float s_center = h.QueryShelterShadow({10, 0, 0});
    EXPECT_NEAR(s_center, 0.9f, 0.01f);

    // На краю — частичная тень
    float s_edge = h.QueryShelterShadow({28, 0, 0});  // dist=18, radius=20, factor=0.1
    EXPECT_GT(s_edge, 0.0f); EXPECT_LT(s_edge, 0.9f);

    // Далеко — нет тени
    float s_far = h.QueryShelterShadow({100, 0, 0});
    EXPECT_FLOAT_EQ(s_far, 0.0f);

    printf("[Shelter] center=%.2f edge=%.2f far=%.2f\n", s_center, s_edge, s_far);
    h.Shutdown();
}
