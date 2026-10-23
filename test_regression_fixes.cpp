// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_regression_fixes.cpp — тесты регрессии на исправления 2026-06-27/30
// Покрывает: struct offsets, stimulus scaling, segment sanitization, shutdown order.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <unistd.h>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

// ══════════════════════════════════════════════════════════════════════════════
// 1. Segment struct offset correctness (GPU stimulus path)
// ══════════════════════════════════════════════════════════════════════════════
// Проверяем через runtime API, что смещения, используемые в simulation_bridge.cpp,
// соответствуют реальной структуре Segment.
TEST(RegressionFixes, SegmentStructOffsets) {
    // core_seg_size() возвращает sizeof(Segment) — должен быть >900 байт
    int seg_sz = core_seg_size();
    printf("[OFFSETS] sizeof(Segment)=%d (expected >=988)\n", seg_sz);
    EXPECT_GE(seg_sz, 988) << "Segment size changed — offsets in simulation_bridge.cpp"
        " (V_m=40, I_buffer=912, intra_ions=44, extra_ions=64, intra_nt=84, nt=140)"
        " must be updated!";
}

// ══════════════════════════════════════════════════════════════════════════════
// 2. Stimulus scaling fix: pA applied directly, NOT ×1e-12
// ══════════════════════════════════════════════════════════════════════════════
// Используем минимальную ручную сеть (1 сома) чтобы избежать saturation.
TEST(RegressionFixes, StimulusScalingCorrect) {
    NetworkModel model;
    model.bion_mode = BION_MODE_BIOLOGICAL;
    model.bion_filename = "test_stim.bion";

    // Ручная сборка: 1 сома + базовые модификаторы
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.length = 10; s.radius = 5;
    s.V_m = -65.0f; s.prev_V_m = -65.0f;
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    s.add_modifier(ModifierType::MOD_K2P_LEAK, 0.3f);
    s.add_modifier(ModifierType::MOD_KCC2, 0.6f);
    s.add_modifier(ModifierType::MOD_CA_LEAK, 0.08f);
    s.add_modifier(ModifierType::MOD_CA_BUFFER, 0.5f);
    s.intra_ions[0] = 0.015f; s.extra_ions[0] = 0.140f;
    s.intra_ions[1] = 0.140f; s.extra_ions[1] = 0.005f;
    s.intra_ions[2] = 0.00005f; s.extra_ions[2] = 0.0012f;
    s.intra_ions[3] = 0.005f; s.extra_ions[3] = 0.130f;
    model.segments.push_back(s);
    NeuronInfo ni; ni.id = 0; ni.segment_start = 0; ni.segment_count = 1; ni.soma_index = 0;
    model.neurons.push_back(ni);

    ASSERT_TRUE(SaveNetwork(model, "/tmp/test_stim_scale.bion"));

    SimulationBridge sim;
    sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_stim_scale.bion"));

    sim.start();
    // 100pA на 500ms — должно дать ~10-20mV сдвиг
    sim.addCurrentStimulus(0, 100.0f, 500.0f, 0.0f);

    // Ждём несколько тиков
    usleep(200000);

    std::vector<SegmentSnapshot> snap;
    sim.getSegmentsSnapshot(snap);
    ASSERT_EQ(snap.size(), 1u);
    float vm = snap[0].V_m;

    printf("[STIM] 1-neuron V_m=%.3f mV after 100pA stimulus (started at -65mV)\n", vm);
    // 100pA × 500ms должен сдвинуть V_m от -65mV
    EXPECT_NE(vm, -65.0f) << "Stimulus had ZERO effect — check scaling (was ×1e-12 bug)";
    EXPECT_GT(vm, -60.0f) << "V_m=" << vm << "mV — stimulus too weak or broken scaling";
    EXPECT_LT(vm, 150.0f) << "V_m=" << vm << "mV — saturated, test invalid";

    sim.stop();
    std::remove("/tmp/test_stim_scale.bion");
}

