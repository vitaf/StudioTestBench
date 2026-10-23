// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gene_pipeline.cpp — тесты сквозного пайплайна генов Core→Studio
// Проверяют: NucleusToSnapshot, соответствие размера, получение через opaque API

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "../BioNeuronStudio/src/nucleus_converter.h"
#include "../BioNeuronStudio/src/simulation_core_api.h"

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 1: NucleusToSnapshot корректно копирует все поля
// Используем сырые байты (без Core-заголовков, чтобы избежать конфликта типов)
// ═══════════════════════════════════════════════════════════════════════════
TEST(GenePipeline, NucleusToSnapshotRoundtrip) {
    // Макет Core Nucleus: 412 байт, смещения как в types.h
    uint8_t src[412];
    memset(src, 0, sizeof(src));

    *(uint32_t*)(src + 0) = 42;      // neuron_id
    *(uint8_t*)(src + 4)  = 1;       // enabled
    *(uint8_t*)(src + 5)  = 2;       // consolidation_mode
    *(uint32_t*)(src + 8)  = 1000;   // consolidation_interval_ticks
    *(uint32_t*)(src + 12) = 555;    // last_consolidation_tick

    for (int i = 0; i < 46; i++) {
        *(float*)(src + 16 + i * 4)  = 0.1f * (float)(i + 1);   // gene_activity
        *(float*)(src + 200 + i * 4) = 0.05f * (float)(i + 1);  // protein_level
    }
    *(float*)(src + 384) = 0.75f;   // protein_synthesis_rate
    *(float*)(src + 388) = 0.3f;    // pcreb_level
    *(float*)(src + 392) = 0.4f;    // erk_level
    *(float*)(src + 396) = 0.5f;    // nfkb_level
    *(float*)(src + 400) = 0.6f;    // mtorc1_level
    *(float*)(src + 404) = 0.7f;    // amino_acid_level

    NucleusSnapshot ns;
    NucleusToSnapshot(src, ns);

    EXPECT_EQ(ns.neuron_id, 42u);
    EXPECT_TRUE(ns.enabled);
    EXPECT_EQ(ns.consolidation_mode, 2);
    EXPECT_EQ(ns.consolidation_interval, 1000u);
    EXPECT_EQ(ns.last_consolidation_tick, 555u);

    for (int i = 0; i < 46; i++) {
        EXPECT_NEAR(ns.gene_activity[i], 0.1f * (float)(i + 1), 1e-6f)
            << "gene_activity[" << i << "]";
        EXPECT_NEAR(ns.protein_level[i], 0.05f * (float)(i + 1), 1e-6f)
            << "protein_level[" << i << "]";
    }

    EXPECT_NEAR(ns.protein_synthesis_rate, 0.75f, 1e-6f);
    EXPECT_NEAR(ns.pcreb_level,  0.3f, 1e-6f);
    EXPECT_NEAR(ns.erk_level,    0.4f, 1e-6f);
    EXPECT_NEAR(ns.nfkb_level,   0.5f, 1e-6f);
    EXPECT_NEAR(ns.mtorc1_level, 0.6f, 1e-6f);
    EXPECT_NEAR(ns.amino_acid_level, 0.7f, 1e-6f);
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 2: core_nucleus_size() соответствует ожидаемому (412 = GENE_COUNT=46)
// ═══════════════════════════════════════════════════════════════════════════
TEST(GenePipeline, NucleusSizeIs412) {
    EXPECT_EQ(core_nucleus_size(), 412u);
    // GENE_COUNT=46 → gene_activity[46]*4 + protein_level[46]*4 = 368
    // + header(16) + signals(24) + reserved(4) = 412
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ 3: core_get_nucleus_snapshot через opaque API после симуляции
// ═══════════════════════════════════════════════════════════════════════════
TEST(GenePipeline, GetNucleusSnapshotViaOpaqueApi) {
    // Интеграционный тест: требует загрузки .bion файла и симуляции.
    // Пропускаем если файл недоступен (CI/headless).
    const char* test_bion = "../../examples/test_single_neuron.bion";
    CoreData data = core_load_bion(test_bion);
    ASSERT_NE(data, nullptr) << "Failed to load " << test_bion;

    uint32_t n_seg = core_num_segments(data);
    uint32_t n_conn = core_num_connections(data);
    uint32_t n_syn = core_num_synapses(data);
    uint32_t n_neurons = core_num_neurons(data);
    ASSERT_GT(n_seg, 0u);
    ASSERT_GT(n_neurons, 0u);

    size_t nuc_size = core_nucleus_size();
    ASSERT_EQ(nuc_size, 412u);
    std::vector<uint8_t> nuclei_buf(n_neurons * nuc_size);
    memset(nuclei_buf.data(), 0, nuclei_buf.size());
    for (uint32_t i = 0; i < n_neurons; i++) {
        uint8_t* nuc = nuclei_buf.data() + i * nuc_size;
        *(uint32_t*)(nuc + 0) = i;
        *(uint8_t*)(nuc + 4)  = 1;
        *(uint8_t*)(nuc + 5)  = 3;
        *(float*)(nuc + 404)  = 0.5f;
    }
    core_init_consolidation(core_neurons_ptr(data), n_neurons,
                             nuclei_buf.data(), n_neurons);
    uint64_t tick = 0;
    core_start_consolidation(core_segments_ptr(data), n_seg,
                              core_synapses_ptr(data), &n_syn,
                              n_syn * 2, &tick);
    uint32_t stim_count = 0, vclamp_count = 0, ramp_count = 0;
    uint32_t ion_inj_count = 0, lig_inj_count = 0;
    for (int t = 0; t < 10; t++) {
        core_run_tick(core_segments_ptr(data), n_seg,
                      core_connections_ptr(data), n_conn,
                      core_synapses_ptr(data), n_syn,
                      0, &tick, 0.001f,
                      nullptr, &stim_count,
                      nullptr, &vclamp_count,
                      nullptr, &ramp_count,
                      nullptr, &ion_inj_count,
                      nullptr, &lig_inj_count, nullptr, nullptr);
    }

    std::vector<uint8_t> buf(nuc_size);
    int ok = core_get_nucleus_snapshot(0, buf.data());
    EXPECT_EQ(ok, 1);

    NucleusSnapshot ns;
    NucleusToSnapshot(buf.data(), ns);
    EXPECT_EQ(ns.neuron_id, 0u);
    EXPECT_TRUE(ns.enabled);
    EXPECT_EQ(ns.consolidation_mode, 3);
    EXPECT_NEAR(ns.amino_acid_level, 0.5f, 1e-4f);

    core_unload_bion(data);
}
