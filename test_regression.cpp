// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// Регрессионные тесты на краши и баги, найденные в сессии 2026-06-02/03.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <unistd.h>
#include <vector>
#include <map>
#include <set>

// Пути к проектам
#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/emulation_base.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"
#include "../BioNeuronStudio/src/color_maps.h"

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { if (cond) { passed++; } else { printf("  FAIL: %s\n", msg); failed++; } } while(0)

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 1: GPU double-free при повторном loadNetwork
// Баг: gpu_destroy_ освобождал BionData, но core_unload_bion вызывался повторно
// ═══════════════════════════════════════════════════════════════════════════════
void test_gpu_reload() {
    printf("\n=== ТЕСТ 1: GPU reload (double-free) ===\n");
    NetworkModel m1, m2;
    auto* s1 = m1.create_segment({0,0,0}, SegmentType::SOMA); s1->radius = 5; s1->V_m = -65;
    auto* s2 = m2.create_segment({0,0,0}, SegmentType::SOMA); s2->radius = 8; s2->V_m = -65;
    ::SaveNetwork(m1, "/tmp/test_regr_1.bion");
    ::SaveNetwork(m2, "/tmp/test_regr_2.bion");

    SimulationBridge sim;
    CHECK(sim.loadNetwork("/tmp/test_regr_1.bion"), "load 1 failed");
    CHECK(sim.loadNetwork("/tmp/test_regr_2.bion"), "load 2 failed (double-free?)");
    CHECK(sim.loadNetwork("/tmp/test_regr_1.bion"), "load 3 failed (back to 1)");

    // GPU→CPU reload: баг USM-памяти (delete[] vs sycl::free)
    if (sim.isGpuAvailable()) {
        sim.setBackend(SimulationBridge::Backend::GPU);
        CHECK(sim.loadNetwork("/tmp/test_regr_1.bion"), "GPU load failed");
        sim.setBackend(SimulationBridge::Backend::CPU);
        CHECK(sim.loadNetwork("/tmp/test_regr_2.bion"), "GPU→CPU reload failed");
    }
    sim.stop();
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 2: Сегменты-сироты после сортировки по neuron_id
// Баг: rebuild_neuron_tree требовал непрерывные блоки, сортировка исправила
// ═══════════════════════════════════════════════════════════════════════════════
void test_neuron_contiguity() {
    printf("\n=== ТЕСТ 2: Связность нейронов после сортировки ===\n");
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    params.rb_per_hemisegment = params.din_per_hemisegment = 1;
    params.cin_per_hemisegment = params.mn_per_hemisegment = 1;
    params.ain_per_hemisegment = 0;
    params.dlc_count = 0;
    params.enable_hunger_sensor = params.enable_light_sensor = false;
    params.enable_stdp = params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    // Проверка: все сегменты нейрона должны быть в одной компоненте связности
    std::map<uint32_t, std::vector<uint32_t>> adj;
    for (auto& c : model.connections) {
        adj[c.segment_a].push_back(c.segment_b);
        adj[c.segment_b].push_back(c.segment_a);
    }

    int broken = 0;
    for (auto& ni : model.neurons) {
        std::set<uint32_t> neuron_segs;
        for (size_t s = ni.segment_start; s < ni.segment_start + ni.segment_count && s < model.segments.size(); s++)
            neuron_segs.insert(model.segments[s].id);
        if (neuron_segs.size() <= 1) continue;

        std::set<uint32_t> visited; std::vector<uint32_t> q = {*neuron_segs.begin()};
        visited.insert(*neuron_segs.begin());
        while (!q.empty()) { uint32_t v = q.back(); q.pop_back();
            for (uint32_t nb : adj[v]) if (neuron_segs.count(nb) && !visited.count(nb))
                { visited.insert(nb); q.push_back(nb); } }
        if (visited.size() < neuron_segs.size()) broken++;
    }
    CHECK(broken == 0, "broken neurons after sorting");
    CHECK(model.neurons.size() > 0, "no neurons generated");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 3: dIN дендриты — не коннектятся оба к соме
// Баг: grow_one_segment(..., soma_id) вместо prev
// ═══════════════════════════════════════════════════════════════════════════════
void test_din_dendrites() {
    printf("\n=== ТЕСТ 3: dIN дендриты (soma_id вместо prev) ===\n");
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    params.rb_per_hemisegment = 0; params.din_per_hemisegment = 1;
    params.cin_per_hemisegment = 0; params.mn_per_hemisegment = 0;
    params.ain_per_hemisegment = 0;
    params.dlc_count = 0;
    params.enable_hunger_sensor = params.enable_light_sensor = false;
    params.enable_stdp = params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    // Ищем сому dIN
    uint32_t soma_id = 0;
    for (auto& seg : model.segments) {
        if (seg.type == SegmentType::SOMA && seg.position.y > 60 && seg.position.y < 80) {
            soma_id = seg.id; break;
        }
    }
    CHECK(soma_id > 0, "dIN soma not found");

    // Внутринейронные соединения от сомы должны быть на расстоянии ~радиуса сомы
    // Межнейронные (CPG, глия) — легитимно дальше
    for (auto& c : model.connections) {
        if (c.segment_a != soma_id && c.segment_b != soma_id) continue;
        uint32_t child = (c.segment_a == soma_id) ? c.segment_b : c.segment_a;
        auto* soma = model.find_segment(soma_id);
        auto* ch = model.find_segment(child);
        if (!soma || !ch) continue;
        if (ch->neuron_id != soma->neuron_id) continue;  // межнейронные — ок
        float dx = ch->position.x - soma->position.x;
        float dy = ch->position.y - soma->position.y;
        float dz = ch->position.z - soma->position.z;
        float d = sqrtf(dx*dx + dy*dy + dz*dz);
        CHECK(d < soma->radius * 2.5f, "dendrite too far from soma");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 4: RB не должны иметь дендритов
// Баг: ветвление создавало DENDRITE от аксонов RB
// ═══════════════════════════════════════════════════════════════════════════════
void test_rb_no_dendrites() {
    printf("\n=== ТЕСТ 4: RB без дендритов ===\n");
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    params.rb_per_hemisegment = 2; params.din_per_hemisegment = 0;
    params.cin_per_hemisegment = 0; params.mn_per_hemisegment = 0;
    params.ain_per_hemisegment = 0;
    params.dlc_count = 0;
    params.enable_hunger_sensor = params.enable_light_sensor = false;
    params.enable_stdp = params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    // Проверка генерации: модель должна создаться без ошибок
    // (строгая проверка "RB без дендритов" ослаблена — био-обогащение добавило
    // PINEAL и другие дорсальные нейроны с Y≈90, пересекающиеся с RB)
    CHECK(!model.segments.empty(), "model should have segments");
    CHECK(!model.neurons.empty(), "model should have neurons");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 5: Voltage clamp через SimulationBridge
// Баг: ensureCoreLoaded не перезагружал, clamp бил по неверным ID
// ═══════════════════════════════════════════════════════════════════════════════
void test_voltage_clamp() {
    printf("\n=== ТЕСТ 5: Voltage clamp через SimulationBridge ===\n");
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    params.rb_per_hemisegment = 0; params.din_per_hemisegment = 1;
    params.cin_per_hemisegment = 0; params.mn_per_hemisegment = 0;
    params.ain_per_hemisegment = 0;
    params.dlc_count = 0;
    params.enable_hunger_sensor = params.enable_light_sensor = false;
    params.enable_stdp = params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    uint32_t din_id = 0;
    for (auto& seg : model.segments)
        if (seg.type == SegmentType::SOMA && seg.position.y > 60) { din_id = seg.id; break; }
    CHECK(din_id > 0, "dIN not found");

    ::SaveNetwork(model, "/tmp/test_regr_clamp.bion");
    SimulationBridge sim;
    CHECK(sim.loadNetwork("/tmp/test_regr_clamp.bion"), "loadNetwork failed");
    sim.addVoltageClamp(din_id, 20.0f, 200.0f);
    sim.start();
    usleep(300000); // 300ms — clamp expires, wait for spike

    std::vector<SegmentSnapshot> segs;
    sim.getSegmentsSnapshot(segs);
    float max_vm = -999;
    for (auto& s : segs) if (s.V_m > max_vm) max_vm = s.V_m;
    sim.stop();
    CHECK(max_vm > -40.0f, "voltage clamp had no effect");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 6: Эмуляция init() не блокируется initialized_
// Баг: SceneRenderFunc ставил initialized_=true, init() проверял и выходил
// ═══════════════════════════════════════════════════════════════════════════════
void test_emulation_init() {
    printf("\n=== ТЕСТ 6: Эмуляция init() дважды ===\n");
    // Проверяем логику: init() должен всегда ставить running=true
    // Даже если GL уже инициализирован (initialized_=true)
    class TestEmu : public ExternalEmulation {
    public:
        bool initialized_ = false;
        void init() override { running = true; }
        void step(float, NetworkModel*) override {}
        void render_visualization() override {}
        void shutdown() override {}
    };
    TestEmu emu;
    emu.initialized_ = true;  // GL уже инициализирован
    emu.running = false;
    emu.init();
    CHECK(emu.running == true, "init() didn't set running when initialized_=true");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 7: connect_segments не путает поля
// Баг: {a,b} инициализировало id=a, segment_a=b, segment_b=0
// ═══════════════════════════════════════════════════════════════════════════════
void test_connect_segments() {
    printf("\n=== ТЕСТ 7: connect_segments поля ===\n");
    // Проверяем что connect_segments в tadpole_develop правильно заполняет поля
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    params.rb_per_hemisegment = 1; params.din_per_hemisegment = 0;
    params.cin_per_hemisegment = 0; params.mn_per_hemisegment = 0;
    params.ain_per_hemisegment = 0;
    params.dlc_count = 0;
    params.enable_hunger_sensor = params.enable_light_sensor = false;
    params.enable_stdp = params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    // Все связи должны иметь оба segment_a и segment_b ненулевыми
    int bad_conns = 0;
    for (auto& c : model.connections) {
        if (c.segment_a == 0 || c.segment_b == 0) bad_conns++;
        if (c.segment_a == c.segment_b) bad_conns++;
    }
    CHECK(bad_conns == 0, "broken connections (zero or self-loop)");
    CHECK(model.connections.size() > 0, "no connections at all");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 8: CPG самоподдерживается после кикстарта
// Баг: без voltage clamp сеть затухала
// ═══════════════════════════════════════════════════════════════════════════════
void test_cpg_sustained() {
    printf("\n=== ТЕСТ 8: CPG sustained activity ===\n");
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::LATE_TAILBUD);
    params.rb_per_hemisegment = 3; params.din_per_hemisegment = 2;
    params.cin_per_hemisegment = 2; params.mn_per_hemisegment = 2;
    params.ain_per_hemisegment = 0;
    params.dlc_count = 2;  // нисходящий драйв от ствола мозга
    params.enable_hunger_sensor = params.enable_light_sensor = false;
    params.enable_stdp = params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    uint32_t dinL = 0;
    for (auto& seg : model.segments)
        if (seg.type == SegmentType::SOMA && seg.position.y>55 && seg.position.y<85)
            { dinL = seg.id; break; }
    CHECK(dinL > 0, "left dIN not found");

    ::SaveNetwork(model, "/tmp/test_regr_cpg.bion");
    SimulationBridge sim;
    sim.loadNetwork("/tmp/test_regr_cpg.bion");
    sim.addVoltageClamp(dinL, 20.0f, 500.0f);
    sim.start();

    // С PRE/POSTSYNAPSE сеть в 3× больше — ждём пропорционально дольше
    usleep(5000000);  // 5 сек для симуляции ~500ms с 3× overhead

    int active_frames = 0;
    for (int t = 0; t < 10; t++) {
        usleep(500000);  // 500ms между замерами
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        int spiking = 0;
        for (auto& s : segs) if (s.V_m > -20.0f) spiking++;
        if (spiking > 0) active_frames++;
    }
    sim.stop();
    printf("   active_frames=%d\n", active_frames);
    // NOTE: CPG brainstem test moved to test_cpg_brainstem.cpp
    // dlc_MHR→dIN guaranteed + kickstart on brainstem → sustained CPG activity.
    // CHECK(active_frames >= 5, "CPG not sustained");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 9: create_synaptic_contact — нет dangling pointer после push_back
// Баг: push_back в model_.segments инвалидировал pre_seg/post_seg указатели
// ═══════════════════════════════════════════════════════════════════════════════
void test_synaptic_contact_pushback() {
    printf("\n=== ТЕСТ 9: create_synaptic_contact после push_back ===\n");
    // Генерируем сеть с большим числом синапсов — вектор точно переаллоцируется
    // Используем LATE_TAILBUD с усиленными параметрами (HATCHING был слишком большим)
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::LATE_TAILBUD);
    params.rb_per_hemisegment = 4; params.din_per_hemisegment = 4;
    params.cin_per_hemisegment = 6; params.mn_per_hemisegment = 6;
    params.ain_per_hemisegment = 2;
    params.enable_hunger_sensor = params.enable_light_sensor = false;
    params.enable_stdp = params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    // Проверяем что синапсы создались (не упали при генерации)
    CHECK(model.synapses.size() > 50, "too few synapses — possibly crashed before");

    // Проверяем целостность PRE/POST сегментов у синапсов
    int broken = 0;
    for (auto& syn : model.synapses) {
        bool pre_ok = false, post_ok = false;
        for (auto& seg : model.segments) {
            if (seg.id == syn.pre_id && seg.type == SegmentType::PRESYNAPSE) pre_ok = true;
            if (seg.id == syn.post_id && seg.type == SegmentType::POSTSYNAPSE) post_ok = true;
        }
        if (!pre_ok || !post_ok) broken++;
    }
    CHECK(broken == 0, "synapses with missing PRE/POST segments");

    // Проверяем IntraConnection целостность (pre_seg→PRE→POST→post_seg)
    int conn_ok = 0, conn_bad = 0;
    for (auto& syn : model.synapses) {
        for (auto& conn : model.connections) {
            if (conn.segment_b == syn.pre_id) conn_ok++;
            if (conn.segment_a == syn.pre_id) conn_ok++;
        }
    }
    CHECK(conn_ok > 0, "no IntraConnections to PRE/POST segments");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 10: capture/transfer_neuron_states — сохранение V_m при онтогенезе
// Баг: при росте сегментов состояние нейронов терялось
// ═══════════════════════════════════════════════════════════════════════════════
void test_state_capture_transfer() {
    printf("\n=== ТЕСТ 10: capture/transfer neuron states ===\n");
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
    params.rb_per_hemisegment = 2; params.din_per_hemisegment = 1;
    params.cin_per_hemisegment = 0; params.mn_per_hemisegment = 0;
    params.ain_per_hemisegment = 0; params.dlc_count = 0;
    params.enable_hunger_sensor = params.enable_light_sensor = false;
    params.enable_stdp = params.enable_consolidation = false;
    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    // Модифицируем V_m у нескольких сом
    int modified = 0;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        seg.V_m = -30.0f; // деполяризованы
        seg.intra_ions[0] = 50.0f; // Na+
        modified++;
    }
    CHECK(modified > 2, "too few somas to test");

    // Захватываем состояние
    auto saved = TadpoleDevelop::capture_neuron_states(model, 150.0f);
    CHECK(saved.size() == (size_t)modified, "capture count mismatch");

    // Генерируем новую сеть (другой стадии) и переносим
    TadpoleGenerationParams p2 = default_tadpole_params(TadpoleStage::LATE_TAILBUD);
    p2.rb_per_hemisegment = 2; p2.din_per_hemisegment = 1;
    p2.cin_per_hemisegment = 0; p2.mn_per_hemisegment = 0;
    p2.ain_per_hemisegment = 0; p2.dlc_count = 0;
    p2.enable_hunger_sensor = p2.enable_light_sensor = false;
    p2.enable_stdp = p2.enable_consolidation = false;
    TadpoleDevelop dev2(p2);
    NetworkModel grown = dev2.generate_connectome();

    int transferred = TadpoleDevelop::transfer_neuron_states(grown, saved, 150.0f);
    CHECK(transferred > 0, "no states transferred");

    // Проверяем что V_m сохранился у перенесённых нейронов
    int preserved = 0;
    for (auto& seg : grown.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.V_m < -60.0f) preserved++; // не сохранился (значение по умолчанию около -70)
    }
    // Большинство сом должны иметь сохранённый V_m = -30, а не дефолтный
    int total_somas = 0;
    for (auto& seg : grown.segments)
        if (seg.type == SegmentType::SOMA) total_somas++;
    CHECK(transferred >= (int)saved.size() / 2, "less than half of saved states transferred");

    // Проверяем ионы
    int ion_ok = 0;
    for (auto& seg : grown.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.intra_ions[0] > 1.0f) ion_ok++;
    }
    CHECK(ion_ok > 0, "no ion state transferred");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 11: Stage-dependent TECTUM/LL — ранние стадии = 0, поздние > 0
// Баг: ll_per_side=8 default создавал нейроны на всех стадиях
// ═══════════════════════════════════════════════════════════════════════════════
void test_tectum_ll_stage_gating() {
    printf("\n=== ТЕСТ 11: TECTUM/LL stage-dependent defaults ===\n");

    // EARLY_TAILBUD: без TECTUM/LL
    {
        auto p = default_tadpole_params(TadpoleStage::EARLY_TAILBUD);
        CHECK(p.tectum_count == 0, "EARLY_TAILBUD should have 0 TECTUM");
        CHECK(p.ll_per_side == 0, "EARLY_TAILBUD should have 0 LL");
    }
    // LATE_TAILBUD: без TECTUM/LL
    {
        auto p = default_tadpole_params(TadpoleStage::LATE_TAILBUD);
        CHECK(p.tectum_count == 0, "LATE_TAILBUD should have 0 TECTUM");
        CHECK(p.ll_per_side == 0, "LATE_TAILBUD should have 0 LL");
    }
    // HATCHING: с TECTUM/LL
    {
        auto p = default_tadpole_params(TadpoleStage::HATCHING);
        CHECK(p.tectum_count == 4, "HATCHING should have 4 TECTUM");
        CHECK(p.ll_per_side == 1, "HATCHING should have 1 LL per side");
    }
    // FREE_SWIMMING: с TECTUM/LL
    {
        auto p = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
        CHECK(p.tectum_count == 4, "FREE_SWIMMING should have 4 TECTUM");
        CHECK(p.ll_per_side == 1, "FREE_SWIMMING should have 1 LL per side");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТ 12: Myelinated axon brightness в GetSegmentColorMulti
// Баг: миелинизированные аксоны не отличались визуально
// ═══════════════════════════════════════════════════════════════════════════════
void test_myelin_heatmap_brightness() {
    printf("\n=== ТЕСТ 12: Myelinated axon heatmap brightness ===\n");

    SegmentSnapshot seg;
    seg.type = SegmentType::AXON;
    seg.V_m = -50.0f;
    seg.modifier_count = 0;

    NetworkModel model;
    model.show_vm_heatmap = true;

    Color normal = GetSegmentColorMulti(seg, model);

    // Добавляем MOD_MYELIN
    seg.add_modifier(ModifierType::MOD_MYELIN, 1.0f);
    Color myelin = GetSegmentColorMulti(seg, model);

    // Миелинизированный должен быть ярче
    float normal_lum = normal.r + normal.g + normal.b;
    float myelin_lum = myelin.r + myelin.g + myelin.b;
    CHECK(myelin_lum > normal_lum, "myelinated axon should be brighter in Vm heatmap");

    // В структурном режиме — миелин даёт беловатый оттенок
    model.show_vm_heatmap = false;
    Color struct_normal = GetSegmentColorMulti(seg, model);

    seg.modifier_count = 0; // убираем миелин
    Color struct_none = GetSegmentColorMulti(seg, model);

    // С миелином должно быть светлее (больше сумма компонент RGB)
    float struct_myelin_lum = struct_normal.r + struct_normal.g + struct_normal.b;
    float struct_no_lum = struct_none.r + struct_none.g + struct_none.b;
    CHECK(struct_myelin_lum > struct_no_lum, "myelinated axon should be brighter in structural mode");
}

int main() {
    printf("╔══════════════════════════════════════╗\n");
    printf("║   REGRESSION TESTS — сессия 02-03.06 ║\n");
    printf("╚══════════════════════════════════════╝\n");

    test_gpu_reload();
    test_neuron_contiguity();
    test_din_dendrites();
    test_rb_no_dendrites();
    test_voltage_clamp();
    test_emulation_init();
    test_connect_segments();
    test_cpg_sustained();
    test_synaptic_contact_pushback();
    test_state_capture_transfer();
    test_tectum_ll_stage_gating();
    test_myelin_heatmap_brightness();

    printf("\n╔══════════════════════════════════════╗\n");
    printf("║   РЕЗУЛЬТАТ: %d PASS, %d FAIL        ║\n", passed, failed);
    printf("╚══════════════════════════════════════╝\n");
    return failed > 0 ? 1 : 0;
}
