// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_myotome.cpp — тесты сегментарной модели мышц хвоста
// Проверяют: инициализацию, нулевую активацию, симметричный изгиб, thrust, stretch

#include <gtest/gtest.h>
#include <cmath>
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_myotome.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 1: Инициализация — прямой хвост, нулевые углы
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, InitCreatesStraightTail) {
    TadpoleMyotomeModel m;
    m.Init(16);

    EXPECT_EQ(m.num_segments(), 16);
    EXPECT_EQ(m.segments().size(), 16u);
    EXPECT_EQ(m.joint_angles().size(), 16u);
    EXPECT_NEAR(m.total_thrust(), 0.0f, 1e-6f);
    EXPECT_NEAR(m.total_turn_torque(), 0.0f, 1e-6f);

    for (int i = 0; i < 16; i++) {
        EXPECT_NEAR(m.joint_angles()[i], 0.0f, 1e-6f);
        EXPECT_NEAR(m.stretch_amplitude(i), 0.0f, 1e-6f);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 2: Нулевая MN активация — хвост остаётся прямым
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, ZeroActivationKeepsTailStraight) {
    TadpoleMyotomeModel m;
    m.Init(8);

    float left[8] = {};
    float right[8] = {};

    // 10 шагов по ~10ms = 100ms симуляции
    for (int t = 0; t < 10; t++)
        m.Step(0.01f, left, right, 0.0f);

    for (int i = 0; i < 8; i++)
        EXPECT_NEAR(m.joint_angles()[i], 0.0f, 0.01f)
            << "Сегмент " << i << " должен оставаться прямым без MN активации";
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 3: Симметричная активация — изгиб без поворота
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, SymmetricActivationBendsTailForward) {
    TadpoleMyotomeModel m;
    m.Init(8);

    float left[8]  = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float right[8] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    // Несколько шагов для развития движения
    for (int t = 0; t < 50; t++)
        m.Step(0.002f, left, right, 0.0f);

    // Симметричная активация не должна давать большой асимметрии
    float cum_angle = 0.0f;
    for (int i = 0; i < 8; i++)
        cum_angle += m.joint_angles()[i];
    EXPECT_NEAR(cum_angle, 0.0f, 0.2f)
        << "Симметричная активация не должна создавать суммарный поворот";

    // Turn torque должен быть ограничен при симметричной активации
    EXPECT_LT(std::fabs(m.total_turn_torque()), 3.0f)
        << "Симметричная MN → ограниченный момент поворота";
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 4: Асимметричная активация — поворот
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, AsymmetricActivationCreatesTurn) {
    TadpoleMyotomeModel m;
    m.Init(8);

    float left[8]  = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
    float right[8] = {0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};

    // Достаточно шагов для установления асимметрии
    // body_speed > 0 нужно для вычисления turn_torque (drag-based)
    for (int t = 0; t < 100; t++)
        m.Step(0.002f, left, right, 0.05f);

    // Левая активация > правой → левые мышцы сокращаются → хвост гнётся влево (полож. углы)
    float cum_angle = 0.0f;
    for (int i = 0; i < 8; i++)
        cum_angle += m.joint_angles()[i];
    EXPECT_GT(cum_angle, 0.0f)
        << "Левая MN > правая → хвост должен изгибаться влево (положительные углы)";

    // Turn torque должен быть ненулевым
    EXPECT_NE(std::fabs(m.total_turn_torque()), 0.0f)
        << "Асимметричная MN → ненулевой момент поворота";
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 5: Углы не превышают max_angle
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, MaxAngleIsRespected) {
    TadpoleMyotomeModel m;
    m.Init(8);
    m.cfg_.max_angle = 0.8f;

    float max_act[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    // Длительная максимальная активация
    for (int t = 0; t < 200; t++)
        m.Step(0.002f, max_act, max_act, 0.0f);

    for (int i = 0; i < 8; i++) {
        EXPECT_LE(std::fabs(m.joint_angles()[i]), 0.81f)
            << "Угол сегмента " << i << " превышает max_angle=0.8";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 6: Пассивная жёсткость возвращает хвост к прямому состоянию
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, PassiveStiffnessRestoresStraight) {
    TadpoleMyotomeModel m;
    m.Init(8);
    // Умеренная жёсткость чтобы изгиб был заметен
    m.cfg_.joint_stiffness = 0.2f;
    m.cfg_.joint_damping = 0.15f;

    float left[8]  = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float right[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // Сначала создаём изгиб (больше шагов для развития амплитуды)
    for (int t = 0; t < 150; t++)
        m.Step(0.002f, left, right, 0.0f);

    // Запоминаем углы при активации
    float max_deflection = 0.0f;
    for (int i = 0; i < 8; i++)
        if (std::fabs(m.joint_angles()[i]) > max_deflection)
            max_deflection = std::fabs(m.joint_angles()[i]);

    EXPECT_GT(max_deflection, 0.02f) << "Изгиб должен быть заметен при активации (max=" << max_deflection << ")";

    // Теперь выключаем активацию
    float zero[8] = {};
    for (int t = 0; t < 200; t++)
        m.Step(0.002f, zero, zero, 0.0f);

    // Хвост должен вернуться близко к прямому
    for (int i = 0; i < 8; i++) {
        EXPECT_NEAR(m.joint_angles()[i], 0.0f, 0.05f)
            << "После снятия активации сегмент " << i << " должен вернуться к 0 (angle=" << m.joint_angles()[i] << ")";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 7: Thrust растёт с активацией MN
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, ThrustIncreasesWithActivation) {
    TadpoleMyotomeModel m;
    m.Init(16);

    float low[16]  = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f,
                      0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
    float high[16] = {0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f,
                      0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f};

    // Прогоняем с низкой активацией
    for (int t = 0; t < 30; t++)
        m.Step(0.002f, low, low, 0.01f);
    float thrust_low = std::fabs(m.total_thrust());

    // Сбрасываем и прогоняем с высокой
    m.Init(16);
    for (int t = 0; t < 30; t++)
        m.Step(0.002f, high, high, 0.01f);
    float thrust_high = std::fabs(m.total_thrust());

    EXPECT_GT(thrust_high, thrust_low)
        << "Высокая активация MN должна давать большую тягу (thrust_high="
        << thrust_high << " vs thrust_low=" << thrust_low << ")";

    // Тяга должна быть измеримой при активации
    EXPECT_GT(thrust_high, 0.001f)
        << "При MN=0.6 должна быть измеримая тяга";
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 8: Stretch amplitude корректна для изогнутого хвоста
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, StretchAmplitudeReflectsCurvature) {
    TadpoleMyotomeModel m;
    m.Init(8);
    m.cfg_.joint_stiffness = 0.0f;  // убираем жёсткость для чистого изгиба

    float left[8]  = {0.3f, 0.5f, 0.7f, 1.0f, 1.0f, 0.7f, 0.5f, 0.3f};
    float right[8] = {0.7f, 0.5f, 0.3f, 0.0f, 0.0f, 0.3f, 0.5f, 0.7f};

    for (int t = 0; t < 30; t++)
        m.Step(0.002f, left, right, 0.0f);

    // При асимметричной активации stretch должен быть ненулевым
    float max_stretch = 0.0f;
    for (int i = 0; i < 8; i++) {
        float s = m.stretch_amplitude(i);
        if (s > max_stretch) max_stretch = s;
    }
    EXPECT_GT(max_stretch, 0.01f)
        << "При асимметричной активации stretch должен быть >0";

    // stretch ограничен [0, 1]
    for (int i = 0; i < 8; i++) {
        float s = m.stretch_amplitude(i);
        EXPECT_GE(s, 0.0f);
        EXPECT_LE(s, 1.0f);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 9: Регрессия — contract_gain по умолчанию (калибровка Xenopus)
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, DefaultConfigIsPhysiologicallyReasonable) {
    TadpoleMyotomeModel m;
    m.Init(16);

    // Параметры по умолчанию из MyotomeConfig
    EXPECT_EQ(m.cfg_.num_segments, 16);
    EXPECT_NEAR(m.cfg_.segment_length, 3.75f, 1e-6f);
    EXPECT_GT(m.cfg_.contract_gain, 0.05f);
    EXPECT_LT(m.cfg_.contract_gain, 0.2f);
    EXPECT_GT(m.cfg_.joint_stiffness, 0.3f);
    EXPECT_LT(m.cfg_.joint_stiffness, 1.2f);
    EXPECT_GT(m.cfg_.max_angle, 0.5f);
    EXPECT_LT(m.cfg_.max_angle, 1.2f);

    // drag_coeff должен быть мал (вода)
    EXPECT_GT(m.cfg_.drag_coeff, 0.001f);
    EXPECT_LT(m.cfg_.drag_coeff, 0.1f);

    // lateral_drag > 1 (боком труднее плыть)
    EXPECT_GT(m.cfg_.lateral_drag_ratio, 2.0f);
    EXPECT_LT(m.cfg_.lateral_drag_ratio, 6.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 10: Амплитуда хвоста ~20% длины тела (Hoff & Wassersug 1986)
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, TailAmplitudeMatchesLiterature) {
    TadpoleMyotomeModel m;
    m.Init(16);
    m.cfg_.joint_stiffness = 0.6f;
    m.cfg_.contract_gain = 0.12f;

    // Крейсерская MN активация (~0.4)
    float cruise[16];
    for (int i = 0; i < 16; i++) cruise[i] = 0.4f;

    for (int t = 0; t < 100; t++)
        m.Step(0.002f, cruise, cruise, 0.02f);

    // Боковое смещение кончика хвоста
    const auto& pos = m.segment_positions();
    float tip_x = std::fabs(pos[16].x);

    // Длина тела = 90, 20% = 18. Допуск ±50%
    EXPECT_GT(tip_x, 5.0f)
        << "Амплитуда кончика хвоста (" << tip_x << ") слишком мала (<5% длины тела)";
    EXPECT_LT(tip_x, 30.0f)
        << "Амплитуда кончика хвоста (" << tip_x << ") слишком велика (>33% длины тела)";
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 11: turn_torque не равен нулю при движении хвоста
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, TurnTorqueNonZeroWhenTailMoving) {
    TadpoleMyotomeModel m;
    m.Init(8);

    float act[8] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
    for (int t = 0; t < 100; t++)
        m.Step(0.002f, act, act, 0.05f);
    // При MN активации хвост движется → turn_torque измерим
    EXPECT_NE(std::fabs(m.total_turn_torque()), 0.0f)
        << "При MN активации turn_torque должен быть ненулевым";
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 12: Step() без heading компилируется и работает
// ═══════════════════════════════════════════════════════════════════════════
TEST(Myotome, StepSignatureDoesNotRequireHeading) {
    TadpoleMyotomeModel m;
    m.Init(8);
    // Проверка что Step() принимает 4 аргумента (без heading)
    float act[8] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    m.Step(0.002f, act, act, 0.05f);
    // Если компилируется — heading удалён из сигнатуры
    EXPECT_EQ(m.num_segments(), 8);
}
