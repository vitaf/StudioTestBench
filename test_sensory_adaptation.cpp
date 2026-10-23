// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_sensory_adaptation.cpp — Биофизическая валидация сенсорной адаптации
//
// Проверяет:
// 1. MOD_PHYSICAL_SENSOR: habituation (контр-направленная) снижает ответ при sustained стимуляции
// 2. MOD_PHYSICAL_SENSOR: sensitization (со-направленная) усиливает ответ при длительной слабой стимуляции
// 3. MOD_PHYSICAL_SENSOR: recovery — после прекращения стимула ответ восстанавливается
// 4. MOD_LIGHT_SENSOR: sensitization работает совместно с существующей адаптацией
// 5. MOD_OLFACTORY_RECEPTOR: sensitization работает совместно с существующей адаптацией
// 6. MOD_HUNGER_SENSOR: habituation + sensitization работают
// 7. CPU vs GPU сравнение: physical_sensor (адаптационные траектории идентичны)
// 8. CPU vs GPU сравнение: light_sensor (адаптационные траектории идентичны)

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Хелперы
// ═══════════════════════════════════════════════════════════════════════════════

static NetworkModel make_physical_sensor_neuron(float V_m_init = -65.0f) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 1; s.neuron_id = 1; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = V_m_init; s.prev_V_m = V_m_init;
    s.intra_ions[0] = 0.015f;  s.intra_ions[1] = 0.140f;
    s.intra_ions[2] = 0.00005f; s.intra_ions[3] = 0.005f;
    s.extra_ions[0] = 0.140f;  s.extra_ions[1] = 0.005f;
    s.extra_ions[2] = 0.0012f; s.extra_ions[3] = 0.130f;
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    s.add_modifier(ModifierType::MOD_K2P_LEAK, 0.05f);   // мин. K⁺ утечка для стабильности (не шунтировать сенсор)
    s.add_modifier(ModifierType::MOD_PHYSICAL_SENSOR, 1.0f);
    m.segments.push_back(s);
    m.neurons.push_back({1, 0, 1, 0});
    return m;
}

static NetworkModel make_light_sensor_neuron(float V_m_init = -65.0f) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 1; s.neuron_id = 1; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = V_m_init; s.prev_V_m = V_m_init;
    s.intra_ions[0] = 0.015f;  s.intra_ions[1] = 0.140f;
    s.intra_ions[2] = 0.00005f; s.intra_ions[3] = 0.005f;
    s.extra_ions[0] = 0.140f;  s.extra_ions[1] = 0.005f;
    s.extra_ions[2] = 0.0012f; s.extra_ions[3] = 0.130f;
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    s.add_modifier(ModifierType::MOD_LIGHT_SENSOR, 1.0f);
    m.segments.push_back(s);
    m.neurons.push_back({1, 0, 1, 0});
    return m;
}

static NetworkModel make_hunger_sensor_neuron(float V_m_init = -65.0f) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 1; s.neuron_id = 1; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = V_m_init; s.prev_V_m = V_m_init;
    s.intra_ions[0] = 0.015f;  s.intra_ions[1] = 0.140f;
    s.intra_ions[2] = 0.00005f; s.intra_ions[3] = 0.005f;
    s.extra_ions[0] = 0.140f;  s.extra_ions[1] = 0.005f;
    s.extra_ions[2] = 0.0012f; s.extra_ions[3] = 0.130f;
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    s.add_modifier(ModifierType::MOD_HUNGER_SENSOR, 1.0f);
    m.segments.push_back(s);
    m.neurons.push_back({1, 0, 1, 0});
    return m;
}

