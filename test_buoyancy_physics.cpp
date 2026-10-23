// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

#include <gtest/gtest.h>
#include <cstdio>
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_habitat_3d.h"

TEST(BuoyancyPhysics, NegativeBuoyancy) {
    TadpoleHabitat3D h;
    h.Init();

    EXPECT_LT(h.buoyancy, 0.0f) << "Головастик должен иметь отрицательную плавучесть (тонет без движения)";
    // Без плавания, за 10 секунд тело опустится на buoyancy*10 метров
    float sink_10s = h.buoyancy * 10.0f;
    EXPECT_LT(sink_10s, -0.1f) << "За 10с должно опуститься заметно";
    EXPECT_GT(sink_10s, -2.0f) << "Но не слишком быстро";

    printf("[Buoy] buoyancy=%.3f m/s², sink in 10s: %.2f m\n", h.buoyancy, sink_10s);
    h.Shutdown();
}

TEST(BuoyancyPhysics, SurfaceClampY) {
    TadpoleHabitat3D h;
    h.Init();

    // Поверхность воды — верхняя граница
    EXPECT_FLOAT_EQ(h.bounds.y_max, 80.0f);

    // Дно — нижняя граница
    EXPECT_FLOAT_EQ(h.bounds.y_min, -30.0f);

    glm::vec3 pos = {0, 100, 0};
    glm::vec3 cp, cn;
    bool hit = h.ClampToBounds(pos, cp, cn);
    EXPECT_TRUE(hit);
    EXPECT_FLOAT_EQ(pos.y, h.bounds.y_max) << "Тело выше поверхности — прижимается вниз";

    printf("[Buoy] Surface at y=%.0f, bottom at y=%.0f\n", h.bounds.y_max, h.bounds.y_min);
    h.Shutdown();
}