// ══════════════════════════════════════════════════════════════════════════════
// 3. Modifier_count validity (pre-kernel sanitization regression)
// ══════════════════════════════════════════════════════════════════════════════
// До pre-kernel sanitization modifier_count>MAX_MODIFIERS_PER_SEGMENT вызывал
// out-of-bounds доступ в GPU-ядре compute_clamp_segments → GPU page fault.
TEST(RegressionFixes, ModifierCountValid) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    for (auto& seg : model.segments) {
        EXPECT_LE(seg.modifier_count, MAX_MODIFIERS_PER_SEGMENT)
            << "Segment " << seg.id << " modifier_count=" << (int)seg.modifier_count
            << " > MAX(" << MAX_MODIFIERS_PER_SEGMENT << ") — GPU kernel would crash!";
    }
    printf("[SANITIZE] %zu segments, all modifier_count <= %d\n",
           model.segments.size(), MAX_MODIFIERS_PER_SEGMENT);
}

// ══════════════════════════════════════════════════════════════════════════════
// 4. No NaN in modifier states (GPU NaN guard regression)
// ══════════════════════════════════════════════════════════════════════════════
TEST(RegressionFixes, NoNanInModifierStates) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    int nan_count = 0;
    for (auto& seg : model.segments) {
        for (int m = 0; m < seg.modifier_count; m++) {
            for (int k = 0; k < 8; k++) {
                if (std::isnan(seg.modifiers[m].state[k])) {
                    nan_count++;
                    break;
                }
            }
        }
    }
    printf("[NAN] NaN modifier states: %d (should be 0)\n", nan_count);
    EXPECT_EQ(nan_count, 0) << "NaN in modifier state — GPU kernel crash risk!";
}

// ══════════════════════════════════════════════════════════════════════════════
// 5. Shutdown order: симуляция останавливается ДО очистки модели
// ══════════════════════════════════════════════════════════════════════════════
// Регрессия: был use-after-free в ~Application() когда модель чистилась
// до остановки симуляции.
TEST(RegressionFixes, ShutdownStopBeforeClear) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_TRUE(SaveNetwork(model, "/tmp/test_shutdown.bion"));

    // Имитируем правильный порядок ~Application():
    // 1. Shutdown() → sim.stop()
    // 2. model.clear()
    {
        SimulationBridge sim;
        sim.setBackend(SimulationBridge::Backend::CPU);
        ASSERT_TRUE(sim.loadNetwork("/tmp/test_shutdown.bion"));
        sim.start();
        usleep(50000);  // даём симуляции поработать

        // Правильный порядок: остановка ДО очистки
        sim.stop();         // шаг 1: остановили потоки
    }  // шаг 2: ~SimulationBridge() очищает ресурсы

    // Если дошли сюда — use-after-free не произошёл
    printf("[SHUTDOWN] Stop-before-clear OK\n");
    std::remove("/tmp/test_shutdown.bion");
}

// ══════════════════════════════════════════════════════════════════════════════
// 6. Pre-kernel sanitization: V_m и ионы в допустимых пределах
// ══════════════════════════════════════════════════════════════════════════════
TEST(RegressionFixes, VmAndIonBounds) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    int extreme = 0;
    for (auto& seg : model.segments) {
        // V_m при инициализации должен быть в [-200, +150]
        if (seg.V_m < -200.1f || seg.V_m > 150.1f) extreme++;
        // Ионы не должны быть NaN или отрицательными
        for (int i = 0; i < ION_COUNT; i++) {
            if (std::isnan(seg.intra_ions[i]) || seg.intra_ions[i] < 0) extreme++;
            if (std::isnan(seg.extra_ions[i]) || seg.extra_ions[i] < 0) extreme++;
        }
    }
    printf("[BOUNDS] Extreme/NaN values at init: %d (should be 0)\n", extreme);
    EXPECT_EQ(extreme, 0) << "Segments have out-of-range values at init — GPU crash risk!";
}

// ══════════════════════════════════════════════════════════════════════════════
// 7. Stimulus buffer не переполняется при массовых стимулах (sensory noise)
// ══════════════════════════════════════════════════════════════════════════════
TEST(RegressionFixes, StimulusBufferNotOverflowing) {
    NetworkModel model;
    generate_debug_tadpole_network(model);
    ASSERT_TRUE(SaveNetwork(model, "/tmp/test_stim_buf.bion"));

    SimulationBridge sim;
    sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_stim_buf.bion"));

    // Находим сомы
    std::vector<uint32_t> somas;
    {
        std::vector<SegmentSnapshot> snap;
        sim.getSegmentsSnapshot(snap);
        for (auto& s : snap)
            if (s.type == SegmentType::SOMA) somas.push_back(s.id);
    }
    ASSERT_GT(somas.size(), 10u);

    sim.start();

    // Симулируем sensory noise: много стимулов каждые 3 тика (как в эмуляции)
    constexpr int BURST_COUNT = 100;  // 100 раундов шума
    int applied = 0;
    for (int burst = 0; burst < BURST_COUNT; burst++) {
        for (size_t i = 0; i < somas.size(); i++) {
            float noise_pa = 20.0f + (rand() % 800) * 0.1f;
            sim.addCurrentStimulus(somas[i], noise_pa, 30.0f, 0.0f);
            applied++;
        }
        // Симулируем 3 тика между burst-ами
        usleep(3000);
    }

    sim.stop();
    printf("[STIMBUF] Applied %d stimuli in %d bursts, no crash\n", applied, BURST_COUNT);
    // Если дошли сюда — буфер не переполнился и не вызвал краш
    std::remove("/tmp/test_stim_buf.bion");
}

