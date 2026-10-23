// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_cell_connectivity.cpp — Тесты монолитности нейронов библиотеки
// Каждый нейрон должен создаваться без оторванных сегментов:
// все сегменты достижимы из сомы через граф IntraConnection.

#include <gtest/gtest.h>
#include <cstdio>
#include <vector>
#include <unordered_set>
#include <queue>
#include "../BioNeuronStudio/src/neuron_library.h"
#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"

// Проверка связности: все сегменты должны быть достижимы из сомы
static int countDisconnected(const NetworkModel& model) {
    if (model.segments.empty()) return 0;

    // Найти сому (первый сегмент типа SOMA)
    uint32_t soma_id = 0;
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::SOMA) { soma_id = seg.id; break; }
    }
    if (soma_id == 0) return (int)model.segments.size(); // нет сомы — все оторваны

    // Построить граф смежности из IntraConnection
    std::unordered_map<uint32_t, std::vector<uint32_t>> graph;
    for (auto& conn : model.connections) {
        graph[conn.segment_a].push_back(conn.segment_b);
        graph[conn.segment_b].push_back(conn.segment_a);
    }

    // BFS от сомы
    std::unordered_set<uint32_t> visited;
    std::queue<uint32_t> q;
    q.push(soma_id);
    visited.insert(soma_id);
    while (!q.empty()) {
        uint32_t cur = q.front(); q.pop();
        for (uint32_t nb : graph[cur]) {
            if (!visited.count(nb)) { visited.insert(nb); q.push(nb); }
        }
    }

    return (int)(model.segments.size() - visited.size());
}

// Вспомогательная функция для создания модели из фабрики нейрона
static NetworkModel buildFromBlueprint(NeuronBlueprint bp) {
    NetworkModel model;
    float sr = bp.soma_radius_um;

    SegmentSnapshot soma;
    soma.id = 1;
    soma.type = SegmentType::SOMA;
    soma.position = {0, 0, 0};
    soma.radius = sr;
    soma.length = sr * 2.0f;
    soma.direction = {0, 1, 0};
    soma.V_m = bp.resting_V_m;
    soma.prev_V_m = bp.resting_V_m;
    soma.neuron_id = 1;
    for (int i = 0; i < ION_COUNT; i++) soma.intra_ions[i] = (i == 1) ? 140.0f : ((i == 0 || i == 3) ? 10.0f : 0.0001f);
    for (int i = 0; i < bp.soma_modifier_count; i++)
        soma.add_modifier(bp.soma_modifiers[i].type, bp.soma_modifiers[i].density);
    model.segments.push_back(soma);

    uint32_t next_id = 2;

    // Аксон
    if (bp.axon.count > 0) {
        float step = bp.axon.length_um;
        Vec3 start = {bp.axon.dir_x * sr, bp.axon.dir_y * sr, bp.axon.dir_z * sr};
        uint32_t prev = 1;
        for (int i = 0; i < bp.axon.count; i++) {
            SegmentSnapshot ax;
            ax.id = next_id++;
            ax.type = SegmentType::AXON;
            ax.position = {start.x + bp.axon.dir_x * step * i,
                           start.y + bp.axon.dir_y * step * i,
                           start.z + bp.axon.dir_z * step * i};
            ax.radius = bp.axon.radius_um;
            ax.length = step;
            ax.direction = {bp.axon.dir_x, bp.axon.dir_y, bp.axon.dir_z};
            ax.V_m = bp.resting_V_m; ax.prev_V_m = bp.resting_V_m;
            ax.neuron_id = 1;
            for (int j = 0; j < ION_COUNT; j++) ax.intra_ions[j] = soma.intra_ions[j];
            for (int j = 0; j < bp.axon.modifier_count; j++)
                ax.add_modifier(bp.axon.modifiers[j].type, bp.axon.modifiers[j].density);
            model.segments.push_back(ax);
            IntraConnection c; c.id = model.connections.size() + 1; c.segment_a = prev; c.segment_b = ax.id;
            model.connections.push_back(c);
            prev = ax.id;
        }
    }

    // Дендриты
    for (int d = 0; d < bp.dendrite_count; d++) {
        auto& dd = bp.dendrites[d];
        if (dd.count <= 0) continue;
        float step = dd.length_um;
        Vec3 start = {dd.dir_x * sr, dd.dir_y * sr, dd.dir_z * sr};
        uint32_t prev = 1;
        for (int i = 0; i < dd.count; i++) {
            SegmentSnapshot dn;
            dn.id = next_id++;
            dn.type = SegmentType::DENDRITE;
            dn.position = {start.x + dd.dir_x * step * i,
                           start.y + dd.dir_y * step * i,
                           start.z + dd.dir_z * step * i};
            dn.radius = dd.radius_um;
            dn.length = step;
            dn.direction = {dd.dir_x, dd.dir_y, dd.dir_z};
            dn.V_m = bp.resting_V_m; dn.prev_V_m = bp.resting_V_m;
            dn.neuron_id = 1;
            for (int j = 0; j < ION_COUNT; j++) dn.intra_ions[j] = soma.intra_ions[j];
            for (int j = 0; j < dd.modifier_count; j++)
                dn.add_modifier(dd.modifiers[j].type, dd.modifiers[j].density);
            model.segments.push_back(dn);
            IntraConnection c; c.id = model.connections.size() + 1; c.segment_a = prev; c.segment_b = dn.id;
            model.connections.push_back(c);
            prev = dn.id;
        }
    }

    NeuronInfo ni; ni.id = 1;
    model.neurons.push_back(ni);
    return model;
}

