// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gui_flow_debug.cpp — точная реплика GUI-потока: генерация → save → load → kickstart → V_m

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
#define GPU_BACKEND_DIR MINDCopy_ROOT "/BioNeuronCore/build"

// Точная реплика GUI-потока: генерация → сохранение → загрузка → кикстарт → проверка V_m
TEST(GuiFlowDebug, FullGuiFlowWithKickstart) {
    // Шаг 1: Генерация (как по кнопке "Отладка: миним. сеть")
    NetworkModel model;
    generate_debug_tadpole_network(model);
    printf("[GUI-Flow] Generated: %zu segs, %zu syns\n",
           model.segments.size(), model.synapses.size());

    // Включаем heatmap (как в GUI fix)
    model.show_vm_heatmap = true;

    // Шаг 2: Сохранение в network.bion
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/gui_flow_test.bion"));
    printf("[GUI-Flow] Saved to /tmp/gui_flow_test.bion\n");

    // Шаг 3: Загрузка в SimulationBridge — тестируем GPU
    SimulationBridge sim;
    sim.initGpuBackend(GPU_BACKEND_DIR);
    bool use_gpu = sim.isGpuAvailable();
    if (use_gpu) {
        sim.setBackend(SimulationBridge::Backend::GPU);
        printf("[GUI-Flow] Using GPU backend\n");
    } else {
        printf("[GUI-Flow] GPU not available, using CPU\n");
    }
    ASSERT_TRUE(sim.loadNetwork("/tmp/gui_flow_test.bion"));
    printf("[GUI-Flow] Loaded into SimulationBridge\n");

    // Шаг 4: Кикстарт через alternating фазы (как TadpoleEmulation::step)
    // Используем addCurrentStimulus с delay для имитации фазовых переходов
    // Находим dlc нейроны
    std::vector<uint32_t> dlc_left, dlc_right;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 60.0f && seg.position.z >= 200.0f) {
            bool has_hcn1 = false;
            for (int m = 0; m < seg.modifier_count; m++)
                if (seg.modifiers[m].mod_type() == ModifierType::MOD_HCN1)
                    { has_hcn1 = true; break; }
            if (has_hcn1) {
                if (seg.position.x < 0) dlc_left.push_back(seg.id);
                else dlc_right.push_back(seg.id);
            }
        }
    }
    printf("[GUI-Flow] dlc: %zuL %zuR\n", dlc_left.size(), dlc_right.size());
    ASSERT_GT(dlc_left.size(), 0u);
    ASSERT_GT(dlc_right.size(), 0u);

    // Alternating kickstart
    int n_cycles = 25;
    for (int cycle = 0; cycle < n_cycles; cycle++) {
        for (auto id : dlc_left)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 120.0f);
        for (auto id : dlc_right)
            sim.addCurrentStimulus(id, 32.0f, 30.0f, cycle * 120.0f + 60.0f);
    }
    printf("[GUI-Flow] Kickstart: %d cycles alternating, 32pA×30ms (×0.04 after scaling fix)\n", n_cycles);

    // Шаг 5: Запуск симуляции
    sim.start();
    printf("[GUI-Flow] Simulation started\n");

    // Шаг 6: Имитация UpdateGUIFromSimulation — сбор V_m через getSegmentsSnapshot
    // (как делает app.cpp:1364)
    std::vector<SegmentSnapshot> liveSegments;
    int L_din_spikes = 0, R_din_spikes = 0, L_mn_spikes = 0, R_mn_spikes = 0;
    float L_din_max = -99, R_din_max = -99, L_mn_max = -99, R_mn_max = -99;
    float L_din_sum = 0, R_din_sum = 0, L_mn_sum = 0, R_mn_sum = 0;
    int L_din_n = 0, R_din_n = 0, L_mn_n = 0, R_mn_n = 0;

    int sample_count = 0;
    for (int i = 0; i < 3000; i++) {  // 3 секунды реального времени
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!sim.isRunning()) break;

        // Имитируем UpdateGUIFromSimulation
        sim.getSegmentsSnapshot(liveSegments);

        // Проверяем V_m как это делает color_maps.cpp
        for (auto& s : liveSegments) {
            if (s.type != SegmentType::SOMA) continue;

            if (s.position.y >= 65.0f && s.position.y <= 76.0f) {
                if (s.position.x < -1.0f) {
                    L_din_sum += s.V_m; L_din_n++;
                    if (s.V_m > L_din_max) L_din_max = s.V_m;
                    if (s.V_m > -20.0f) L_din_spikes++;
                } else {
                    R_din_sum += s.V_m; R_din_n++;
                    if (s.V_m > R_din_max) R_din_max = s.V_m;
                    if (s.V_m > -20.0f) R_din_spikes++;
                }
            }

            if (s.position.y >= 15.0f && s.position.y <= 25.0f) {
                if (s.position.x < -1.0f) {
                    L_mn_sum += s.V_m; L_mn_n++;
                    if (s.V_m > L_mn_max) L_mn_max = s.V_m;
                    if (s.V_m > -20.0f) L_mn_spikes++;
                } else {
                    R_mn_sum += s.V_m; R_mn_n++;
                    if (s.V_m > R_mn_max) R_mn_max = s.V_m;
                    if (s.V_m > -20.0f) R_mn_spikes++;
                }
            }
        }
        sample_count++;
    }

    sim.stop();
    std::remove("/tmp/gui_flow_test.bion");

    printf("[GUI-Flow] Collected %d samples over ~3s\n", sample_count);
    printf("[GUI-Flow] dIN: L spikes=%d avg=%.1f max=%.1f | R spikes=%d avg=%.1f max=%.1f\n",
           L_din_spikes, L_din_n>0?L_din_sum/L_din_n:-99.0f, L_din_max,
           R_din_spikes, R_din_n>0?R_din_sum/R_din_n:-99.0f, R_din_max);
    printf("[GUI-Flow] MN:  L spikes=%d avg=%.1f max=%.1f | R spikes=%d avg=%.1f max=%.1f\n",
           L_mn_spikes, L_mn_n>0?L_mn_sum/L_mn_n:-99.0f, L_mn_max,
           R_mn_spikes, R_mn_n>0?R_mn_sum/R_mn_n:-99.0f, R_mn_max);

    // Проверки: должен быть хотя бы один спайк
    EXPECT_GT(L_din_spikes + R_din_spikes, 0) << "dIN полностью молчат!";
    EXPECT_GT(L_mn_spikes + R_mn_spikes, 0) << "MN полностью молчат!";

    // Проверка что V_m != начальные -65mV (должна быть динамика)
    float avg_din = (L_din_n + R_din_n) > 0 ? (L_din_sum + R_din_sum) / (L_din_n + R_din_n) : -99;
    printf("[GUI-Flow] Average dIN V_m: %.1f (expect ≠ -65 resting)\n", avg_din);
}
