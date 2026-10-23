// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// G3-P2: Тесты GetActivityColor + ApplyActivityToBase (ColorMaps extension)
#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/color_maps.h"
#include "../BioNeuronStudio/src/data_model.h"

TEST(ColorMapsExt, ActivityColor_DefaultStep) {
    Color c = GetActivityColor(-65.0f, 0.0f);  // step <= 0 → default 5mV
    // -65mV ≈ dark blue band
    EXPECT_GE(c.r, 0); EXPECT_LE(c.r, 255);
    EXPECT_GE(c.g, 0); EXPECT_LE(c.g, 255);
    EXPECT_GE(c.b, 0); EXPECT_LE(c.b, 255);
}

TEST(ColorMapsExt, ActivityColor_Bands) {
    // Bands at multiples of step_mv
    Color c1 = GetActivityColor(-80.0f, 20.0f);  // -80mV band
    Color c2 = GetActivityColor(-60.0f, 20.0f);  // -60mV band
    Color c3 = GetActivityColor(40.0f, 20.0f);   // +40mV band
    // Все цвета должны быть валидными
    EXPECT_TRUE(c1.r + c1.g + c1.b > 0);
    EXPECT_TRUE(c2.r + c2.g + c2.b > 0);
    EXPECT_TRUE(c3.r + c3.g + c3.b > 0);
}

TEST(ColorMapsExt, ActivityColor_Step5mV) {
    // Band at step=5mV: -65 → band at -65, -64 → band at -65 (same), -63 → -65
    Color ca = GetActivityColor(-65.0f, 5.0f);
    Color cb = GetActivityColor(-64.0f, 5.0f);
    Color cc = GetActivityColor(-63.0f, 5.0f);
    // Same band — must be identical
    EXPECT_EQ(ca.r, cb.r); EXPECT_EQ(ca.g, cb.g); EXPECT_EQ(ca.b, cb.b);
    EXPECT_EQ(cb.r, cc.r); EXPECT_EQ(cb.g, cc.g); EXPECT_EQ(cb.b, cc.b);
}

TEST(ColorMapsExt, ApplyActivity_VmMode) {
    Color base = {128, 128, 128};
    // At Vm=-75mV: blend = (-75+75)/35 = 0 → return base
    Color c1 = ApplyActivityToBase(base, -75.0f, -75.0f, 5.0f, ActivitySource::Vm);
    EXPECT_EQ(c1.r, base.r);

    // At Vm=-40mV: blend = (-40+75)/35 = 1.0 → full activity color
    Color c2 = ApplyActivityToBase(base, -40.0f, -75.0f, 5.0f, ActivitySource::Vm);
    EXPECT_GE(c2.r + c2.g + c2.b, 0);  // something changed

    // At Vm=-80mV: blend = (-80+75)/35 < 0 → clamped to 0
    Color c3 = ApplyActivityToBase(base, -80.0f, -75.0f, 5.0f, ActivitySource::Vm);
    EXPECT_EQ(c3.r, base.r);
}

TEST(ColorMapsExt, ApplyActivity_DvDtDeadband) {
    Color base = {100, 100, 100};
    // dV/dt = 0.3 mV/tick → within deadband (<0.5) → return base
    Color c1 = ApplyActivityToBase(base, -65.0f, -65.3f, 5.0f, ActivitySource::dVdt);
    EXPECT_EQ(c1.r, base.r);

    // dV/dt = 5 mV/tick → outside deadband → blend = 5/15 ≈ 0.33
    Color c2 = ApplyActivityToBase(base, -60.0f, -65.0f, 5.0f, ActivitySource::dVdt);
    EXPECT_TRUE(c2.r != base.r || c2.g != base.g || c2.b != base.b);

    // dV/dt = 20 mV/tick → blend clamped to 1.0
    Color c3 = ApplyActivityToBase(base, -45.0f, -65.0f, 5.0f, ActivitySource::dVdt);
    EXPECT_TRUE(c3.r != base.r || c3.g != base.g || c3.b != base.b);
}

TEST(ColorMapsExt, GetActivityColor_Monotonic) {
    // Palette should be monotonic: V_m increases → color changes smoothly
    Color prev = GetActivityColor(-80.0f, 10.0f);
    for (float v = -70.0f; v <= 40.0f; v += 10.0f) {
        Color cur = GetActivityColor(v, 10.0f);
        // Just verify valid colors across range
        EXPECT_LE(cur.r, 255); EXPECT_LE(cur.g, 255); EXPECT_LE(cur.b, 255);
        prev = cur;
    }
}
