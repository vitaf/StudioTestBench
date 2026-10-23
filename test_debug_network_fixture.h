// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_debug_network_fixture.h — общий fixture для тестов CPG-сети
// Гарантирует очистку статических буферов между тестами в suite-режиме

#pragma once
#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/simulation_core_api.h"

struct DebugNetworkTest : public ::testing::Test {
    void SetUp() override {
        core_clear_stimuli();  // чистка статических буферов стимулов ПЕРЕД каждым тестом
    }
    void TearDown() override {
        core_clear_stimuli();  // и ПОСЛЕ каждого теста
    }
};
