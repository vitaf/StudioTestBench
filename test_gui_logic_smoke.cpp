// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.
// test_gui_logic_smoke.cpp — smoke-тесты логики GUI-компонентов (без OpenGL/ImGui)

#include <gtest/gtest.h>
#include <cmath>
#include "data_model.h"
#include "color_maps.h"
#include "history_buffer.h"

// ═══════════════════════════════════════════════════════════════
// ColorMaps — чистая математика, без рендеринга
// ═══════════════════════════════════════════════════════════════

TEST(ColorMaps, VmToColorResting) {
    Color c = VmToColor(-70.0f);
    // V_m = -70: должен быть в диапазоне синий→голубой
    EXPECT_TRUE(c.r >= 0.0f && c.r <= 1.0f);
    EXPECT_TRUE(c.g >= 0.0f && c.g <= 1.0f);
    EXPECT_TRUE(c.b >= 0.0f && c.b <= 1.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(ColorMaps, VmToColorDepolarized) {
    Color c = VmToColor(0.0f);
    // V_m = 0: оранжевый→красный
    EXPECT_GT(c.r, 0.5f);
}

TEST(ColorMaps, VmToColorHyperpol) {
    Color c = VmToColor(-100.0f);
    // V_m = -100: глубокий синий
    EXPECT_GT(c.b, c.r);
}

TEST(ColorMaps, VmToColorNaN) {
    Color c = VmToColor(NAN);
    // NaN не должен крашнуть — возвращаем дефолтный цвет
    EXPECT_TRUE(c.r >= 0.0f && c.r <= 1.0f);
}

TEST(ColorMaps, IonToColorRange) {
    Color low = IonToColor(0.0f, 0.0f, 100.0f);
    Color mid = IonToColor(50.0f, 0.0f, 100.0f);
    Color high = IonToColor(100.0f, 0.0f, 100.0f);
    // Все валидные цвета
    EXPECT_TRUE(low.a == 1.0f);
    EXPECT_TRUE(mid.a == 1.0f);
    EXPECT_TRUE(high.a == 1.0f);
}

TEST(ColorMaps, SegmentTypeColorAllTypes) {
    for (int i = 0; i <= 5; i++) {
        Color c = SegmentTypeColor(static_cast<SegmentType>(i));
        EXPECT_TRUE(c.a == 1.0f) << "SegmentType " << i;
    }
}

TEST(ColorMaps, ActivityColorStable) {
    Color c = GetActivityColor(-70.0f, 5.0f);
    EXPECT_TRUE(c.a == 1.0f);
}

TEST(ColorMaps, ActivityColorSpike) {
    Color c = GetActivityColor(0.0f, 5.0f);
    EXPECT_TRUE(c.a == 1.0f);
}

// ═══════════════════════════════════════════════════════════════
// HistoryBuffer — кольцевой буфер
// ═══════════════════════════════════════════════════════════════

TEST(HistoryBuffer, AddDataPoint) {
    SegmentHistory h;
    h.addDataPoint(100, -70.0f, 10.0f, 140.0f, 0.0001f, 10.0f);
    EXPECT_EQ(h.vm.size(), 1u);
    EXPECT_FLOAT_EQ(h.vm[0], -70.0f);
    EXPECT_EQ(h.ticks[0], 100u);
}

TEST(HistoryBuffer, MaxPointsCap) {
    SegmentHistory h;
    for (uint32_t i = 0; i < SegmentHistory::MAX_POINTS + 100; i++) {
        h.addDataPoint(i, -70.0f + (float)(i % 10), 10.0f, 140.0f, 0.0001f, 10.0f);
    }
    EXPECT_EQ(h.vm.size(), SegmentHistory::MAX_POINTS);
    // Первые точки удалены
    EXPECT_EQ(h.ticks[0], 100u);
}

// ═══════════════════════════════════════════════════════════════
// DataModel — Modifier struct (sync с Core)
// ═══════════════════════════════════════════════════════════════

TEST(DataModel, ModifierSize) {
    EXPECT_EQ(sizeof(Modifier), 44u);
}

TEST(DataModel, ModifierModType) {
    Modifier m;
    m.type = static_cast<uint8_t>(ModifierType::MOD_AXON_NAV12);
    EXPECT_EQ(m.mod_type(), ModifierType::MOD_AXON_NAV12);
}

TEST(DataModel, SegmentSnapshotAddModifier) {
    SegmentSnapshot seg;
    EXPECT_EQ(seg.modifier_count, 0);
    seg.add_modifier(ModifierType::MOD_AXON_NAV12, 0.8f);
    EXPECT_EQ(seg.modifier_count, 1);
    EXPECT_EQ(seg.modifiers[0].mod_type(), ModifierType::MOD_AXON_NAV12);
    EXPECT_FLOAT_EQ(seg.modifiers[0].density, 0.8f);
}

TEST(DataModel, SegmentSnapshotRemoveModifier) {
    SegmentSnapshot seg;
    seg.add_modifier(ModifierType::MOD_AXON_NAV12, 0.8f);
    seg.add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
    EXPECT_EQ(seg.modifier_count, 2);
    seg.remove_modifier(0);
    EXPECT_EQ(seg.modifier_count, 1);
    EXPECT_EQ(seg.modifiers[0].mod_type(), ModifierType::MOD_PUMP_NAK);
}

TEST(DataModel, ModifierMaxCount) {
    SegmentSnapshot seg;
    for (int i = 0; i < MAX_MODIFIERS_PER_SEGMENT + 5; i++) {
        seg.add_modifier(ModifierType::MOD_AXON_NAV12, 0.5f);
    }
    EXPECT_EQ(seg.modifier_count, MAX_MODIFIERS_PER_SEGMENT);
}

// ═══════════════════════════════════════════════════════════════
// JSON utils — smoke test
// ═══════════════════════════════════════════════════════════════

#include "json_utils.h"

TEST(JsonUtils, GetString) {
    EXPECT_EQ(json_utils::get_string(R"({"key":"value"})", "key"), "value");
}

TEST(JsonUtils, GetStringMissing) {
    EXPECT_EQ(json_utils::get_string(R"({"key":"value"})", "other"), "");
}

TEST(JsonUtils, GetInt) {
    EXPECT_EQ(json_utils::get_int(R"({"count":42})", "count"), 42);
}

TEST(JsonUtils, GetIntDefault) {
    EXPECT_EQ(json_utils::get_int(R"({"count":42})", "missing", -1), -1);
}

TEST(JsonUtils, EscapeHandling) {
    EXPECT_EQ(json_utils::get_string(R"({"path":"a\/b"})", "path"), "a/b");
    EXPECT_EQ(json_utils::get_string(R"({"msg":"hello\"world"})", "msg"), "hello\"world");
}
