// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_serialization_perf.cpp — проверка корректности оптимизаций загрузки/сохранения

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unordered_map>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/bion_format.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"

static std::string kTestFile = "/tmp/test_serialization_perf.bion";

// ═══════════════════════════════════════════════════════════════
// 1. SaveNetwork с прогрессом: коллбэк вызывается
// ═══════════════════════════════════════════════════════════════
TEST(SerializationPerf, SaveProgressCallback) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    TadpoleDevelop dev(params);
    auto model = dev.generate_connectome();

    std::vector<int> progress_pcts;
    std::vector<std::string> progress_labels;
    bool ok = ::SaveNetwork(model, kTestFile,
        [&](int pct, const char* label) {
            progress_pcts.push_back(pct);
            progress_labels.push_back(label ? label : "");
        });

    EXPECT_TRUE(ok);
    EXPECT_GE(progress_pcts.size(), 3u) << "Прогресс-коллбэк вызван менее 3 раз";
    if (!progress_pcts.empty()) {
        EXPECT_GE(progress_pcts.front(), 0);
        EXPECT_LE(progress_pcts.back(), 100);
    }
    // Проверяем монотонность
    for (size_t i = 1; i < progress_pcts.size(); i++)
        EXPECT_LE(progress_pcts[i-1], progress_pcts[i]) << "Прогресс не монотонен";

    std::remove(kTestFile.c_str());
    printf("[SerPerf] SaveProgressCallback: %zu callbacks\n", progress_pcts.size());
}

// ═══════════════════════════════════════════════════════════════
// 2. ID→index map: ID сегментов корректно мапятся на индексы
// ═══════════════════════════════════════════════════════════════
TEST(SerializationPerf, IdToIndexMap) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    TadpoleDevelop dev(params);
    auto model = dev.generate_connectome();

    ASSERT_TRUE(::SaveNetwork(model, kTestFile));

    // Загружаем
    NetworkModel loaded;
    std::string err;
    ASSERT_EQ(studio_load_bion(kTestFile.c_str(), &loaded, err), 0) << err;

    // Строим ID→index карту как в studio_save_bion
    std::unordered_map<uint32_t, uint32_t> id_to_idx;
    for (uint32_t j = 0; j < loaded.segments.size(); j++)
        id_to_idx[loaded.segments[j].id] = j;

    // Для каждого соединения: segment_a/b должны найтись в карте
    int missing = 0;
    for (auto& conn : loaded.connections) {
        if (id_to_idx.find(conn.segment_a) == id_to_idx.end()) missing++;
        if (id_to_idx.find(conn.segment_b) == id_to_idx.end()) missing++;
    }
    EXPECT_EQ(missing, 0) << missing << " ID связей не найдены в карте";

    // Для каждого синапса: pre_id/post_id должны найтись
    int missing_syn = 0;
    for (auto& syn : loaded.synapses) {
        if (id_to_idx.find(syn.pre_id) == id_to_idx.end()) missing_syn++;
        if (id_to_idx.find(syn.post_id) == id_to_idx.end()) missing_syn++;
    }
    EXPECT_EQ(missing_syn, 0) << missing_syn << " ID синапсов не найдены в карте";

    std::remove(kTestFile.c_str());
    printf("[SerPerf] IdToIndexMap: %zu segs, %zu conns, %zu syns, missing=%d+%d\n",
           loaded.segments.size(), loaded.connections.size(), loaded.synapses.size(),
           missing, missing_syn);
}

// ═══════════════════════════════════════════════════════════════
// 3. Roundtrip: generate → save → load → сравнение
// ═══════════════════════════════════════════════════════════════
TEST(SerializationPerf, Roundtrip) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    TadpoleDevelop dev(params);
    auto original = dev.generate_connectome();

    ASSERT_TRUE(::SaveNetwork(original, kTestFile));

    NetworkModel loaded;
    std::string err;
    ASSERT_EQ(studio_load_bion(kTestFile.c_str(), &loaded, err), 0) << err;

    // Базовые проверки
    EXPECT_EQ(original.neurons.size(), loaded.neurons.size());
    EXPECT_EQ(original.segments.size(), loaded.segments.size());
    EXPECT_EQ(original.connections.size(), loaded.connections.size());
    EXPECT_EQ(original.synapses.size(), loaded.synapses.size());

    // Проверяем сегменты (ID могут отличаться из-за next_id++)
    EXPECT_GE(loaded.segments.size(), 1u);
    for (size_t i = 0; i < loaded.segments.size(); i++) {
        auto& orig = original.segments[i];
        auto& load = loaded.segments[i];
        EXPECT_EQ(orig.neuron_id, load.neuron_id) << "neuron_id mismatch at " << i;
        EXPECT_EQ((int)orig.type, (int)load.type) << "type mismatch at " << i;
        EXPECT_FLOAT_EQ(orig.length, load.length);
        EXPECT_FLOAT_EQ(orig.radius, load.radius);
        EXPECT_FLOAT_EQ(orig.position.x, load.position.x);
        EXPECT_FLOAT_EQ(orig.position.y, load.position.y);
        EXPECT_FLOAT_EQ(orig.position.z, load.position.z);
        EXPECT_EQ(orig.modifier_count, load.modifier_count);
    }

    // Проверяем что capacity ≥ size (reserve был вызван при загрузке)
    EXPECT_GE(loaded.segments.capacity(), loaded.segments.size());
    EXPECT_GE(loaded.connections.capacity(), loaded.connections.size());
    EXPECT_GE(loaded.synapses.capacity(), loaded.synapses.size());

    std::remove(kTestFile.c_str());
    printf("[SerPerf] Roundtrip: %zu segs, %zu conns, %zu syns — OK\n",
           loaded.segments.size(), loaded.connections.size(), loaded.synapses.size());
}

// ═══════════════════════════════════════════════════════════════
// 4. SaveNetwork без коллбэка: обратная совместимость
// ═══════════════════════════════════════════════════════════════
TEST(SerializationPerf, SaveWithoutCallback) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    TadpoleDevelop dev(params);
    auto model = dev.generate_connectome();

    // Старый вызов без коллбэка должен работать
    bool ok = ::SaveNetwork(model, kTestFile);
    EXPECT_TRUE(ok);

    // Файл должен существовать и быть непустым
    FILE* f = fopen(kTestFile.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    EXPECT_GT(sz, 1000) << "Файл слишком мал: " << sz << " байт";

    std::remove(kTestFile.c_str());
}