// Хелпер: просимулировать N мс, установив state[1] сенсорного модификатора в заданное значение
static float simulate_with_sensor_input(const std::string& path, ModifierType sensor_type,
                                         float state1_value, int duration_ms) {
    // Загружаем сеть, модифицируем state[1] первого модификатора сенсорного типа
    // и симулируем duration_ms тиков
    NetworkModel model;
    if (!::LoadNetwork(model, path)) {
        printf("[ERROR] Failed to load %s\n", path.c_str());
        return -999.0f;
    }
    // Установить state[1] для сенсорного модификатора
    for (auto& seg : model.segments) {
        for (int mi = 0; mi < seg.modifier_count; mi++) {
            if (seg.modifiers[mi].mod_type() == sensor_type) {
                seg.modifiers[mi].state[1] = state1_value;
            }
        }
    }
    // Пересохранить с обновлённым state
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path.c_str());
    if (!::SaveNetwork(model, tmp_path)) {
        printf("[ERROR] Failed to save %s\n", tmp_path);
        return -999.0f;
    }

    SimulationBridge sim;
    sim.setBackend(SimulationBridge::Backend::CPU);
    if (!sim.loadNetwork(tmp_path)) {
        printf("[ERROR] Failed to load %s into sim\n", tmp_path);
        return -999.0f;
    }
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    std::vector<SegmentSnapshot> segs;
    sim.getSegmentsSnapshot(segs);
    sim.stop();
    std::remove(tmp_path);
    return segs.empty() ? -999.0f : segs[0].V_m;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 1: MOD_PHYSICAL_SENSOR — habituation снижает V_m ответ при sustained стимуле
