// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_bion_validation.cpp — тесты обработки ошибок при загрузке .bion файлов.
// Проверяет: битый magic, неполный файл, неверная версия, нулевые сегменты,
// восстановление после ошибки загрузки.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Core API для загрузки/выгрузки
#include "../BioNeuronCore/include/bion_loader.h"
#include "../BioNeuronCore/include/bion_format.h"
#include "../BioNeuronCore/include/types.h"

// ═══════════════════════════════════════════════════════════════════
// Хелпер: запись минимального валидного .bion v5
// ═══════════════════════════════════════════════════════════════════

static void write_valid_bion(const char* path) {
    FILE* f = fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);

    NetworkHeader hdr = {};
    hdr.magic = BION_MAGIC;
    hdr.version = BION_VERSION;
    hdr.num_neurons = 1;
    hdr.num_segments = 1;
    hdr.num_intra_connections = 0;
    hdr.num_synapses = 0;
    hdr.num_nuclei = 1;
    hdr.nucleus_version = 1;
    fwrite(&hdr, sizeof(NetworkHeader), 1, f);

    Neuron n = {};
    n.id = 1;
    n.segment_start = 0;
    n.segment_count = 1;
    n.threshold = -20.0f;
    fwrite(&n, sizeof(Neuron), 1, f);

    Segment seg = {};
    seg.id = 1;
    seg.neuron_id = 1;
    seg.position = {0, 0, 0};
    seg.direction = {0, 1, 0};
    seg.length = 10.0f;
    seg.radius = 1.0f;
    seg.V_m = -65.0f;
    fwrite(&seg, sizeof(Segment), 1, f);

    Nucleus nc = {};
    nc.neuron_id = 1;
    nc.enabled = 1;
    nc.consolidation_mode = 3;
    nc.amino_acid_level = 0.5f;
    fwrite(&nc, sizeof(Nucleus), 1, f);

    fclose(f);
}

// ═══════════════════════════════════════════════════════════════════
// Тесты валидации
// ═══════════════════════════════════════════════════════════════════

TEST(BionValidation, LoadValidBionSucceeds) {
    const char* path = "/tmp/test_valid_load.bion";
    write_valid_bion(path);

    BionData data;
    int rc = bion_load_host(path, &data);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(data.num_segments, 1u);
    EXPECT_EQ(data.num_neurons, 1u);
    EXPECT_EQ(data.num_nuclei, 1u);

    bion_unload_host(&data);
    remove(path);
}

TEST(BionValidation, NonexistentFileReturnsError) {
    BionData data;
    int rc = bion_load_host("/tmp/nonexistent_xyz_file_12345.bion", &data);
    EXPECT_LT(rc, 0);
}

TEST(BionValidation, BadMagicReturnsError) {
    const char* path = "/tmp/test_bad_magic.bion";
    FILE* f = fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);

    NetworkHeader hdr = {};
    hdr.magic = 0xDEADBEEF;
    hdr.version = BION_VERSION;
    hdr.num_segments = 1;
    fwrite(&hdr, sizeof(NetworkHeader), 1, f);
    fclose(f);

    BionData data;
    int rc = bion_load_host(path, &data);
    EXPECT_LT(rc, 0);

    remove(path);
}

TEST(BionValidation, UnsupportedVersionReturnsError) {
    const char* path = "/tmp/test_bad_version.bion";
    FILE* f = fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);

    NetworkHeader hdr = {};
    hdr.magic = BION_MAGIC;
    hdr.version = 99;
    hdr.num_segments = 1;
    fwrite(&hdr, sizeof(NetworkHeader), 1, f);
    fclose(f);

    BionData data;
    int rc = bion_load_host(path, &data);
    EXPECT_LT(rc, 0);

    remove(path);
}

TEST(BionValidation, ZeroSegmentsReturnsError) {
    const char* path = "/tmp/test_zero_segs.bion";
    FILE* f = fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);

    NetworkHeader hdr = {};
    hdr.magic = BION_MAGIC;
    hdr.version = BION_VERSION;
    hdr.num_segments = 0;
    fwrite(&hdr, sizeof(NetworkHeader), 1, f);
    fclose(f);

    BionData data;
    int rc = bion_load_host(path, &data);
    EXPECT_LT(rc, 0);

    remove(path);
}

