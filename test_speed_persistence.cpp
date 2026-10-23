// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_speed_persistence.cpp — тесты сохранения скорости симуляции в settings

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include "../BioNeuronStudio/src/settings.h"

static const char* TEST_SETTINGS = "/tmp/test_speed_settings.bin";

TEST(SpeedPersistence, SaveAndRestoreSpeed) {
    remove(TEST_SETTINGS);
    {
        SettingsManager s;
        // По умолчанию должен быть KEY_SIM_SPEED
        int def = s.get_int(SettingsManager::KEY_SIM_SPEED, 5);
        EXPECT_EQ(def, 5) << "Скорость по умолчанию: режим 5 (1 тик/5 сек)";

        // Сохраняем скорость 3 (1 тик/сек)
        s.set_int(SettingsManager::KEY_SIM_SPEED, 3);
        EXPECT_TRUE(s.save(TEST_SETTINGS));
    }
    {
        // Перезагружаем
        SettingsManager s;
        EXPECT_TRUE(s.load(TEST_SETTINGS));
        int restored = s.get_int(SettingsManager::KEY_SIM_SPEED, 5);
        EXPECT_EQ(restored, 3) << "Скорость должна сохраниться между сессиями";
    }
    remove(TEST_SETTINGS);
}

TEST(SpeedPersistence, AllSpeedModesPersist) {
    int modes[] = {0, 1, 2, 3, 4, 5};
    for (int mode : modes) {
        remove(TEST_SETTINGS);
        {
            SettingsManager s;
            s.set_int(SettingsManager::KEY_SIM_SPEED, mode);
            EXPECT_TRUE(s.save(TEST_SETTINGS));
        }
        {
            SettingsManager s;
            EXPECT_TRUE(s.load(TEST_SETTINGS));
            int restored = s.get_int(SettingsManager::KEY_SIM_SPEED, -1);
            EXPECT_EQ(restored, mode) << "Режим " << mode << " не сохранился";
        }
    }
    remove(TEST_SETTINGS);
}

TEST(SpeedPersistence, KeyExists) {
    // KEY_SIM_SPEED должен быть определён
    const char* key = SettingsManager::KEY_SIM_SPEED;
    ASSERT_NE(key, nullptr);
    ASSERT_GT(strlen(key), 0u);
}