// Макрос для тестирования одного нейрона
#define TEST_CONNECTIVITY(tname, factory) \
    TEST(CellConnectivity, tname) { \
        auto bp = factory(); \
        auto model = buildFromBlueprint(bp); \
        int disc = countDisconnected(model); \
        EXPECT_EQ(disc, 0) << #tname << ": " << disc << " оторванных сегментов из " << model.segments.size(); \
        if (disc == 0) printf("[CONN-OK] %s: %zu segs, %zu conns\n", \
               #tname, model.segments.size(), model.connections.size()); \
    }

// ═══ Tadpole (20 типов) ═══
TEST_CONNECTIVITY(Tadpole_RB,               NeuronLibrary::Tadpole::RB);
TEST_CONNECTIVITY(Tadpole_dIN,              NeuronLibrary::Tadpole::dIN);
TEST_CONNECTIVITY(Tadpole_cIN,              NeuronLibrary::Tadpole::cIN);
TEST_CONNECTIVITY(Tadpole_mn,               NeuronLibrary::Tadpole::mn);
TEST_CONNECTIVITY(Tadpole_HYPOTHALAMIC,    NeuronLibrary::Tadpole::HYPOTHALAMIC);
TEST_CONNECTIVITY(Tadpole_PINEAL,          NeuronLibrary::Tadpole::PINEAL);
TEST_CONNECTIVITY(Tadpole_aIN,              NeuronLibrary::Tadpole::aIN);
TEST_CONNECTIVITY(Tadpole_dlc_MHR,         NeuronLibrary::Tadpole::dlc_MHR);
TEST_CONNECTIVITY(Tadpole_TECTUM,          NeuronLibrary::Tadpole::TECTUM);
TEST_CONNECTIVITY(Tadpole_LL,              NeuronLibrary::Tadpole::LL);
TEST_CONNECTIVITY(Tadpole_ORN,             NeuronLibrary::Tadpole::ORN);
TEST_CONNECTIVITY(Tadpole_GLIA_RADIAL,     NeuronLibrary::Tadpole::GLIA_RADIAL);
TEST_CONNECTIVITY(Tadpole_DS_IN,           NeuronLibrary::Tadpole::DS_IN);
TEST_CONNECTIVITY(Tadpole_SER,             NeuronLibrary::Tadpole::SER);
TEST_CONNECTIVITY(Tadpole_AVOID,           NeuronLibrary::Tadpole::AVOID);
TEST_CONNECTIVITY(Tadpole_AUDITORY_SENSOR,  NeuronLibrary::Tadpole::AUDITORY_SENSOR);
TEST_CONNECTIVITY(Tadpole_OTOLITH,         NeuronLibrary::Tadpole::OTOLITH);
TEST_CONNECTIVITY(Tadpole_SEMICIRCULAR,    NeuronLibrary::Tadpole::SEMICIRCULAR);
TEST_CONNECTIVITY(Tadpole_ELECTRORECEPTOR, NeuronLibrary::Tadpole::ELECTRORECEPTOR);
TEST_CONNECTIVITY(Tadpole_MAGNETORECEPTOR, NeuronLibrary::Tadpole::MAGNETORECEPTOR);

// ═══ Human (выборочно — ключевые типы) ═══
TEST_CONNECTIVITY(Human_Pyramidal_L5_thick,  NeuronLibrary::Human::Pyramidal_L5_thick);
TEST_CONNECTIVITY(Human_MSN_D1,              NeuronLibrary::Human::MSN_D1);
TEST_CONNECTIVITY(Human_Purkinje,            NeuronLibrary::Human::Purkinje);
TEST_CONNECTIVITY(Human_CA1_Pyramidal,       NeuronLibrary::Human::CA1_Pyramidal);
TEST_CONNECTIVITY(Human_AlphaMotorNeuron,    NeuronLibrary::Human::AlphaMotorNeuron);
TEST_CONNECTIVITY(Human_Thalamocortical,     NeuronLibrary::Human::Thalamocortical);
TEST_CONNECTIVITY(Human_Cholinergic_TAN,     NeuronLibrary::Human::Cholinergic_TAN);
TEST_CONNECTIVITY(Human_Dopaminergic_SNc,    NeuronLibrary::Human::Dopaminergic_SNc);
TEST_CONNECTIVITY(Human_Serotonergic_Raphe,  NeuronLibrary::Human::Serotonergic_Raphe);
TEST_CONNECTIVITY(Human_Noradrenergic_LC,    NeuronLibrary::Human::Noradrenergic_LC);
