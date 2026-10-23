// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// G3-P3: Тесты DataModel extension (find_neuron, ProbeRecording, create_synapse, add_modifier)
#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/data_model.h"

TEST(DataModelExt, FindNeuronForSegment) {
    NetworkModel m;
    m.neurons.push_back({0, 0, 3, 0});  // neuron 0: segments 0-2
    m.neurons.push_back({1, 3, 2, 0});  // neuron 1: segments 3-4

    // seg 0 belongs to neuron 0
    auto* n0 = m.find_neuron_for_segment(0);
    ASSERT_NE(n0, nullptr);
    EXPECT_EQ(n0->id, 0u);

    // seg 2 belongs to neuron 0
    auto* n0b = m.find_neuron_for_segment(2);
    ASSERT_NE(n0b, nullptr);
    EXPECT_EQ(n0b->id, 0u);

    // seg 3 belongs to neuron 1
    auto* n1 = m.find_neuron_for_segment(3);
    ASSERT_NE(n1, nullptr);
    EXPECT_EQ(n1->id, 1u);

    // seg 999 — no neuron
    EXPECT_EQ(m.find_neuron_for_segment(999), nullptr);
}

TEST(DataModelExt, ProbeRecordingPushWraps) {
    ProbeRecording pr;
    // Fill beyond capacity
    for (int i = 0; i < 15000; i++) {
        float ions[4] = {0, 0, 0, 0};
        pr.push(-65.0f + i * 0.01f, ions);
    }
    // Should not crash; count capped at 10000
    EXPECT_LE(pr.count, (unsigned)10000);
    EXPECT_GT(pr.count, (unsigned)0);
}

TEST(DataModelExt, ProbeRecordingClear) {
    ProbeRecording pr;
    float ions[4] = {0, 0, 0, 0};
    pr.push(-65.0f, ions);
    pr.push(-64.0f, ions);
    EXPECT_EQ(pr.count, 2u);
    pr.clear();
    EXPECT_EQ(pr.count, 0u);
}

TEST(DataModelExt, CreateSynapseMissingSegment) {
    NetworkModel m;
    // No segments at all — create_synapse should return nullptr
    auto* syn = m.create_synapse(0, 1, SynapseType::CHEMICAL);
    EXPECT_EQ(syn, nullptr);

    // Add one segment but reference non-existent post
    SegmentSnapshot seg;
    seg.id = 0; seg.neuron_id = 0;
    m.segments.push_back(seg);
    syn = m.create_synapse(0, 999, SynapseType::CHEMICAL);
    EXPECT_EQ(syn, nullptr);
}

TEST(DataModelExt, AddModifierAtMax) {
    SegmentSnapshot seg;
    seg.id = 0;
    // Add MAX_MODIFIERS_PER_SEGMENT modifiers
    for (int i = 0; i < MAX_MODIFIERS_PER_SEGMENT; i++) {
        seg.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    }
    EXPECT_EQ(seg.modifier_count, (uint8_t)MAX_MODIFIERS_PER_SEGMENT);

    // One more — should be no-op
    seg.add_modifier(ModifierType::MOD_K2P_LEAK, 0.5f);
    EXPECT_EQ(seg.modifier_count, (uint8_t)MAX_MODIFIERS_PER_SEGMENT);
}

TEST(DataModelExt, ModifierTypeRoundTrip) {
    Modifier m = {};
    m.type = (uint8_t)ModifierType::MOD_AXON_NAV12;
    EXPECT_EQ(m.mod_type(), ModifierType::MOD_AXON_NAV12);

    m.type = (uint8_t)ModifierType::MOD_NT_RECEPTOR_GLYCINE;
    EXPECT_EQ(m.mod_type(), ModifierType::MOD_NT_RECEPTOR_GLYCINE);

    m.type = (uint8_t)ModifierType::MOD_HCN2;
    EXPECT_EQ(m.mod_type(), ModifierType::MOD_HCN2);
}
