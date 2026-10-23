// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// G3-P4: Тесты ExperimentPlayer (Load валидация + GetChannelValue)
#include <gtest/gtest.h>
#include "../BioNeuronStudio/src/experiment_player.h"
#include "../BioNeuronStudio/src/experiment_data.h"

static ExperimentData make_valid_data() {
    ExperimentData d;
    d.meta.name = "test";
    d.tick_count = 100;
    d.frame_stride = 1;
    d.probe_configs.push_back({"probe0", 0, 0, 0, 1, {}});
    // frames: tick_count * frame_stride * probes * (1 Vm + 0 ions)
    d.frames.resize(100, 0.0f);
    return d;
}

TEST(ExperimentPlayer, LoadValid) {
    ExperimentPlayer player;
    auto data = make_valid_data();
    EXPECT_TRUE(player.Load(data));
}

TEST(ExperimentPlayer, LoadEmpty) {
    ExperimentPlayer player;
    ExperimentData empty;
    EXPECT_FALSE(player.Load(empty));
}

TEST(ExperimentPlayer, LoadBadStride) {
    ExperimentPlayer player;
    auto data = make_valid_data();
    data.frame_stride = 0;
    EXPECT_FALSE(player.Load(data));
    data.frame_stride = 2000;
    EXPECT_FALSE(player.Load(data));
}

TEST(ExperimentPlayer, LoadBadTickCount) {
    ExperimentPlayer player;
    auto data = make_valid_data();
    data.tick_count = 0;
    EXPECT_FALSE(player.Load(data));
    data.tick_count = 20000000;
    EXPECT_FALSE(player.Load(data));
}

TEST(ExperimentPlayer, LoadFrameMismatch) {
    ExperimentPlayer player;
    auto data = make_valid_data();
    data.tick_count = 10;
    data.frames.resize(5);  // too few frames
    EXPECT_FALSE(player.Load(data));
}

TEST(ExperimentPlayer, GetChannelValue) {
    ExperimentPlayer player;
    auto data = make_valid_data();
    data.frames[0] = -65.0f;
    data.frames[1] = -40.0f;
    ASSERT_TRUE(player.Load(data));

    float v = player.GetChannelValue(0, 0, 0);  // frame 0, probe 0, channel 0 (Vm)
    EXPECT_FLOAT_EQ(v, -65.0f);

    v = player.GetChannelValue(1, 0, 0);  // frame 1, probe 0, channel 0
    EXPECT_FLOAT_EQ(v, -40.0f);
}

TEST(ExperimentPlayer, GetChannelValueOutOfBounds) {
    ExperimentPlayer player;
    auto data = make_valid_data();
    ASSERT_TRUE(player.Load(data));

    // Out of bounds should return 0.0f
    EXPECT_FLOAT_EQ(player.GetChannelValue(99999, 0, 0), 0.0f);
    EXPECT_FLOAT_EQ(player.GetChannelValue(0, 999, 0), 0.0f);
}
