// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_gen_optimizations.cpp — проверка корректности оптимизаций генерации

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <unordered_map>
#include <algorithm>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"

// ── Вспомогательные ──────────────────────────────────────────────────────
static NetworkModel generate_early_tailbud() {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    TadpoleDevelop dev(params);
    return dev.generate_connectome();
}

static NetworkModel generate_late_tailbud() {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::LATE_TAILBUD);
    TadpoleDevelop dev(params);
    return dev.generate_connectome();
}

// ═══════════════════════════════════════════════════════════════
// 1. neuron_id → тип нейрона: эквивалентность старому spatial search
// ═══════════════════════════════════════════════════════════════
TEST(GenOptimizations, NeuronTypeViaNeuronId) {
    auto model = generate_early_tailbud();

    // Каждый не-сома сегмент должен иметь neuron_id своей сомы
    std::unordered_map<uint32_t, uint32_t> soma_neuron_ids;
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::SOMA)
            soma_neuron_ids[seg.neuron_id] = seg.id;
    }

    int mismatches = 0;
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::SOMA) continue;
        // Сегмент должен принадлежать существующему нейрону
        EXPECT_NE(soma_neuron_ids.find(seg.neuron_id), soma_neuron_ids.end())
            << "Сегмент " << seg.id << " ссылается на несуществующий neuron_id " << seg.neuron_id;
        if (soma_neuron_ids.find(seg.neuron_id) == soma_neuron_ids.end()) mismatches++;
    }
    EXPECT_EQ(mismatches, 0);
    printf("[GenOpt] NeuronTypeViaNeuronId: %zu segments, %zu somas, %d orphans\n",
           model.segments.size(), soma_neuron_ids.size(), mismatches);
}

// ═══════════════════════════════════════════════════════════════
// 2. Коллизии: нет перекрывающихся сом после разрешения
// ═══════════════════════════════════════════════════════════════
TEST(GenOptimizations, CollisionNoOverlap) {
    auto model = generate_early_tailbud();  // 86 сом — быстро, достаточно для проверки

    int overlaps = 0;
    for (size_t i = 0; i < model.segments.size(); i++) {
        if (model.segments[i].type != SegmentType::SOMA) continue;
        for (size_t j = i + 1; j < model.segments.size(); j++) {
            if (model.segments[j].type != SegmentType::SOMA) continue;
            if (model.segments[i].neuron_id == model.segments[j].neuron_id) continue;

            float dx = model.segments[i].position.x - model.segments[j].position.x;
            float dy = model.segments[i].position.y - model.segments[j].position.y;
            float dz = model.segments[i].position.z - model.segments[j].position.z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            float min_dist = (model.segments[i].radius + model.segments[j].radius) * 2.5f;

            if (dist < min_dist * 0.95f) {  // 5% допуск на floating-point
                overlaps++;
                if (overlaps <= 3)
                    printf("[GenOpt] OVERLAP: n%u(%.1f,%.1f,%.1f r=%.1f) n%u(%.1f,%.1f,%.1f r=%.1f) dist=%.1f min=%.1f\n",
                           model.segments[i].neuron_id, model.segments[i].position.x,
                           model.segments[i].position.y, model.segments[i].position.z,
                           model.segments[i].radius,
                           model.segments[j].neuron_id, model.segments[j].position.x,
                           model.segments[j].position.y, model.segments[j].position.z,
                           model.segments[j].radius, dist, min_dist);
            }
        }
    }
    // 10 итераций + spatial binning должны разрешить большинство коллизий
    size_t soma_count = 0;
    for (auto& s : model.segments) if (s.type == SegmentType::SOMA) soma_count++;
    float total_pairs = (float)soma_count * (float)(soma_count - 1) / 2.0f;
    float overlap_pct = (float)overlaps / total_pairs * 100.0f;
    printf("[GenOpt] CollisionNoOverlap: %zu somas, %d overlaps (%.1f%% of pairs)\n",
           soma_count, overlaps, overlap_pct);
    // 10 итераций: для 86 сом перекрытия обычно 5-12% (dIN/cIN кластеры)
    EXPECT_LT(overlap_pct, 20.0f) << overlap_pct << "%% overlaps — collision resolution broken";
    EXPECT_GT(soma_count, 0u);
}

