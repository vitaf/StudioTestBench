// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_info_files.cpp — проверка целостности .info файлов (127 модификаторов)

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>

// ── Пути к .info файлам ──────────────────────────────────────────────────
// Ищем .info в build-директории (там CMake копирует с стандартизацией имён)
// и в исходниках (для файлов без дескриптивных суффиксов)
static const char* kInfoDirs[] = {
    "/home/vitaf/workarea/develop/MindCopy/BioNeuronCore/build/plugins/builtin",
    "../BioNeuronCore/build/plugins/builtin",
    "../../BioNeuronCore/build/plugins/builtin",
    "plugins/builtin",
    "../plugins/builtin",
    "../BioNeuronCore/plugins/builtin",
    "../../BioNeuronCore/plugins/builtin",
};
static const char* find_info_dir() {
    for (auto dir : kInfoDirs) {
        char path[256];
        snprintf(path, sizeof(path), "%s/mod_00.info", dir);
        FILE* f = fopen(path, "r");
        if (f) { fclose(f); return dir; }
    }
    return kInfoDirs[0];  // fallback
}

static std::string info_path(int id) {
    static const char* dir = nullptr;
    if (!dir) dir = find_info_dir();
    char buf[256];
    snprintf(buf, sizeof(buf), "%s/mod_%02d.info", dir, id);
    return buf;
}
// Читает файл целиком в строку
static bool read_file(const char* path, std::string& out) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    out.resize(sz);
    fread(&out[0], 1, sz, f);
    fclose(f);
    return true;
}

// Простой парсер: ищет ключ в JSON
static bool json_has_key(const std::string& json, const char* key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    return json.find(search) != std::string::npos;
}
static bool json_has_type_id(const std::string& json) {
    return json.find("\"type_id\"") != std::string::npos ||
           json.find("\"id\"") != std::string::npos;  // некоторые файлы используют "id"
}

// Проверяет что JSON — плоский (не nested с "modifiers" массивом)
static bool is_flat_json(const std::string& json) {
    if (json.empty()) return false;
    return json.find("\"modifiers\"") == std::string::npos;
}

// ═══════════════════════════════════════════════════════════════
// 1. Все 124 .info файла существуют
// ═══════════════════════════════════════════════════════════════
TEST(InfoFiles, AllInfoFilesExist) {
    int missing = 0;
    for (int i = 0; i < 124; i++) {
        std::string path = info_path(i);
        FILE* f = fopen(path.c_str(), "r");
        if (!f) {
            missing++;
            printf("[Info] MISSING: mod_%02d.info\n", i);
        } else {
            fclose(f);
        }
    }
    EXPECT_EQ(missing, 0) << missing << " .info файлов отсутствует";
    if (missing == 0) printf("[Info] All 124 .info files present\n");
}

// ═══════════════════════════════════════════════════════════════
// 2. Все .info файлы — плоский JSON с обязательными полями
// ═══════════════════════════════════════════════════════════════
TEST(InfoFiles, AllFlatJsonWithRequiredFields) {
    int nested = 0, missing_name = 0, missing_func = 0, missing_type = 0;
    for (int i = 0; i < 124; i++) {
        std::string path = info_path(i);
        std::string content;
        if (!read_file(path.c_str(), content)) {
            printf("[Info] CANT_READ: mod_%02d.info\n", i);
            continue;
        }
        if (!is_flat_json(content)) {
            nested++;
            printf("[Info] NESTED: mod_%02d.info\n", i);
        }
        if (!json_has_key(content, "name"))      missing_name++;
        if (!json_has_key(content, "function") &&
            !json_has_key(content, "description")) missing_func++;  // оба варианта
        if (!json_has_type_id(content))           missing_type++;
    }
    EXPECT_EQ(nested, 0) << nested << " .info файлов в nested-формате";
    EXPECT_EQ(missing_name, 0);
    EXPECT_EQ(missing_func, 0);
    EXPECT_EQ(missing_type, 0);
    printf("[Info] FlatJson: %d nested, name=%d func=%d type=%d\n",
           nested, missing_name, missing_func, missing_type);
}

// ═══════════════════════════════════════════════════════════════
// 3. type_id в .info совпадает с именем файла
// ═══════════════════════════════════════════════════════════════
TEST(InfoFiles, TypeIdMatchesFilename) {
    int mismatches = 0;
    for (int i = 0; i < 124; i++) {
        std::string path = info_path(i);
        std::string content;
        if (!read_file(path.c_str(), content)) continue;

        char expected[32];
        snprintf(expected, sizeof(expected), "\"type_id\": %d", i);
        char expected_alt[32];
        snprintf(expected_alt, sizeof(expected_alt), "\"id\": %d", i);
        if (content.find(expected) == std::string::npos &&
            content.find(expected_alt) == std::string::npos) {
            snprintf(expected, sizeof(expected), "\"type_id\":%d", i);
            snprintf(expected_alt, sizeof(expected_alt), "\"id\":%d", i);
            if (content.find(expected) == std::string::npos &&
                content.find(expected_alt) == std::string::npos) {
                mismatches++;
                printf("[Info] TYPE_MISMATCH: mod_%02d.info\n", i);
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " .info с несовпадением type_id";
}

// ═══════════════════════════════════════════════════════════════
// 4. Итого: все 124 .info присутствуют и валидны
// ═══════════════════════════════════════════════════════════════
TEST(InfoFiles, Summary) {
    int exist = 0, flat = 0;
    for (int i = 0; i < 124; i++) {
        std::string path = info_path(i);
        std::string content;
        if (read_file(path.c_str(), content)) exist++;
        if (is_flat_json(content)) flat++;
    }
    EXPECT_EQ(exist, 124);
    EXPECT_EQ(flat, 124);
    printf("[Info] Summary: %d/124 exist, %d/124 flat JSON\n", exist, flat);
}