// ══════════════════════════════════════════════════════════════════════════════
// 8. CPG gly tau_clearance в допустимых пределах после фикса
// ══════════════════════════════════════════════════════════════════════════════
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"

TEST(RegressionFixes, GlycineTauClearanceReduced) {
    auto p = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    printf("[GLY] FREE_SWIMMING tau_clearance=%.0f ms (was 30)\n", p.cpg_glycine_tau_clearance);
    // После фикса должно быть ≤16 (было 30)
    EXPECT_LE(p.cpg_glycine_tau_clearance, 16.0f) << "Gly tau NOT reduced from 30!";
}

// ══════════════════════════════════════════════════════════════════════════════
// 9. Stimulus pA scaling: проверка что сильный стимул даёт сильный сдвиг
// ══════════════════════════════════════════════════════════════════════════════
TEST(RegressionFixes, StimulusProducesProportionalDelta) {
    // Минимальная сеть: 1 сома для чистого теста
    NetworkModel model;
    model.bion_mode = BION_MODE_BIOLOGICAL;
    model.bion_filename = "test_prop.bion";

    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.length = 10; s.radius = 5;
    s.V_m = -65.0f; s.prev_V_m = -65.0f;
    s.add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s.add_modifier(ModifierType::MOD_PUMP_NAK, 0.2f);
    s.add_modifier(ModifierType::MOD_K2P_LEAK, 0.3f);
    s.add_modifier(ModifierType::MOD_KCC2, 0.6f);
    s.add_modifier(ModifierType::MOD_CA_LEAK, 0.08f);
    s.add_modifier(ModifierType::MOD_CA_BUFFER, 0.5f);
    s.intra_ions[0] = 0.015f; s.extra_ions[0] = 0.140f;
    s.intra_ions[1] = 0.140f; s.extra_ions[1] = 0.005f;
    s.intra_ions[2] = 0.00005f; s.extra_ions[2] = 0.0012f;
    s.intra_ions[3] = 0.005f; s.extra_ions[3] = 0.130f;
    model.segments.push_back(s);
    NeuronInfo ni; ni.id = 0; ni.segment_start = 0; ni.segment_count = 1; ni.soma_index = 0;
    model.neurons.push_back(ni);

    ASSERT_TRUE(SaveNetwork(model, "/tmp/test_prop.bion"));

    SimulationBridge sim;
    sim.setBackend(SimulationBridge::Backend::CPU);
    ASSERT_TRUE(sim.loadNetwork("/tmp/test_prop.bion"));

    sim.start();
    usleep(100000);  // стабилизация

    // Замер V_m до
    float vm_before;
    {
        std::vector<SegmentSnapshot> snap;
        sim.getSegmentsSnapshot(snap);
        vm_before = snap[0].V_m;
    }

    // Сильный стимул: 500pA × 100ms
    sim.addCurrentStimulus(0, 500.0f, 100.0f, 0.0f);
    usleep(200000);

    float vm_after;
    {
        std::vector<SegmentSnapshot> snap;
        sim.getSegmentsSnapshot(snap);
        vm_after = snap[0].V_m;
    }

    float delta = vm_after - vm_before;
    printf("[PROP] V_m before=%.2f after=%.2f delta=%.2f mV (500pA×100ms)\n",
           vm_before, vm_after, delta);
    // 500pA×100ms должен дать >10mV сдвиг
    EXPECT_GT(fabs(delta), 10.0f)
        << "500pA produced only " << delta << "mV — scaling broken (was ×1e-12)";

    sim.stop();
    std::remove("/tmp/test_prop.bion");
}
