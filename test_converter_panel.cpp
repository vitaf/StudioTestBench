// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// G3-P5: Тесты ConverterPanel (SWC convert, makeDefaultSegment, SegTip, shell_escape)
#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/swc_parser.h"
#include "../BioNeuronStudio/src/converter_panel.h"
#include "../BioNeuronStudio/src/data_model.h"

TEST(ConverterPanel, ConvertSwcToModel_Basic) {
    SwcMorphology morph;
    // Simple 3-point SWC: soma + dendrite
    morph.points.push_back({0, 1, 0.0f, 0.0f, 0.0f, 5.0f, -1});   // soma
    morph.points.push_back({1, 3, 0.0f, 10.0f, 0.0f, 2.0f, 0});     // dendrite from soma
    morph.points.push_back({2, 3, 0.0f, 20.0f, 0.0f, 1.0f, 1});     // dendrite child

    NetworkModel model;
    convertSwcToModel(model, morph);

    // Should create segments for each SWC point (soma + dendrites)
    EXPECT_GE(model.segments.size(), 3u);
    // Should have at least one connection (parent-child)
    EXPECT_GE(model.connections.size(), 2u);
}

TEST(ConverterPanel, ConvertSwcToModel_Empty) {
    SwcMorphology empty;
    NetworkModel model;
    convertSwcToModel(model, empty);
    EXPECT_EQ(model.segments.size(), 0u);
}

TEST(ConverterPanel, ConvertSwcToModel_SomaOnly) {
    SwcMorphology morph;
    morph.points.push_back({0, 1, 0.0f, 0.0f, 0.0f, 5.0f, -1});

    NetworkModel model;
    convertSwcToModel(model, morph);
    EXPECT_GE(model.segments.size(), 1u);
    EXPECT_EQ(model.segments[0].type, SegmentType::SOMA);
}

TEST(ConverterPanel, ConvertSwcToModel_PreservesTopology) {
    // Branching neuron: soma → 2 children
    SwcMorphology morph;
    morph.points.push_back({0, 1, 0.0f, 0.0f, 0.0f, 5.0f, -1});    // soma
    morph.points.push_back({1, 3, -5.0f, 10.0f, 0.0f, 1.5f, 0});    // left dendrite
    morph.points.push_back({2, 3, 5.0f, 10.0f, 0.0f, 1.5f, 0});     // right dendrite

    NetworkModel model;
    convertSwcToModel(model, morph);

    // Should have parent→child connections
    EXPECT_GE(model.connections.size(), 2u);
}

TEST(ConverterPanel, ConvertSwcToModel_MixedTypes) {
    SwcMorphology morph;
    morph.points.push_back({0, 1, 0.0f, 0.0f, 0.0f, 5.0f, -1});   // soma (type 1)
    morph.points.push_back({1, 2, 0.0f, 10.0f, 0.0f, 0.8f, 0});    // axon (type 2)

    NetworkModel model;
    convertSwcToModel(model, morph);
    EXPECT_GE(model.segments.size(), 2u);

    // Find axon segment
    bool has_axon = false;
    for (auto& s : model.segments)
        if (s.type == SegmentType::AXON) has_axon = true;
    EXPECT_TRUE(has_axon);
}
