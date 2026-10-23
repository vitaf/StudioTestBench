// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// G3-P1: Тесты ExperimentManager (search, sort, save/load, config, timestamp)
#include <gtest/gtest.h>
#include <cstdio>
#include "../BioNeuronStudio/src/experiment_manager.h"
#include "../BioNeuronStudio/src/experiment_data.h"

static ExperimentManager make_manager_with_data() {
    ExperimentManager mgr;
    for (int i = 0; i < 5; i++) {
        ExperimentIndexEntry e;
        e.name = std::string("exp_") + char('A' + i);
        e.description = std::string("desc_") + char('a' + i);
        e.date = "2026-08-0" + std::to_string(1 + i);
        e.cell_type = (i % 2 == 0) ? "pyramidal" : "interneuron";
        mgr.add_to_index(e);
    }
    return mgr;
}

TEST(ExperimentManager, SearchByName) {
    auto mgr = make_manager_with_data();
    auto results = mgr.search("exp_A");
    EXPECT_EQ(results.size(), 1u);

    results = mgr.search("exp_");
    EXPECT_EQ(results.size(), 5u);

    results = mgr.search("NONEXISTENT");
    EXPECT_EQ(results.size(), 0u);
}

TEST(ExperimentManager, SearchCaseInsensitive) {
    auto mgr = make_manager_with_data();
    auto results = mgr.search("EXP_A");
    EXPECT_EQ(results.size(), 1u);

    results = mgr.search("Pyramidal");
    EXPECT_EQ(results.size(), 3u);  // A, C, E
}

TEST(ExperimentManager, SortByName) {
    auto mgr = make_manager_with_data();
    mgr.sortIndex(0, true);  // sort by name ascending
    EXPECT_EQ(mgr.index_size(), 5u);
}

TEST(ExperimentManager, SortByDate) {
    auto mgr = make_manager_with_data();
    mgr.sortIndex(2, false);  // sort by date descending
    EXPECT_EQ(mgr.index_size(), 5u);
}

TEST(ExperimentManager, Timestamp) {
    std::string ts = ExperimentManager::getCurrentTimestamp();
    EXPECT_GE(ts.size(), 10u);  // at least "YYYY-MM-DD"
    EXPECT_EQ(ts[4], '-');
    EXPECT_EQ(ts[7], '-');
}

TEST(ExperimentManager, ConfigToProbe) {
    ExperimentProbeConfig cfg;
    cfg.name = "test_probe";
    cfg.neuron_id = 42;
    cfg.segment_index = 1;
    cfg.record_ions = true;

    auto probe = ExperimentManager::ConfigToProbe(cfg);
    EXPECT_EQ(probe.neuron_id, 42u);
}

TEST(ExperimentManager, ApplyExperimentSetup) {
    NetworkModel model;
    ExperimentData data;
    data.meta.name = "test";
    ExperimentProbeConfig cfg;
    cfg.name = "p1"; cfg.neuron_id = 0; cfg.segment_index = 0;
    data.probe_configs.push_back(cfg);

    ExperimentManager::ApplyExperimentSetup(model, data);
    EXPECT_EQ(model.probes.size(), 1u);
}

TEST(ExperimentManager, SaveLoadRoundTrip) {
    ExperimentManager mgr;
    ExperimentData orig;
    orig.meta.name = "roundtrip_test";
    orig.meta.description = "test description";
    orig.tick_count = 50;
    orig.frame_stride = 2;
    orig.frames = { -65.0f, -64.0f, -40.0f, -39.0f, -20.0f, -19.0f, -21.0f, -20.0f };

    const char* path = "/tmp/test_g3_roundtrip.bexp";
    std::string err;
    bool saved = mgr.saveToFile(path, orig, err);
    ASSERT_TRUE(saved) << "Save failed: " << err;

    ExperimentData loaded;
    bool loaded_ok = mgr.loadFromFile(path, loaded, err);
    ASSERT_TRUE(loaded_ok) << "Load failed: " << err;

    EXPECT_EQ(loaded.meta.name, orig.meta.name);
    EXPECT_EQ(loaded.meta.description, orig.meta.description);
    EXPECT_EQ(loaded.tick_count, orig.tick_count);
    EXPECT_EQ(loaded.frame_stride, orig.frame_stride);
    EXPECT_EQ(loaded.frames.size(), orig.frames.size());

    std::remove(path);
}