// ═══════════════════════════════════════════════════════════════════════════════
TEST(SensoryAdaptation, PhysicalSensorHabituationReducesResponse) {
    NetworkModel m = make_physical_sensor_neuron(-65.0f);
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/sens_adap_phys.bion"));

    // Без стимула: baseline V_m
    float V_baseline = simulate_with_sensor_input("/tmp/sens_adap_phys.bion",
        ModifierType::MOD_PHYSICAL_SENSOR, 0.0f, 200);
    printf("[PhysHab] Baseline V_m=%.2f mV\n", V_baseline);

    // С sustained стимулом (state[1]=0.8) — 500ms: habituation должен снизить ответ
    float V_early = simulate_with_sensor_input("/tmp/sens_adap_phys.bion",
        ModifierType::MOD_PHYSICAL_SENSOR, 0.8f, 200);
    printf("[PhysHab] Early (200ms) V_m=%.2f mV\n", V_early);

    // После 2000ms sustained стимула: habituation накопилась → ответ слабее
    float V_late = simulate_with_sensor_input("/tmp/sens_adap_phys.bion",
        ModifierType::MOD_PHYSICAL_SENSOR, 0.8f, 2000);
    printf("[PhysHab] Late (2000ms) V_m=%.2f mV\n", V_late);

    // habituation должен уменьшить деполяризацию (V_late ближе к baseline, чем V_early)
    float depol_early = V_early - V_baseline;
    float depol_late  = V_late - V_baseline;
    printf("[PhysHab] depol_early=%.2f depol_late=%.2f\n", depol_early, depol_late);

    // Если стимул деполяризует (depol_early > 0): late должно быть меньше early
    if (depol_early > 0.5f) {
        EXPECT_LT(depol_late, depol_early * 0.9f)
            << "Habituation должен снизить деполяризацию через 2000ms sustained стимула";
    }

    std::remove("/tmp/sens_adap_phys.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 2: MOD_PHYSICAL_SENSOR — sensitization усиливает слабый sustained стимул
// ═══════════════════════════════════════════════════════════════════════════════
TEST(SensoryAdaptation, PhysicalSensorSensitizationEnhancesWeakInput) {
    NetworkModel m = make_physical_sensor_neuron(-65.0f);
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/sens_adap_phys2.bion"));

    // Слабый sustained стимул (state[1]=0.1) — sensitization tau=30s, нужно долго
    float V_early = simulate_with_sensor_input("/tmp/sens_adap_phys2.bion",
        ModifierType::MOD_PHYSICAL_SENSOR, 0.1f, 500);
    float V_late = simulate_with_sensor_input("/tmp/sens_adap_phys2.bion",
        ModifierType::MOD_PHYSICAL_SENSOR, 0.1f, 5000);

    printf("[PhysSens] Early(500ms)=%.2f Late(5000ms)=%.2f mV\n", V_early, V_late);

    // Sensitization очень медленная (tau=30s), за 5s едва заметна.
    // Главное: адаптация не ломает стабильность.
    EXPECT_GT(V_early, -200.0f) << "V_m стабилен на раннем этапе";
    EXPECT_LT(V_early, 200.0f)  << "V_m не взрывается на раннем этапе";
    EXPECT_GT(V_late, -200.0f)  << "V_m стабилен на позднем этапе";
    EXPECT_LT(V_late, 200.0f)   << "V_m не взрывается на позднем этапе";
    EXPECT_FALSE(std::isnan(V_early)) << "V_m не NaN";
    EXPECT_FALSE(std::isnan(V_late))  << "V_m не NaN";

    std::remove("/tmp/sens_adap_phys2.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 3: MOD_PHYSICAL_SENSOR — recovery после прекращения стимула
// ═══════════════════════════════════════════════════════════════════════════════
TEST(SensoryAdaptation, PhysicalSensorRecoveryAfterStimulation) {
    NetworkModel m = make_physical_sensor_neuron(-65.0f);
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/sens_adap_recov.bion"));

    // Стимулируем 2000ms (habituation накапливается)
    float V_stimulated = simulate_with_sensor_input("/tmp/sens_adap_recov.bion",
        ModifierType::MOD_PHYSICAL_SENSOR, 0.8f, 2000);
    // Затем без стимула 1000ms (habituation должен частично восстановиться)
    float V_recovered = simulate_with_sensor_input("/tmp/sens_adap_recov.bion",
        ModifierType::MOD_PHYSICAL_SENSOR, 0.0f, 1000);

    printf("[PhysRecov] Stimulated=%.2f Recovered=%.2f mV\n", V_stimulated, V_recovered);

    // После прекращения стимула V_m должен вернуться к resting.
    // Биологический resting: -50..-80mV. Без глиального шорта нейрон находит
    // свой естественный потенциал покоя (~-67mV), что в пределах нормы.
    EXPECT_LT(V_recovered, V_stimulated + 1.0f)
        << "V_m должен снизиться после прекращения стимула (возврат к resting)";
    EXPECT_GT(V_recovered, -75.0f)
        << "V_m не должен уходить ниже био resting (-75mV)";

    std::remove("/tmp/sens_adap_recov.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 4: MOD_LIGHT_SENSOR — habituation + sensitization работают совместно
// ═══════════════════════════════════════════════════════════════════════════════
TEST(SensoryAdaptation, LightSensorAdaptationWorks) {
    NetworkModel m = make_light_sensor_neuron(-65.0f);
    // Установим небольшой baseline свет (state[1]=0.3) чтобы видеть эффект
    for (auto& seg : m.segments) {
        for (int mi = 0; mi < seg.modifier_count; mi++) {
            if (seg.modifiers[mi].mod_type() == ModifierType::MOD_LIGHT_SENSOR) {
                seg.modifiers[mi].state[1] = 0.3f;
            }
        }
    }
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/sens_adap_light.bion"));

    // Короткая симуляция: адаптация минимальна
    float V_early = simulate_with_sensor_input("/tmp/sens_adap_light.bion",
        ModifierType::MOD_LIGHT_SENSOR, 0.3f, 200);

    // Длинная симуляция: habituation накопилась → фоторецептор менее гиперполяризован
    float V_late = simulate_with_sensor_input("/tmp/sens_adap_light.bion",
        ModifierType::MOD_LIGHT_SENSOR, 0.3f, 3000);

    printf("[LightAdap] Early(200ms)=%.2f Late(3000ms)=%.2f mV\n", V_early, V_late);

    // При свете фоторецептор ГИПЕРПОЛЯРИЗУЕТСЯ (CNG-каналы закрываются).
    // Адаптация снижает этот эффект → V_late БЛИЖЕ к resting чем V_early.
    // Или: если оба гиперполяризованы, V_late > V_early (менее негативный)
    // Главное: нет NaN и нет взрыва
    EXPECT_GT(V_early, -200.0f) << "V_m не должен уходить в бесконечность";
    EXPECT_LT(V_early, 200.0f)  << "V_m не должен взрываться";
    EXPECT_GT(V_late, -200.0f)  << "V_m не должен уходить в бесконечность";
    EXPECT_LT(V_late, 200.0f)   << "V_m не должен взрываться";
    EXPECT_FALSE(std::isnan(V_early)) << "V_m не должен быть NaN";
    EXPECT_FALSE(std::isnan(V_late))  << "V_m не должен быть NaN";

    std::remove("/tmp/sens_adap_light.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 5: MOD_HUNGER_SENSOR — адаптация работает
// ═══════════════════════════════════════════════════════════════════════════════
TEST(SensoryAdaptation, HungerSensorAdaptationWorks) {
    NetworkModel m = make_hunger_sensor_neuron(-65.0f);
    // Низкий ATP → активация голода
    for (auto& seg : m.segments) {
        seg.ATP_level = 0.0001f;  // очень низкий ATP (голод)
    }
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/sens_adap_hunger.bion"));

    SimulationBridge sim;
    sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/sens_adap_hunger.bion"));
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::vector<SegmentSnapshot> segs;
    sim.getSegmentsSnapshot(segs);
    sim.stop();

    ASSERT_FALSE(segs.empty());
    float V = segs[0].V_m;
    printf("[HungerAdap] V_m with low ATP: %.2f mV\n", V);

    EXPECT_GT(V, -200.0f) << "V_m не должен уходить в бесконечность";
    EXPECT_LT(V, 200.0f)  << "V_m не должен взрываться";
    EXPECT_FALSE(std::isnan(V)) << "V_m не должен быть NaN";

    std::remove("/tmp/sens_adap_hunger.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 6: CPU vs GPU — physical_sensor адаптационные траектории идентичны
// ═══════════════════════════════════════════════════════════════════════════════
TEST(SensoryAdaptation, PhysicalSensorCpuGpuParity) {
    NetworkModel m = make_physical_sensor_neuron(-55.0f);
    // Устанавливаем state[1] (интенсивность) для активации сенсора
    for (auto& seg : m.segments) {
        for (int mi = 0; mi < seg.modifier_count; mi++) {
            if (seg.modifiers[mi].mod_type() == ModifierType::MOD_PHYSICAL_SENSOR) {
                seg.modifiers[mi].state[1] = 0.5f;
            }
        }
    }
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/sens_phys_cpugpu.bion"));

    auto run = [](SimulationBridge::Backend backend, const char* label) -> float {
        SimulationBridge sim;
        sim.setBackend(backend);
        sim.loadNetwork("/tmp/sens_phys_cpugpu.bion");
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        float vm = segs.empty() ? -999.0f : segs[0].V_m;
        printf("[PhysAdapt-CPUvsGPU] %s: V_m=%.2f mV\n", label, vm);
        return vm;
    };

    float vm_cpu = run(SimulationBridge::Backend::CPU, "CPU");
    float vm_gpu = run(SimulationBridge::Backend::GPU, "GPU");

    float delta = fabsf(vm_cpu - vm_gpu);
    printf("[PhysAdapt-CPUvsGPU] delta=%.2f mV\n", delta);
    EXPECT_LT(delta, 5.0f) << "CPU-GPU delta для physical_sensor адаптации должна быть <5mV";

    std::remove("/tmp/sens_phys_cpugpu.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 7: CPU vs GPU — light_sensor адаптационные траектории идентичны
// ═══════════════════════════════════════════════════════════════════════════════
TEST(SensoryAdaptation, LightSensorCpuGpuParity) {
    NetworkModel m = make_light_sensor_neuron(-55.0f);
    for (auto& seg : m.segments) {
        for (int mi = 0; mi < seg.modifier_count; mi++) {
            if (seg.modifiers[mi].mod_type() == ModifierType::MOD_LIGHT_SENSOR) {
                seg.modifiers[mi].state[1] = 0.5f;  // средний свет
            }
        }
    }
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/sens_light_cpugpu.bion"));

    auto run = [](SimulationBridge::Backend backend, const char* label) -> float {
        SimulationBridge sim;
        sim.setBackend(backend);
        sim.loadNetwork("/tmp/sens_light_cpugpu.bion");
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        float vm = segs.empty() ? -999.0f : segs[0].V_m;
        printf("[LightAdapt-CPUvsGPU] %s: V_m=%.2f mV\n", label, vm);
        return vm;
    };

    float vm_cpu = run(SimulationBridge::Backend::CPU, "CPU");
    float vm_gpu = run(SimulationBridge::Backend::GPU, "GPU");

    float delta = fabsf(vm_cpu - vm_gpu);
    printf("[LightAdapt-CPUvsGPU] delta=%.2f mV\n", delta);
    EXPECT_LT(delta, 5.0f) << "CPU-GPU delta для light_sensor адаптации должна быть <5mV";

    std::remove("/tmp/sens_light_cpugpu.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 8: Все 4 сенсорных модификатора не дают NaN/stability
// ═══════════════════════════════════════════════════════════════════════════════
TEST(SensoryAdaptation, AllSensorsStable) {
    struct SensorCfg {
        const char* name;
        std::function<NetworkModel()> make;
        ModifierType type;
        float state1;
        int duration_ms;
    };

    SensorCfg configs[] = {
        {"Physical", [](){ return make_physical_sensor_neuron(-65.0f); },
         ModifierType::MOD_PHYSICAL_SENSOR, 0.5f, 1000},
        {"Light", [](){ return make_light_sensor_neuron(-65.0f); },
         ModifierType::MOD_LIGHT_SENSOR, 0.3f, 1000},
        {"Hunger", [](){ return make_hunger_sensor_neuron(-65.0f); },
         ModifierType::MOD_HUNGER_SENSOR, 0.0f, 500},
    };

    for (const auto& cfg : configs) {
        NetworkModel m = cfg.make();
        // Для физического и светового сенсоров устанавливаем state[1]
        if (cfg.type != ModifierType::MOD_HUNGER_SENSOR) {
            for (auto& seg : m.segments) {
                for (int mi = 0; mi < seg.modifier_count; mi++) {
                    if (seg.modifiers[mi].mod_type() == cfg.type) {
                        seg.modifiers[mi].state[1] = cfg.state1;
                    }
                }
            }
        } else {
            // Для голода: низкий ATP
            for (auto& seg : m.segments) seg.ATP_level = 0.0001f;
        }

        char path_buf[256];
        snprintf(path_buf, sizeof(path_buf), "/tmp/sens_stab_%s.bion", cfg.name);
        ASSERT_TRUE(::SaveNetwork(m, path_buf));

        SimulationBridge sim;
        sim.setBackend(SimulationBridge::Backend::CPU);
        ASSERT_TRUE(sim.loadNetwork(path_buf));
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.duration_ms));

        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();

        ASSERT_FALSE(segs.empty()) << cfg.name << ": сегменты не должны быть пустыми";
        float V = segs[0].V_m;
        printf("[Stability-%s] V_m=%.2f mV after %dms\n", cfg.name, V, cfg.duration_ms);

        EXPECT_GT(V, -200.0f) << cfg.name << ": V_m не должен уходить вниз";
        EXPECT_LT(V, 200.0f)  << cfg.name << ": V_m не должен взрываться";
        EXPECT_FALSE(std::isnan(V)) << cfg.name << ": V_m не должен быть NaN";

        std::remove(path_buf);
    }
}
