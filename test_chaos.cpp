// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_chaos.cpp — тесты отказоустойчивости: double start,
// stop без start, load во время симуляции, пустая сеть.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

static void make_minimal_network() {
    auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    p.rb_per_hemisegment = 1; p.din_per_hemisegment = 1;
    p.cin_per_hemisegment = 1; p.mn_per_hemisegment = 1;
    p.ain_per_hemisegment = 0; p.dlc_count = 0;
    p.enable_hunger_sensor = p.enable_light_sensor = false;
    p.enable_stdp = p.enable_consolidation = false;
    auto m = TadpoleDevelop(p).generate_connectome();
    SaveNetwork(m, "/tmp/chaos_network.bion");
}

// ═══════════════════════════════════════════════════════════════════

TEST(Chaos, SurvivesDoubleStart) {
    make_minimal_network();
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/chaos_network.bion"));

    sim.start();
    // Второй start не должен крашить (должен быть no-op или перезапуск)
    sim.start();
    usleep(100000);  // даём немного поработать
    sim.stop();
    SUCCEED();
}

TEST(Chaos, SurvivesStopWithoutStart) {
    make_minimal_network();
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/chaos_network.bion"));

    // stop() без start() не должен крашить
    sim.stop();
    SUCCEED();
}

TEST(Chaos, SurvivesLoadWhileRunning) {
    make_minimal_network();
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/chaos_network.bion"));

    sim.start();
    // loadNetwork во время симуляции: либо перезагружает, либо отвергает
    // Главное — не крашит
    sim.loadNetwork("/tmp/chaos_network.bion");
    usleep(50000);
    sim.stop();
    SUCCEED();
}

TEST(Chaos, SurvivesEmptyNetwork) {
    // Создаём минимальный .bion вручную (без генерации)
    // Сохраняем сеть из 0 нейронов
    NetworkModel empty;
    SaveNetwork(empty, "/tmp/chaos_empty.bion");

    SimulationBridge sim;
    // Загрузка пустой сети: должна вернуть false или true, но не крашнуть
    sim.loadNetwork("/tmp/chaos_empty.bion");
    SUCCEED();
}

TEST(Chaos, SurvivesStartStopStart) {
    make_minimal_network();
    SimulationBridge sim;
    ASSERT_TRUE(sim.loadNetwork("/tmp/chaos_network.bion"));

    sim.start();
    usleep(50000);
    sim.stop();

    // Перезапуск после остановки
    sim.start();
    usleep(50000);
    sim.stop();
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════════════
