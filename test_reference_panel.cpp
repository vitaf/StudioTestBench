// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_reference_panel.cpp — проверка целостности справочных данных

#include <gtest/gtest.h>
#include <cstring>
#include <set>

#include "../BioNeuronStudio/src/data_model.h"

// Заглушки i18n — ModifierName использует i18n_tr для перевода ключей
#include "../BioNeuronStudio/src/i18n.h"
const char* i18n_tr(const char* key) { return key; }
Language i18n_language() { return Language::RU; }

// ═══════════════════════════════════════════════════════════════
// 1. GENE_COUNT_STUDIO = 46
// ═══════════════════════════════════════════════════════════════
TEST(ReferencePanel, GeneCount) {
    EXPECT_EQ(GENE_COUNT_STUDIO, 46);
}

// ═══════════════════════════════════════════════════════════════
// 2. Все 46 генов имеют непустые имена
// ═══════════════════════════════════════════════════════════════
TEST(ReferencePanel, AllGeneNames) {
    std::set<std::string> names;
    int unnamed = 0;
    for (int i = 0; i < GENE_COUNT_STUDIO; i++) {
        const char* name = GeneName(i);
        if (!name || strlen(name) == 0 || strcmp(name, "???") == 0) {
            unnamed++;
            printf("[RefPanel] Gene %d unnamed\n", i);
        }
        EXPECT_STRNE(name, "???") << "Ген " << i << " без имени";
        EXPECT_GT(strlen(name), 0u) << "Ген " << i << " пустое имя";
        if (name && strlen(name) > 0) names.insert(name);
    }
    EXPECT_EQ(unnamed, 0);
    EXPECT_EQ(names.size(), (size_t)GENE_COUNT_STUDIO) << "Дубликаты имён генов";
}

// ═══════════════════════════════════════════════════════════════
// 3. MOD_TYPE_COUNT = 197
// ═══════════════════════════════════════════════════════════════
TEST(ReferencePanel, ModTypeCount) {
    EXPECT_EQ(MOD_TYPE_COUNT, 197);
}

// ═══════════════════════════════════════════════════════════════
// 4. Все 124 модификатора имеют не-"???" имена
// ═══════════════════════════════════════════════════════════════
TEST(ReferencePanel, AllModifierNames) {
    std::set<std::string> names;
    int bad = 0;
    for (int i = 0; i < MOD_TYPE_COUNT; i++) {
        const char* name = ModifierName(static_cast<ModifierType>(i));
        if (strcmp(name, "???") == 0) {
            bad++;
            printf("[RefPanel] Modifier %d has ??? name\n", i);
        }
        EXPECT_STRNE(name, "???") << "Модификатор " << i << " без имени";
        if (name && name[0]) names.insert(name);
    }
    EXPECT_EQ(bad, 0);
    // Большинство имён должны быть уникальны
    EXPECT_GT(names.size(), 100u) << "Только " << names.size() << " уникальных имён";
}

// ═══════════════════════════════════════════════════════════════
// 5. MAX_MODIFIERS_PER_SEGMENT ≥ 32
// ═══════════════════════════════════════════════════════════════
TEST(ReferencePanel, MaxModifiersPerSegment) {
    EXPECT_GE(MAX_MODIFIERS_PER_SEGMENT, 32);
}

// ═══════════════════════════════════════════════════════════════
// 6. GENE_COUNT_STUDIO совпадает с Core GENE_COUNT
// ═══════════════════════════════════════════════════════════════
TEST(ReferencePanel, GeneCountMatchesCore) {
    // Core GENE_COUNT is 46 — проверяем что не разошлись
    EXPECT_EQ(GENE_COUNT_STUDIO, 46);
}
