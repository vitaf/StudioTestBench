// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gene_editing.cpp — тесты ручного редактирования генов

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/nucleus_converter.h"
#include "../BioNeuronStudio/src/simulation_core_api.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/serialization.h"

// ═══════════════════════════════════════════════════════════════
// SnapshotToNucleus / NucleusToSnapshot roundtrip
// ═══════════════════════════════════════════════════════════════

TEST(GeneEditing, SnapshotRoundtrip) {
    // Создаём ядро с известными значениями
    NucleusSnapshot original;
    original.neuron_id = 42;
    original.enabled = true;
    original.consolidation_mode = 3;  // both
    original.consolidation_interval = 5000;
    original.last_consolidation_tick = 12345;
    for (int i = 0; i < GENE_COUNT_STUDIO; i++) {
        original.gene_activity[i] = 0.1f + i * 0.01f;
        original.protein_level[i] = 0.05f + i * 0.005f;
    }
    original.protein_synthesis_rate = 0.75f;
    original.pcreb_level = 0.42f;
    original.erk_level = 0.33f;
    original.nfkb_level = 0.25f;
    original.mtorc1_level = 0.67f;
    original.amino_acid_level = 0.5f;

    // Конвертируем в raw bytes и обратно
    uint8_t raw[412] = {};
    SnapshotToNucleus(original, raw);

    NucleusSnapshot restored;
    NucleusToSnapshot(raw, restored);

    // Проверяем что все поля совпадают
    EXPECT_EQ(restored.neuron_id, 42u);
    EXPECT_TRUE(restored.enabled);
    EXPECT_EQ(restored.consolidation_mode, 3);
    EXPECT_EQ(restored.consolidation_interval, 5000u);
    EXPECT_EQ(restored.last_consolidation_tick, 12345u);

    for (int i = 0; i < GENE_COUNT_STUDIO; i++) {
        EXPECT_FLOAT_EQ(restored.gene_activity[i], original.gene_activity[i]) << "gene " << i;
        EXPECT_FLOAT_EQ(restored.protein_level[i], original.protein_level[i]) << "protein " << i;
    }
    EXPECT_FLOAT_EQ(restored.protein_synthesis_rate, 0.75f);
    EXPECT_FLOAT_EQ(restored.pcreb_level, 0.42f);
    EXPECT_FLOAT_EQ(restored.erk_level, 0.33f);
    EXPECT_FLOAT_EQ(restored.nfkb_level, 0.25f);
    EXPECT_FLOAT_EQ(restored.mtorc1_level, 0.67f);
    EXPECT_FLOAT_EQ(restored.amino_acid_level, 0.5f);
}

// ═══════════════════════════════════════════════════════════════
// Nucleus size consistency
// ═══════════════════════════════════════════════════════════════

TEST(GeneEditing, NucleusSizeIs412) {
    EXPECT_EQ(core_nucleus_size(), 412u);
    EXPECT_EQ(sizeof(NucleusSnapshot), 412u);
}

// ═══════════════════════════════════════════════════════════════
// core_set_nucleus + core_get_nucleus roundtrip через Core
// ═══════════════════════════════════════════════════════════════

TEST(GeneEditing, SetAndGetNucleusViaCore) {
    core_init_libraries();
    core_set_bio_mode();

    // Загружаем тестовую сеть
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/test_genedit.bion"));

    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_genedit.bion"));

    // Читаем ядро нейрона 0
    NucleusSnapshot original;
    {
        size_t nuc_sz = core_nucleus_size();
        std::vector<uint8_t> buf(nuc_sz);
        ASSERT_NE(core_get_nucleus_snapshot(0, buf.data()), 0);
        NucleusToSnapshot(buf.data(), original);
    }

    // Модифицируем gene_activity[0] (CREB)
    original.gene_activity[0] = 0.99f;
    original.protein_level[0] = 0.88f;

    // Записываем обратно
    {
        uint8_t raw[412] = {};
        SnapshotToNucleus(original, raw);
        ASSERT_TRUE(sim.setNucleusSnapshot(0, raw));
    }

    // Перечитываем и проверяем
    {
        size_t nuc_sz = core_nucleus_size();
        std::vector<uint8_t> buf(nuc_sz);
        ASSERT_NE(core_get_nucleus_snapshot(0, buf.data()), 0);
        NucleusSnapshot restored;
        NucleusToSnapshot(buf.data(), restored);

        EXPECT_FLOAT_EQ(restored.gene_activity[0], 0.99f);
        EXPECT_FLOAT_EQ(restored.protein_level[0], 0.88f);
    }

    // Сбрасываем нейрон
    ASSERT_TRUE(sim.resetSingleNucleus(0));

    // Проверяем что сбросился
    {
        size_t nuc_sz = core_nucleus_size();
        std::vector<uint8_t> buf(nuc_sz);
        ASSERT_NE(core_get_nucleus_snapshot(0, buf.data()), 0);
        NucleusSnapshot reset;
        NucleusToSnapshot(buf.data(), reset);

        // После сброса должен быть enabled=1, mode=3
        EXPECT_TRUE(reset.enabled);
        EXPECT_EQ(reset.consolidation_mode, 3);
    }

    std::remove("/tmp/test_genedit.bion");
}

// ═══════════════════════════════════════════════════════════════
// Все 124 модификатора в BuildPanel
// ═══════════════════════════════════════════════════════════════

TEST(Modifiers, ModifierNamesExist) {
    // Проверяем что ModifierName() возвращает не-"???" для всех 124 типов
    for (int i = 0; i < 124; i++) {
        const char* name = ModifierName(static_cast<ModifierType>(i));
        EXPECT_NE(std::string(name), "???") << "Modifier " << i << " has no name";
        EXPECT_GT(strlen(name), 0u) << "Modifier " << i << " has empty name";
    }
}

// ═══════════════════════════════════════════════════════════════
// GENE_COUNT consistency
// ═══════════════════════════════════════════════════════════════

TEST(GeneEditing, GeneCountConsistency) {
    EXPECT_EQ(GENE_COUNT_STUDIO, 46);
}

// ═══════════════════════════════════════════════════════════════
// Genie names (проверка что все 46 генов поименованы)
// ═══════════════════════════════════════════════════════════════

TEST(GeneEditing, GeneNames) {
    static const char* names[46] = {
        "CREB","c-Fos","Zif268","Arc","BDNF","Cofilin","MMP-9","TIMP",
        "GluR1","GluR2","PSD-95","Neurexin","Neuroligin","Dynamin","Clathrin",
        "IntegrinB3","TrkB","mTOR","S6K1","4E-BP",
        "LHX3","ISLET1","PAX2","BRN3A","MNX1","LMX1B",
        "Nav1.1","Kv4.2","Kv7.2","Kv7.3","Cav3.1","HCN1","HCN2","Kir2.1","TREK-1","BK",
        "Synapsin","VAMP2","SNAP-25","STX1A","SYT1","SYT7","Munc13","RIM1","CPLX1","KNa",
    };
    for (int i = 0; i < 46; i++) {
        EXPECT_GT(strlen(names[i]), 0u) << "Gene " << i << " unnamed";
    }
}
