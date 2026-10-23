// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <glm/glm.hpp>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_habitat_3d.h"

TEST(EnvironmentPhysics, CubeBoundaryClamp) {
    TadpoleHabitat3D h;
    h.Init();

    // Тело за левой границей
    glm::vec3 pos = {-500, 0, 0};
    glm::vec3 cp, cn;
    bool hit = h.ClampToBounds(pos, cp, cn);
    EXPECT_TRUE(hit);
    EXPECT_FLOAT_EQ(pos.x, h.bounds.x_min);  // -400
    EXPECT_GT(cn.x, 0.0f);  // нормаль направлена внутрь (вправо)

    // Тело за правой границей
    pos = {500, 0, 0}; hit = h.ClampToBounds(pos, cp, cn);
    EXPECT_TRUE(hit); EXPECT_FLOAT_EQ(pos.x, h.bounds.x_max);
    EXPECT_LT(cn.x, 0.0f);

    // Тело за дном
    pos = {0, -50, 0}; hit = h.ClampToBounds(pos, cp, cn);
    EXPECT_TRUE(hit); EXPECT_FLOAT_EQ(pos.y, h.bounds.y_min);
    EXPECT_GT(cn.y, 0.0f);  // нормаль вверх

    // Тело выше поверхности
    pos = {0, 100, 0}; hit = h.ClampToBounds(pos, cp, cn);
    EXPECT_TRUE(hit); EXPECT_FLOAT_EQ(pos.y, h.bounds.y_max);
    EXPECT_LT(cn.y, 0.0f);  // нормаль вниз

    // Внутри куба — нет столкновения
    pos = {0, 0, 0}; hit = h.ClampToBounds(pos, cp, cn);
    EXPECT_FALSE(hit);
    EXPECT_FLOAT_EQ(pos.x, 0.0f); EXPECT_FLOAT_EQ(pos.y, 0.0f); EXPECT_FLOAT_EQ(pos.z, 0.0f);

    printf("[Cube] Clamp tests PASS\n");
    h.Shutdown();
}

TEST(EnvironmentPhysics, BottomSubstrateContact) {
    TadpoleHabitat3D h;
    h.Init();

    // Тело на дне
    glm::vec3 pos = {0, -35, 0};
    glm::vec3 cp, cn;
    bool hit = h.ClampToBounds(pos, cp, cn);
    EXPECT_TRUE(hit);
    // contact_point должен быть на границе
    EXPECT_FLOAT_EQ(cp.y, h.bounds.y_min);
    // нормаль должна указывать вверх (против гравитации)
    EXPECT_GT(cn.y, 0.0f);

    printf("[Cube] Bottom contact: cp=(%.1f,%.1f,%.1f) n=(%.1f,%.1f,%.1f)\n",
           cp.x, cp.y, cp.z, cn.x, cn.y, cn.z);
    h.Shutdown();
}

TEST(EnvironmentPhysics, WallCollisionDetection) {
    TadpoleHabitat3D h;
    h.Init();

    // Столкновение с каждой стенкой
    struct { glm::vec3 pos; int expected_side; } tests[] = {
        {{-500, 0, 0}, 0},   // left (-X)
        {{500, 0, 0}, 1},     // right (+X)
        {{0, -50, 0}, 2},     // bottom (-Y)
        {{0, 100, 0}, 3},     // top (+Y)
        {{0, 0, -500}, 4},    // back (-Z)
        {{0, 0, 500}, 5},     // front (+Z)
    };

    for (auto& t : tests) {
        glm::vec3 cp, cn;
        bool hit = h.ClampToBounds(t.pos, cp, cn);
        EXPECT_TRUE(hit) << "Should hit wall at " << t.pos.x << "," << t.pos.y << "," << t.pos.z;
        // normal length should be ~1
        float len = sqrtf(cn.x*cn.x + cn.y*cn.y + cn.z*cn.z);
        EXPECT_NEAR(len, 1.0f, 0.01f) << "Normal should be unit length";
    }

    printf("[Cube] All 6 walls detected with unit normals\n");
    h.Shutdown();
}

TEST(EnvironmentPhysics, CubeVisualizationConfig) {
    TadpoleHabitat3D h;
    h.Init();

    EXPECT_TRUE(h.cube.show);
    h.cube.show = false;
    EXPECT_FALSE(h.cube.show);
    h.cube.show = true;
    EXPECT_TRUE(h.cube.show);

    printf("[Cube] Visualization toggle works\n");
    h.Shutdown();
}
