// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_experiment_full_workflow.cpp — тесты полного цикла эксперимента:
// создание→запись→сохранение .bexp→загрузка→повтор, batch repeat, auto-save.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <sys/stat.h>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/experiment_data.h"
#include "../BioNeuronStudio/src/experiment_manager.h"
#include "../BioNeuronStudio/src/probe_panel.h"

// ═══════════════════════════════════════════════════════════════════
// Хелпер: минимальная модель с пробами для записи
// ═══════════════════════════════════════════════════════════════════

static NetworkModel makeTestModel() {
    NetworkModel m;
    m.create_segment({0, 0, 0}, SegmentType::SOMA);
    m.segments[0].V_m = -65.0f; m.segments[0].radius = 10.0f; m.segments[0].length = 20.0f;
    m.segments[0].add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    m.segments[0].add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);

    m.create_segment({0, 20, 0}, SegmentType::DENDRITE);
    m.segments[1].V_m = -65.0f; m.segments[1].radius = 3.0f; m.segments[1].length = 30.0f;
    m.segments[1].add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    m.segments[1].add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);

    // Добавляем электрод на сегмент 1
    Probe probe;
    probe.type = ProbeType::ELECTRODE;
    probe.segment_id = m.segments[0].id;
    strncpy(probe.label, "TestProbe1", 31);
    probe.record_vm = true;
    probe.record_ions[0] = true;  // Na
    probe.record_ions[2] = true;  // Ca
    m.probes.push_back(probe);

    // Канюля на сегмент 2
    Probe cannula;
    cannula.type = ProbeType::CANNULE;
    cannula.segment_id = m.segments[1].id;
    strncpy(cannula.label, "TestCannula", 31);
    cannula.record_vm = true;
    cannula.substance_type = CannulaSubstance::LIGAND;
    cannula.inject_substance_idx = 3;  // serotonin
    cannula.inject_amount_mM = 0.5f;
    cannula.inject_enabled = true;
    m.probes.push_back(cannula);

    return m;
}

// ═══════════════════════════════════════════════════════════════════
// Тесты
// ═══════════════════════════════════════════════════════════════════

TEST(ExperimentFullWorkflow, RecordSaveLoadRoundtrip) {
    const char* exp_path = "/tmp/test_full_workflow.bexp";
    NetworkModel model = makeTestModel();

    // ── Создание и запись ──
    ExperimentManager mgr;
    ASSERT_TRUE(mgr.startRecording(model, "TestExperiment",
                                    "Full workflow test", "Pyramidal"));

    // Симулируем запись нескольких тиков (как делает SimulationBridge)
    ProbeManager pm;  // пустой, без реальных пробов
    for (uint32_t tick = 0; tick < 50; tick++) {
        // Меняем V_m для проверки что данные записываются
        model.segments[0].V_m = -65.0f + tick * 0.1f;
        model.segments[1].V_m = -65.0f - tick * 0.05f;
        mgr.recordTick(model, pm, tick);
    }

    mgr.stopRecording();
    EXPECT_FALSE(mgr.isRecording());

    // ── Сохранение ──
    ASSERT_TRUE(mgr.saveToFile(exp_path));

    // Проверяем что файл существует и не пустой
    struct stat st;
    ASSERT_EQ(stat(exp_path, &st), 0);
    EXPECT_GT(st.st_size, 100) << "Saved .bexp should be > 100 bytes";

    // ── Загрузка ──
    ExperimentData loaded;
    std::string err;
    ASSERT_TRUE(mgr.loadFromFile(exp_path, loaded, err)) << "Load error: " << err;

    EXPECT_EQ(loaded.meta.name, "TestExperiment");
    EXPECT_EQ(loaded.meta.description, "Full workflow test");
    EXPECT_EQ(loaded.meta.probe_count, 2u);
    EXPECT_EQ(loaded.meta.tick_count, 50u);
    EXPECT_EQ(loaded.probes.size(), 2u);
    EXPECT_EQ(loaded.probes[0].label[0], 'T');  // "TestProbe1"
    EXPECT_EQ((int)loaded.frames.size(), 50 * (int)loaded.frame_stride);

    unlink(exp_path);
}

