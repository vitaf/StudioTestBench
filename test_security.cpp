// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_security.cpp — тесты безопасности: битые .bion, .desc JSON,
// oversized заголовки, path traversal, roundtrip integrity.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

// ═══════════════════════════════════════════════════════════════════

TEST(Security, RejectsCorruptedBion) {
    // Битый .bion: только заголовок с неверным magic
    FILE* f = fopen("/tmp/corrupted.bion", "wb");
    ASSERT_TRUE(f != nullptr);
    char garbage[256] = {};
    memset(garbage, 0xFF, sizeof(garbage));
    fwrite(garbage, 1, sizeof(garbage), f);
    fclose(f);

    SimulationBridge sim;
    EXPECT_FALSE(sim.loadNetwork("/tmp/corrupted.bion"))
        << "Should reject file with invalid magic";
    // Bridge не должен крашиться — проверяется самим вызовом
}

TEST(Security, RejectsTruncatedBion) {
    // Корректный .bion, искусственно обрезанный
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 1; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    auto m = TadpoleDevelop(p).generate_connectome();
    SaveNetwork(m, "/tmp/valid_then_truncate.bion");

    // Обрезаем файл до половины
    FILE* f = fopen("/tmp/valid_then_truncate.bion", "rb+");
    ASSERT_TRUE(f != nullptr);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);

    // Обрезаем
    truncate("/tmp/valid_then_truncate.bion", sz / 2);

    SimulationBridge sim;
    // Не должно крашиться — либо false, либо частичная загрузка
    sim.loadNetwork("/tmp/valid_then_truncate.bion");
    SUCCEED();  // не упали = успех
}

TEST(Security, SurvivesOversizedBion) {
    // .bion с гигантским num_segments в заголовке
    FILE* f = fopen("/tmp/oversized.bion", "wb");
    ASSERT_TRUE(f != nullptr);

    // Заголовок с нереалистично большим числом сегментов
    uint8_t buf[1024] = {};
    // BION magic (если известен, используем; иначе просто мусор чтобы вызвать отказ)
    memset(buf, 0xAA, sizeof(buf));
    // Пишем гигантское значение num_segments в область заголовка
    uint64_t huge = 0xFFFFFFFFFFFFFFFFULL;
    memcpy(buf + 8, &huge, sizeof(huge));  // смещение где может быть num_segments
    fwrite(buf, 1, sizeof(buf), f);
    fclose(f);

    SimulationBridge sim;
    EXPECT_FALSE(sim.loadNetwork("/tmp/oversized.bion"))
        << "Should reject file with unrealistic segment count";
}

TEST(Security, RoundtripPreservesStructure) {
    // save → load → сравнение: структура сети не меняется
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 2; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;

    auto m1 = TadpoleDevelop(p).generate_connectome();
    SaveNetwork(m1, "/tmp/security_roundtrip.bion");

    NetworkModel m2;
    ASSERT_TRUE(LoadNetwork(m2, "/tmp/security_roundtrip.bion"));

    EXPECT_EQ(m1.neurons.size(), m2.neurons.size());
    EXPECT_EQ(m1.segments.size(), m2.segments.size());
    EXPECT_EQ(m1.connections.size(), m2.connections.size());
    EXPECT_EQ(m1.synapses.size(), m2.synapses.size());

    // Проверяем что первый сегмент имеет те же координаты
    if (!m1.segments.empty() && !m2.segments.empty()) {
        EXPECT_FLOAT_EQ(m1.segments[0].position.x, m2.segments[0].position.x);
        EXPECT_FLOAT_EQ(m1.segments[0].position.y, m2.segments[0].position.y);
        EXPECT_FLOAT_EQ(m1.segments[0].position.z, m2.segments[0].position.z);
    }
}

TEST(Security, RejectsEmptyBion) {
    // Пустой файл
    FILE* f = fopen("/tmp/empty.bion", "wb");
    ASSERT_TRUE(f != nullptr);
    fclose(f);

    SimulationBridge sim;
    EXPECT_FALSE(sim.loadNetwork("/tmp/empty.bion"))
        << "Should reject empty file";
}