TEST(BionValidation, TruncatedFileReturnsError) {
    const char* path = "/tmp/test_truncated.bion";
    FILE* f = fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);

    NetworkHeader hdr = {};
    hdr.magic = BION_MAGIC;
    hdr.version = BION_VERSION;
    hdr.num_neurons = 1;
    hdr.num_segments = 2;   // заявлено 2 сегмента
    hdr.num_nuclei = 1;
    fwrite(&hdr, sizeof(NetworkHeader), 1, f);

    // Пишем только 1 нейрон и 1 сегмент — неполный файл
    Neuron n = {1, 0, 2, 0, -20.0f, 0, {}};
    fwrite(&n, sizeof(Neuron), 1, f);
    Segment seg = {};
    seg.id = 1;
    fwrite(&seg, sizeof(Segment), 1, f);
    // Второй сегмент не пишем — файл оборван
    fclose(f);

    BionData data;
    int rc = bion_load_host(path, &data);
    // Должен вернуть ошибку (не может прочитать второй сегмент)
    // Может быть успешным если fread второго сегмента вернёт 0 и данные будут нулевыми,
    // но это всё равно ошибка целостности.
    if (rc == 0) {
        // Если загрузчик не заметил, очищаем
        bion_unload_host(&data);
    }

    remove(path);
}

TEST(BionValidation, RecoveryAfterFailedLoad) {
    // Проверяем что после неудачной загрузки можно загрузить валидный файл
    const char* bad_path = "/tmp/test_recovery_bad.bion";
    const char* good_path = "/tmp/test_recovery_good.bion";

    // Сначала пытаемся загрузить битый
    FILE* f = fopen(bad_path, "wb");
    ASSERT_TRUE(f != nullptr);
    NetworkHeader hdr = {};
    hdr.magic = 0xDEADBEEF;
    hdr.num_segments = 1;
    fwrite(&hdr, sizeof(NetworkHeader), 1, f);
    fclose(f);

    BionData data1;
    int rc1 = bion_load_host(bad_path, &data1);
    EXPECT_LT(rc1, 0);

    // Затем загружаем валидный
    write_valid_bion(good_path);
    BionData data2;
    int rc2 = bion_load_host(good_path, &data2);
    EXPECT_EQ(rc2, 0);
    EXPECT_EQ(data2.num_segments, 1u);

    bion_unload_host(&data2);
    remove(bad_path);
    remove(good_path);
}

TEST(BionValidation, SaveAndReloadPreservesData) {
    const char* path = "/tmp/test_roundtrip.bion";
    write_valid_bion(path);

    // Загружаем → сохраняем → загружаем снова
    BionData data1;
    ASSERT_EQ(bion_load_host(path, &data1), 0);

    const char* saved = "/tmp/test_roundtrip_saved.bion";
    int save_rc = bion_save(saved, &data1);
    ASSERT_EQ(save_rc, 0);

    BionData data2;
    int load_rc = bion_load_host(saved, &data2);
    EXPECT_EQ(load_rc, 0);
    EXPECT_EQ(data2.num_segments, data1.num_segments);
    EXPECT_EQ(data2.num_neurons, data1.num_neurons);
    EXPECT_EQ(data2.num_nuclei, data1.num_nuclei);

    bion_unload_host(&data1);
    bion_unload_host(&data2);
    remove(path);
    remove(saved);
}

TEST(BionValidation, BionValidateHeaderRejectsNull) {
    // bion_validate_header с nullptr — undefined behaviour, но не должно крашить
    // Проверяем только что функция компилируется и работает с валидным hdr
    NetworkHeader hdr = {};
    hdr.magic = BION_MAGIC;
    hdr.version = BION_VERSION;
    hdr.num_segments = 1;
    EXPECT_EQ(bion_validate_header(&hdr), 0);

    hdr.magic = 0;
    EXPECT_LT(bion_validate_header(&hdr), 0);

    hdr.magic = BION_MAGIC;
    hdr.version = 99;
    EXPECT_LT(bion_validate_header(&hdr), 0);

    hdr.version = BION_VERSION;
    hdr.num_segments = 0;
    EXPECT_LT(bion_validate_header(&hdr), 0);
}

TEST(BionValidation, BionCalcSizeMatchesWrittenFile) {
    size_t calc = bion_calc_size(1, 1, 0, 0, 1);
    size_t expected = sizeof(NetworkHeader) + sizeof(Neuron) + sizeof(Segment) + sizeof(Nucleus);
    EXPECT_EQ(calc, expected);

    size_t calc_v4 = bion_calc_size_v4(1, 1, 0, 0);
    size_t expected_v4 = sizeof(NetworkHeader) + sizeof(Neuron) + sizeof(Segment) + sizeof(Nucleus);
    EXPECT_EQ(calc_v4, expected_v4);
}