TEST(ExperimentFullWorkflow, RecordingLifecycle) {
    NetworkModel model = makeTestModel();
    ExperimentManager mgr;

    // Начальное состояние
    EXPECT_FALSE(mgr.isRecording());
    EXPECT_FALSE(mgr.isPaused());

    // Старт записи
    ASSERT_TRUE(mgr.startRecording(model, "Lifecycle", "Test", "Soma"));
    EXPECT_TRUE(mgr.isRecording());

    // Пауза
    mgr.pauseRecording();
    EXPECT_TRUE(mgr.isPaused());

    // Resume
    mgr.resumeRecording();
    EXPECT_FALSE(mgr.isPaused());

    // Стоп
    mgr.stopRecording();
    EXPECT_FALSE(mgr.isRecording());

    // Повторный стоп не крашит
    mgr.stopRecording();
}

TEST(ExperimentFullWorkflow, StartRecordingWithoutProbes) {
    NetworkModel model;
    model.create_segment({0, 0, 0}, SegmentType::SOMA);
    model.segments[0].V_m = -65.0f;

    ExperimentManager mgr;
    // Модель без пробов — запись всё равно должна работать (хотя каналов будет 0)
    EXPECT_TRUE(mgr.startRecording(model, "NoProbes", "No probes test", "Test"));
    ProbeManager pm;
    mgr.recordTick(model, pm, 0);
    mgr.recordTick(model, pm, 1);
    mgr.stopRecording();
}

TEST(ExperimentFullWorkflow, BatchRepeatTemplate) {
    const char* tmpl_path = "/tmp/test_batch_template.bexp_template";
    NetworkModel model = makeTestModel();

    // Записываем несколько тиков
    ExperimentManager mgr;
    ASSERT_TRUE(mgr.startRecording(model, "BatchTest", "Template", "Test"));
    ProbeManager pm;
    for (uint32_t t = 0; t < 10; t++) mgr.recordTick(model, pm, t);
    mgr.stopRecording();

    // Сохраняем как шаблон (без кадров, только setup)
    ASSERT_TRUE(mgr.saveAsTemplate(tmpl_path, mgr.activeExperiment()));

    // Проверяем что шаблон существует
    EXPECT_EQ(access(tmpl_path, F_OK), 0);

    // Загружаем шаблон
    ExperimentData tmpl_data;
    std::string err;
    ASSERT_TRUE(mgr.loadFromFile(tmpl_path, tmpl_data, err));
    EXPECT_EQ(tmpl_data.meta.tick_count, 0u);   // tick_count обнулён
    EXPECT_EQ((int)tmpl_data.frames.size(), 0);  // без кадров
    EXPECT_EQ(tmpl_data.probes.size(), 2u);      // пробы сохранены

    // Применяем настройки к новой модели
    NetworkModel new_model;
    new_model.create_segment({10, 10, 10}, SegmentType::SOMA);
    new_model.segments[0].V_m = -70.0f;
    new_model.segments[0].radius = 15.0f;
    new_model.segments[0].length = 25.0f;

    new_model.create_segment({10, 30, 10}, SegmentType::DENDRITE);
    new_model.segments[1].V_m = -70.0f;
    new_model.segments[1].radius = 4.0f;
    new_model.segments[1].length = 40.0f;

    ExperimentManager::ApplyExperimentSetup(new_model, tmpl_data);
    EXPECT_EQ(new_model.probes.size(), 2u);

    unlink(tmpl_path);
}

TEST(ExperimentFullWorkflow, SaveWithoutRecordingSavesEmptyExperiment) {
    ExperimentManager mgr;
    // saveToFile сохраняет active_experiment_ (может быть пустым, но валидным)
    // Главное — не крашит и возвращает true
    bool ok = mgr.saveToFile("/tmp/empty_experiment.bexp");
    EXPECT_TRUE(ok);
    unlink("/tmp/empty_experiment.bexp");
}

TEST(ExperimentFullWorkflow, LoadNonexistentFileReturnsFalse) {
    ExperimentManager mgr;
    ExperimentData data;
    std::string err;
    EXPECT_FALSE(mgr.loadFromFile("/tmp/nonexistent_file_xyz.bexp", data, err));
    EXPECT_FALSE(err.empty()) << "Error message should be set";
}