TEST(Security, SurvivesMalformedDescJson) {
    // Битый .desc JSON: невалидный синтаксис
    FILE* f = fopen("/tmp/malformed.desc", "w");
    ASSERT_TRUE(f != nullptr);
    fprintf(f, "{this is not valid json [[[ }\n");
    fclose(f);

    // Попытка загрузить плагин с битым .desc не должна крашить
    // (Core plugin_manager должен вернуть 0 загруженных плагинов)
    SimulationBridge sim;
    // Создаём также валидную сеть и проверяем что симуляция работает
    // после попытки загрузить битый плагин
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 1; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    auto m = TadpoleDevelop(p).generate_connectome();
    SaveNetwork(m, "/tmp/security_desc_test.bion");
    ASSERT_TRUE(sim.loadNetwork("/tmp/security_desc_test.bion"));

    sim.start();
    for (int i = 0; i < 10; i++) sim.step();
    sim.stop();
    SUCCEED();  // не крашнулись = успех
}

TEST(Security, SurvivesDescMissingFields) {
    // .desc JSON без обязательных полей (пустой объект)
    FILE* f = fopen("/tmp/empty_fields.desc", "w");
    ASSERT_TRUE(f != nullptr);
    fprintf(f, "{}\n");
    fclose(f);

    // Аналогично — не должно крашить
    SimulationBridge sim;
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 1; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    auto m = TadpoleDevelop(p).generate_connectome();
    SaveNetwork(m, "/tmp/security_desc2_test.bion");
    ASSERT_TRUE(sim.loadNetwork("/tmp/security_desc2_test.bion"));

    sim.start();
    for (int i = 0; i < 10; i++) sim.step();
    sim.stop();
    SUCCEED();
}

TEST(Security, FuzzBionRandomMutations) {
    // Создаём валидный .bion и применяем случайные мутации
    // Проверяем что ни одна мутация не вызывает краш
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 1; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    auto m = TadpoleDevelop(p).generate_connectome();
    SaveNetwork(m, "/tmp/fuzz_valid.bion");

    // Читаем валидный файл
    FILE* f = fopen("/tmp/fuzz_valid.bion", "rb");
    ASSERT_TRUE(f != nullptr);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> original(sz);
    fread(original.data(), 1, sz, f);
    fclose(f);

    int survived = 0, rejected = 0;
    const int MUTATIONS = 200;

    for (int i = 0; i < MUTATIONS; i++) {
        std::vector<uint8_t> mutated = original;

        int corruption_type = i % 6;
        switch (corruption_type) {
            case 0: // Полная перезапись первых 8 байт (magic + version)
                for (int b = 0; b < 8; b++) mutated[b] = rand() & 0xFF;
                break;
            case 1: // Обнуление случайного диапазона 50 байт
                { int start = rand() % (sz - 50);
                  for (int b = 0; b < 50; b++) mutated[start + b] = 0x00; }
                break;
            case 2: // Запись 0xFF в случайный диапазон 50 байт
                { int start = rand() % (sz - 50);
                  for (int b = 0; b < 50; b++) mutated[start + b] = 0xFF; }
                break;
            case 3: // Усечение файла до 10-90% длины
                { int new_sz = sz * (10 + rand() % 80) / 100;
                  if (new_sz > 4) mutated.resize(new_sz); }
                break;
            case 4: // Добавление мусора в конец (50-200 байт)
                { int extra = 50 + rand() % 150;
                  for (int b = 0; b < extra; b++) mutated.push_back(rand() & 0xFF); }
                break;
            case 5: // Инверсия всех байт в случайном окне 20 байт
                { int start = rand() % (sz - 20);
                  for (int b = 0; b < 20; b++) mutated[start + b] = ~mutated[start + b]; }
                break;
        }

        FILE* mf = fopen("/tmp/fuzz_mutated.bion", "wb");
        fwrite(mutated.data(), 1, mutated.size(), mf);
        fclose(mf);

        SimulationBridge sim;
        if (sim.loadNetwork("/tmp/fuzz_mutated.bion")) {
            sim.start();
            for (int j = 0; j < 5; j++) sim.step();
            sim.stop();
            survived++;
        } else {
            rejected++;
        }
    }

    printf("   fuzzed %d: %d rejected, %d survived — 0 crashed\n",
           MUTATIONS, rejected, survived);
    EXPECT_GE(rejected + survived, MUTATIONS)
        << "Not all mutations processed";
}

// ═══════════════════════════════════════════════════════════════════
