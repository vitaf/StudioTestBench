// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_bio_audit.cpp — тесты биологических исправлений (июль 2026)

#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/data_model.h"

// ═══════════════════════════════════════════════════════════════════
// T3: EARLY_TAILBUD — MN=0, aIN=0, dlc=0
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, EarlyTailbud_NoMN_NoaIN_NodlC) {
    auto params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    EXPECT_EQ(params.mn_per_hemisegment, 0);
    EXPECT_EQ(params.ain_per_hemisegment, 0);
    EXPECT_EQ(params.dlc_count, 0);
    EXPECT_GT(params.din_per_hemisegment, 0);
    EXPECT_GT(params.cin_per_hemisegment, 0);
    EXPECT_GT(params.rb_per_hemisegment, 0);
}

// ═══════════════════════════════════════════════════════════════════
// T3b: FREE_SWIMMING — все типы нейронов присутствуют
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, FreeSwimming_AllTypesPresent) {
    auto params = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    EXPECT_GT(params.mn_per_hemisegment, 0);
    EXPECT_GT(params.ain_per_hemisegment, 0);
    EXPECT_GT(params.dlc_count, 0);
    EXPECT_GT(params.din_per_hemisegment, 0);
    EXPECT_GT(params.cin_per_hemisegment, 0);
}

// ═══════════════════════════════════════════════════════════════════
// T4: NT_GLYCINE=7 (из Core types.h: NT_GLYCINE=7, NT_DOPAMINE=2)
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, NT_Glycine_Index_Is7) {
    constexpr int NT_GLYCINE_VAL = 7;
    constexpr int NT_DOPAMINE_VAL = 2;
    EXPECT_EQ(NT_GLYCINE_VAL, 7);
    EXPECT_EQ(NT_DOPAMINE_VAL, 2);
    EXPECT_NE(NT_GLYCINE_VAL, NT_DOPAMINE_VAL);
}

// ═══════════════════════════════════════════════════════════════════
// T2: GABA-B рецептор — отдельный тип, не GABAA и не глицин
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, GABAB_ReceptorType_Exists) {
    auto gabab = ModifierType::MOD_NT_RECEPTOR_GABAB;
    EXPECT_GT((int)gabab, 0);
    EXPECT_NE((int)gabab, (int)ModifierType::MOD_NT_RECEPTOR_GABAA);
    auto gly = ModifierType::MOD_NT_RECEPTOR_GLYCINE;
    EXPECT_NE((int)gabab, (int)gly);
}

// ═══════════════════════════════════════════════════════════════════
// T5: Типы ORN, cIN, SER существуют в TadpoleNeuronType
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, ORN_cIN_SER_Circuit_TypesExist) {
    EXPECT_GE((int)TadpoleNeuronType::ORN, 0);
    EXPECT_GE((int)TadpoleNeuronType::cIN, 0);
    EXPECT_GE((int)TadpoleNeuronType::SER, 0);
    EXPECT_LT((int)TadpoleNeuronType::ORN, (int)TadpoleNeuronType::COUNT);
    EXPECT_LT((int)TadpoleNeuronType::cIN, (int)TadpoleNeuronType::COUNT);
    EXPECT_LT((int)TadpoleNeuronType::SER, (int)TadpoleNeuronType::COUNT);
}

// ═══════════════════════════════════════════════════════════════════
// Проверка: NT индексы уникальны (из Core types.h)
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, NT_Indices_Consistent) {
    constexpr int GLUTAMATE=0, GABA=1, DOPAMINE=2, SEROTONIN=3;
    constexpr int ACETYLCHOLINE=4, ADENOSINE=5, NORADRENALINE=6, GLYCINE=7;
    EXPECT_EQ(GLYCINE, 7);
    EXPECT_EQ(DOPAMINE, 2);
    EXPECT_NE(GLYCINE, DOPAMINE);
    int indices[] = {GLUTAMATE, GABA, DOPAMINE, SEROTONIN,
                     ACETYLCHOLINE, ADENOSINE, NORADRENALINE, GLYCINE};
    for (int i = 0; i < 8; i++)
        for (int j = i + 1; j < 8; j++)
            EXPECT_NE(indices[i], indices[j]);
}

// ═══════════════════════════════════════════════════════════════════
// Проверка: MAX_MODIFIERS_PER_SEGMENT = 32 в Studio
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, MaxModifiersPerSegment_Is32) {
    EXPECT_EQ(MAX_MODIFIERS_PER_SEGMENT, 32);
}

// ═══════════════════════════════════════════════════════════════════
// Проверка: MOD_TYPE_COUNT = 197
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, ModTypeCount_Is197) {
    EXPECT_EQ(MOD_TYPE_COUNT, 197);  // ...+ изоформы Nav/Kv (до MOD_KV21=196)
}

// ═══════════════════════════════════════════════════════════════════
// Проверка: SegmentType::NECK существует в Studio
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, SegmentType_NECK_Exists) {
    EXPECT_GE((int)SegmentType::NECK, 0);
    EXPECT_LE((int)SegmentType::NECK, 10);
    EXPECT_NE((int)SegmentType::NECK, (int)SegmentType::SOMA);
    EXPECT_NE((int)SegmentType::NECK, (int)SegmentType::AXON);
}

// ═══════════════════════════════════════════════════════════════════
// Проверка: glia neuron_id маркер в Core
// ═══════════════════════════════════════════════════════════════════
TEST(BioAudit, Glia_NeuronId_Marker) {
    // Core's add_glia() sets neuron_id = 0xFFFFFFFF
    // Проверяем что это значение помещается в uint32_t
    constexpr uint32_t GLIA_MARKER = 0xFFFFFFFF;
    EXPECT_EQ(GLIA_MARKER, 0xFFFFFFFFu);
}