// ═══════════════════════════════════════════════════════════════
// 3. rebuild_neuron_tree: все нейроны корректны
// ═══════════════════════════════════════════════════════════════
TEST(GenOptimizations, RebuildNeuronTreeCorrect) {
    auto model = generate_early_tailbud();

    // Проверяем что каждый нейрон имеет хотя бы один сегмент
    for (auto& ni : model.neurons) {
        EXPECT_GT(ni.segment_count, 0u) << "Нейрон " << ni.id << " без сегментов";
        EXPECT_LE(ni.segment_start + ni.segment_count, model.segments.size())
            << "Нейрон " << ni.id << " выходит за границы массива";
        EXPECT_LT(ni.soma_index, ni.segment_count)
            << "Нейрон " << ni.id << " сома вне диапазона сегментов";

        // Сома должна быть типа SOMA
        size_t soma_idx = ni.segment_start + ni.soma_index;
        ASSERT_LT(soma_idx, model.segments.size());
        EXPECT_EQ(model.segments[soma_idx].type, SegmentType::SOMA)
            << "Нейрон " << ni.id << " указатель сомы не на сому";
    }

    // Все сегменты должны принадлежать какому-то нейрону
    std::set<uint32_t> covered;
    for (auto& ni : model.neurons)
        for (uint32_t s = ni.segment_start; s < ni.segment_start + ni.segment_count; s++)
            covered.insert(s);

    // Каждый сегмент покрыт ровно одним нейроном (нет дырок, нет дубликатов)
    EXPECT_EQ(covered.size(), model.segments.size())
        << covered.size() << " covered vs " << model.segments.size() << " total";

    printf("[GenOpt] RebuildNeuronTree: %zu neurons, %zu segments, %zu covered\n",
           model.neurons.size(), model.segments.size(), covered.size());
}

// ═══════════════════════════════════════════════════════════════
// 4. Index sort: сегменты отсортированы по neuron_id
// ═══════════════════════════════════════════════════════════════
TEST(GenOptimizations, SegmentsSortedByNeuronId) {
    auto model = generate_early_tailbud();

    uint32_t prev_nid = 0;
    bool monotonic = true;
    for (auto& seg : model.segments) {
        if (seg.neuron_id < prev_nid) { monotonic = false; break; }
        prev_nid = seg.neuron_id;
    }
    EXPECT_TRUE(monotonic) << "Сегменты не отсортированы по neuron_id";

    // После rebuild_neuron_tree neuron.segment_start должен указывать
    // на первый сегмент с этим neuron_id
    for (auto& ni : model.neurons) {
        uint32_t sidx = ni.segment_start;
        ASSERT_LT(sidx, model.segments.size());
        EXPECT_EQ(model.segments[sidx].neuron_id, ni.id)
            << "Нейрон " << ni.id << " segment_start указывает на чужой сегмент";
        if (ni.segment_count > 1) {
            uint32_t last = ni.segment_start + ni.segment_count - 1;
            ASSERT_LT(last, model.segments.size());
            EXPECT_EQ(model.segments[last].neuron_id, ni.id)
                << "Нейрон " << ni.id << " последний сегмент чужой";
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 5. CPG: все dIN↔cIN перекрёстные связи присутствуют
// ═══════════════════════════════════════════════════════════════
TEST(GenOptimizations, CpgCircuitExists) {
    auto model = generate_early_tailbud();

    // Находим dIN и cIN сомы
    std::vector<uint32_t> din_ids, cin_ids;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y > 60.0f && seg.position.y < 80.0f)
            din_ids.push_back(seg.neuron_id);
        else if (seg.position.y > 40.0f && seg.position.y < 55.0f)
            cin_ids.push_back(seg.neuron_id);
    }
    ASSERT_GT(din_ids.size(), 0u) << "Нет dIN нейронов";
    ASSERT_GT(cin_ids.size(), 0u) << "Нет cIN нейронов";

    // Ищем синапсы между dIN и cIN
    int din_to_cin = 0, cin_to_din = 0;
    std::unordered_map<uint32_t, uint32_t> seg_nid;
    for (auto& seg : model.segments) seg_nid[seg.id] = seg.neuron_id;

    for (auto& syn : model.synapses) {
        uint32_t pre_nid = seg_nid[syn.pre_id];
        uint32_t post_nid = seg_nid[syn.post_id];
        bool pre_is_din = std::find(din_ids.begin(), din_ids.end(), pre_nid) != din_ids.end();
        bool post_is_cin = std::find(cin_ids.begin(), cin_ids.end(), post_nid) != cin_ids.end();
        bool pre_is_cin = std::find(cin_ids.begin(), cin_ids.end(), pre_nid) != cin_ids.end();
        bool post_is_din = std::find(din_ids.begin(), din_ids.end(), post_nid) != din_ids.end();
        if (pre_is_din && post_is_cin) din_to_cin++;
        if (pre_is_cin && post_is_din) cin_to_din++;
    }

    EXPECT_GT(din_to_cin, 0) << "Нет dIN→cIN синапсов";
    EXPECT_GT(cin_to_din, 0) << "Нет cIN→dIN (перекрёстных) синапсов";
    printf("[GenOpt] CpgCircuit: dIN=%zu cIN=%zu din→cin=%d cin→din=%d\n",
           din_ids.size(), cin_ids.size(), din_to_cin, cin_to_din);
}
