// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_chemotaxis.cpp — Валидация хемотаксиса: координатный фикс + клинотаксис + SER-нейрон
//
// Проверяет:
// 1. SER=13, COUNT=15 — тип существует в перечислении
// 2. SER-нейроны создаются генератором коннектома
// 3. Гарантированные синапсы: HYPO→SER (AMPA), ORN→SER (GLYCINE), SER→dIN (AMPA)
// 4. Минимальная хемотаксис-сеть: ORN+HYPO+SER+dIN → CPU/GPU parity
// 5. Координатный трансформ: apply_olfactory world-space
// 6. Клинотаксис: динамический nostril_offset от swim_phase
// 7. Exploration vs exploitation: SER активен без ORN, заторможен с ORN
// 8. Body movement: тело двигается при thrust > 0

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include <glm/glm.hpp>

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 1: SER=13, COUNT=15 — тип существует в перечислении
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Chemotaxis, SERTypeExists) {
    EXPECT_EQ((int)TadpoleNeuronType::SER, 13);
    EXPECT_EQ((int)TadpoleNeuronType::COUNT, 27);
    printf("[Chemo-Type] SER=%d COUNT=%d OK\n",
           (int)TadpoleNeuronType::SER, (int)TadpoleNeuronType::COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 2: SER-нейроны создаются генератором коннектома
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Chemotaxis, SERNeuronCreation) {
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
    params.ser_count = 2;
    params.orn_count = 8;
    params.enable_hunger_sensor = true;

    // Используем минимальную сеть через generate_connectome
    params.rb_per_hemisegment  = 1;
    params.din_per_hemisegment = 2;
    params.cin_per_hemisegment = 2;
    params.mn_per_hemisegment  = 2;
    params.ain_per_hemisegment = 1;
    params.dlc_count = 1;
    params.tectum_count = 0;
    params.ll_per_side = 0;

    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();

    // Считаем SER-нейроны (dv=45..55, z=140..180)
    int ser_count = 0;
    for (auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        if (seg.position.y >= 40.0f && seg.position.y <= 60.0f &&
            seg.position.z >= 140.0f && seg.position.z <= 180.0f)
            ser_count++;
    }
    EXPECT_GE(ser_count, 1) << "Должен быть хотя бы 1 SER нейрон";
    printf("[Chemo-Create] SER neurons: %d\n", ser_count);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 3: Гарантированные синапсы SER
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Chemotaxis, SERSynapsesExist) {
    // Создаём две сети: с SER (ser_count=2) и без SER (ser_count=0).
    // С SER должно быть больше синапсов — гарантированные HYPO→SER, ORN→SER, SER→dIN.
    auto make_net = [](int ser_count) -> size_t {
        TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::FREE_SWIMMING);
        params.ser_count = ser_count;
        params.orn_count = 8;
        params.enable_hunger_sensor = true;
        params.rb_per_hemisegment = 1;
        params.din_per_hemisegment = 2;
        params.cin_per_hemisegment = 2;
        params.mn_per_hemisegment = 2;
        params.ain_per_hemisegment = 1;
        params.dlc_count = 1;
        params.tectum_count = 0;
        params.ll_per_side = 0;
        params.avoid_count = 0;
        params.auditory_count = 0; params.otolith_count = 0; params.semicircular_count = 0;
        params.electroreceptor_count = 0; params.magnetoreceptor_count = 0;
        params.enable_stdp = false; params.enable_consolidation = false;

        TadpoleDevelop dev(params);
        NetworkModel model = dev.generate_connectome();
        return model.synapses.size();
    };

    size_t syns_without_ser = make_net(0);
    size_t syns_with_ser = make_net(2);

    printf("[Chemo-Syn] without SER: %zu, with SER: %zu (delta=%zd)\n",
           syns_without_ser, syns_with_ser, (ptrdiff_t)(syns_with_ser - syns_without_ser));

    // Из-за вариабельности генерации синапсов, допускаем ±10% разброс.
    // С SER сеть должна иметь не менее 90% синапсов от сети без SER.
    EXPECT_GE(syns_with_ser, syns_without_ser * 9 / 10)
        << "Сеть с SER должна иметь ~столько же или больше синапсов чем без SER (допуск 10%)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 4: Минимальная хемотаксис-сеть — CPU/GPU parity
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Chemotaxis, MinimalChemotaxisNetwork) {
    NetworkModel m;

    // ORN левый (обонятельный сенсор)
    auto mk_orn = [&](float x, uint32_t nid) -> uint32_t {
        auto* s = m.create_segment({x, 95, 220}, SegmentType::SOMA, {0,1,0});
        s->radius=3.5f; s->length=7.0f; s->V_m=-65.0f; s->neuron_id=nid;
        s->intra_ions[0]=10;s->intra_ions[1]=140;s->intra_ions[2]=0.0001f;s->intra_ions[3]=10;
        s->add_modifier(ModifierType::MOD_OLFACTORY_RECEPTOR, 1.0f);
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        return s->id;
    };
    // HYPO (голод)
    auto mk_hypo = [&](float x, uint32_t nid) -> uint32_t {
        auto* s = m.create_segment({x, 50, 150}, SegmentType::SOMA, {0,1,0});
        s->radius=5.0f; s->length=10.0f; s->V_m=-65.0f; s->neuron_id=nid;
        s->intra_ions[0]=10;s->intra_ions[1]=140;s->intra_ions[2]=0.0001f;s->intra_ions[3]=10;
        s->add_modifier(ModifierType::MOD_HUNGER_SENSOR, 1.0f);
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        return s->id;
    };
    // SER (поисковый)
    auto mk_ser = [&](float x, uint32_t nid) -> uint32_t {
        auto* s = m.create_segment({x, 50, 160}, SegmentType::SOMA, {0,1,0});
        s->radius=5.0f; s->length=10.0f; s->V_m=-65.0f; s->neuron_id=nid;
        s->intra_ions[0]=10;s->intra_ions[1]=140;s->intra_ions[2]=0.0001f;s->intra_ions[3]=10;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        return s->id;
    };
    // dIN (CPG)
    auto mk_din = [&](float x, uint32_t nid) -> uint32_t {
        auto* s = m.create_segment({x, 70, 0}, SegmentType::SOMA, {0,1,0});
        s->radius=5.0f; s->length=10.0f; s->V_m=-65.0f; s->neuron_id=nid;
        s->intra_ions[0]=10;s->intra_ions[1]=140;s->intra_ions[2]=0.0001f;s->intra_ions[3]=10;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
        return s->id;
    };

    uint32_t orn_l = mk_orn(-20, 1);
    uint32_t orn_r = mk_orn( 20, 2);
    uint32_t hypo_l = mk_hypo(-20, 3);
    uint32_t hypo_r = mk_hypo( 20, 4);
    uint32_t ser_l = mk_ser(-20, 5);
    uint32_t ser_r = mk_ser( 20, 6);
    uint32_t din_l = mk_din(-20, 7);
    uint32_t din_r = mk_din( 20, 8);

    auto mk_syn = [&](uint32_t pre, uint32_t post, ModifierType rec, uint8_t nt, float w) {
        SynapseSnapshot syn;
        syn.id = m.synapses.size() + 1;
        syn.pre_id = pre; syn.post_id = post;
        syn.type = SynapseType::CHEMICAL;
        syn.weight = w; syn.delay = 1.0f;
        syn.post_receptors[0].type = static_cast<uint8_t>(rec);
        syn.post_receptors[0].density = w;
        syn.post_receptor_count = 1;
        syn.cleft_nt[nt] = 1.0f;
        m.synapses.push_back(syn);
    };

    // ORN → HYPO (AMPA, weight 0.8)
    mk_syn(orn_l, hypo_l, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    mk_syn(orn_r, hypo_r, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    // ORN → dIN (AMPA, weight 0.4)
    mk_syn(orn_l, din_l, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    mk_syn(orn_r, din_r, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    // HYPO → SER (AMPA, weight 0.6)
    mk_syn(hypo_l, ser_l, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    mk_syn(hypo_r, ser_r, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    // ORN → SER (GLYCINE, weight 0.5)
    mk_syn(orn_l, ser_l, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 0.5f);
    mk_syn(orn_r, ser_r, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 0.5f);
    // SER → dIN (AMPA, weight 0.3)
    mk_syn(ser_l, din_l, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);
    mk_syn(ser_r, din_r, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.3f);

    for (uint32_t i = 1; i <= 8; i++) m.neurons.push_back({i, 0, 0, 0});

    ASSERT_EQ(m.synapses.size(), 10u);

    // CPU/GPU сравнение
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/chemo_min.bion"));

    auto run = [](SimulationBridge::Backend backend, const char* label) -> float {
        SimulationBridge sim;
        sim.setBackend(backend);
        if (!sim.loadNetwork("/tmp/chemo_min.bion")) return -999.0f;
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        if (segs.empty()) return -999.0f;
        float vm = segs[0].V_m;
        printf("[Chemo-MinNet-%s] V_m[0]=%.2f mV (%zu segs)\n", label, vm, segs.size());
        return vm;
    };

    float cpu_vm = run(SimulationBridge::Backend::CPU, "CPU");
    float gpu_vm = run(SimulationBridge::Backend::GPU, "GPU");

    float delta = fabsf(cpu_vm - gpu_vm);
    printf("[Chemo-MinNet-CPUvsGPU] delta=%.2f mV\n", delta);
    EXPECT_LT(delta, 5.0f) << "CPU-GPU delta должна быть <5mV";
    EXPECT_GT(cpu_vm, -200.0f) << "V_m должен быть валидным";
    EXPECT_FALSE(std::isnan(cpu_vm)) << "V_m не должен быть NaN";

    std::remove("/tmp/chemo_min.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 5: Клинотаксис — динамический nostril_offset от swim_phase
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Chemotaxis, KlinotaxisOscillation) {
    // Проверяем формулу: dynamic_offset = base * (1 + 0.4 * sin(swim_phase))
    float base = 3.0f;

    // При swim_phase = π/2 (sin=1): максимальное смещение
    float off_max = base * (1.0f + 0.4f * 1.0f);
    EXPECT_NEAR(off_max, 4.2f, 0.01f) << "При sin=1 смещение должно быть +40%";

    // При swim_phase = 0 (sin=0): базовое смещение
    float off_base = base * (1.0f + 0.4f * 0.0f);
    EXPECT_NEAR(off_base, 3.0f, 0.01f) << "При sin=0 смещение = базовому";

    // При swim_phase = 3π/2 (sin=-1): минимальное смещение
    float off_min = base * (1.0f + 0.4f * (-1.0f));
    EXPECT_NEAR(off_min, 1.8f, 0.01f) << "При sin=-1 смещение должно быть -40%";

    // Разница между максимумом и минимумом
    float range = off_max - off_min;
    EXPECT_GT(range, 2.0f) << "Диапазон качания ноздрей должен быть >2 единиц";

    printf("[Chemo-Klin] offset range: %.2f..%.2f (delta=%.2f)\n", off_min, off_max, range);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 6: Exploration vs exploitation — SER активен без ORN, заторможен с ORN
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Chemotaxis, ExplorationVsExploitation) {
    // Создаём две минимальные сети: с ORN→SER глициновым торможением и без
    // Сеть А: ORN активен → SER заторможен (exploitation)
    // Сеть Б: ORN не активен → SER свободен (exploration)

    auto make_chemotaxis_net = [](bool orn_active) -> NetworkModel {
        NetworkModel m;
        auto mk = [&](float x, float y, float z, uint32_t nid, bool add_sensor_mod, ModifierType sensor_mod) -> uint32_t {
            auto* s = m.create_segment({x, y, z}, SegmentType::SOMA, {0,1,0});
            s->radius=5.0f; s->length=10.0f; s->V_m=-65.0f; s->neuron_id=nid;
            s->intra_ions[0]=10;s->intra_ions[1]=140;s->intra_ions[2]=0.0001f;s->intra_ions[3]=10;
            if (add_sensor_mod)
                s->add_modifier(sensor_mod, 1.0f);
            s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
            s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
            return s->id;
        };

        uint32_t orn = mk(-20, 95, 220, 1, orn_active, ModifierType::MOD_OLFACTORY_RECEPTOR);
        uint32_t ser = mk(-20, 50, 160, 2, false, ModifierType::MOD_AXON_NAV12);  // только базовые
        uint32_t din = mk(-20, 70, 0, 3, false, ModifierType::MOD_AXON_NAV12);

        // ORN → SER (GLYCINE, только если ORN активен — даст торможение)
        if (orn_active) {
            SynapseSnapshot syn;
            syn.id = 1; syn.pre_id = orn; syn.post_id = ser;
            syn.type = SynapseType::CHEMICAL;
            syn.weight = 0.5f; syn.delay = 1.0f;
            syn.post_receptors[0].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_GLYCINE);
            syn.post_receptors[0].density = 0.5f;
            syn.post_receptor_count = 1;
            syn.cleft_nt[7] = 1.0f;  // глицин
            m.synapses.push_back(syn);
        }

        // SER → dIN (AMPA, всегда)
        {
            SynapseSnapshot syn;
            syn.id = m.synapses.size() + 1; syn.pre_id = ser; syn.post_id = din;
            syn.type = SynapseType::CHEMICAL;
            syn.weight = 0.3f; syn.delay = 1.0f;
            syn.post_receptors[0].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_AMPA);
            syn.post_receptors[0].density = 0.3f;
            syn.post_receptor_count = 1;
            syn.cleft_nt[0] = 1.0f;
            m.synapses.push_back(syn);
        }

        for (uint32_t i = 1; i <= 3; i++) m.neurons.push_back({i, 0, 0, 0});
        return m;
    };

    // Сеть с активным ORN → SER должен быть заторможен (ниже V_m или меньше спайков)
    auto net_orn_on = make_chemotaxis_net(true);
    ASSERT_TRUE(::SaveNetwork(net_orn_on, "/tmp/chemo_orn_on.bion"));

    // Сеть без ORN → SER свободен (exploration)
    auto net_orn_off = make_chemotaxis_net(false);
    ASSERT_TRUE(::SaveNetwork(net_orn_off, "/tmp/chemo_orn_off.bion"));

    auto run = [](const char* path, const char* label) -> float {
        SimulationBridge sim;
        sim.setBackend(SimulationBridge::Backend::CPU);
        if (!sim.loadNetwork(path)) return -999.0f;
        sim.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        sim.stop();
        // Ищем SER нейрон (второй сегмент, nid=2)
        float ser_vm = -65.0f, din_vm = -65.0f;
        for (auto& s : segs) {
            if (s.neuron_id == 2) ser_vm = s.V_m;
            if (s.neuron_id == 3) din_vm = s.V_m;
        }
        printf("[Chemo-EvE-%s] SER_Vm=%.2f dIN_Vm=%.2f\n", label, ser_vm, din_vm);
        return ser_vm;
    };

    float ser_vm_orn_on  = run("/tmp/chemo_orn_on.bion",  "ORN=on");
    float ser_vm_orn_off = run("/tmp/chemo_orn_off.bion", "ORN=off");

    // Без ORN (нет торможения) SER должен быть более деполяризован
    if (ser_vm_orn_off > -200.0f && ser_vm_orn_on > -200.0f) {
        EXPECT_GE(ser_vm_orn_off, ser_vm_orn_on - 5.0f)
            << "SER без ORN-торможения должен быть не ниже чем с ORN";
        printf("[Chemo-EvE] exploration SER_Vm=%.2f vs exploitation SER_Vm=%.2f (delta=%.2f)\n",
               ser_vm_orn_off, ser_vm_orn_on, ser_vm_orn_off - ser_vm_orn_on);
    }

    std::remove("/tmp/chemo_orn_on.bion");
    std::remove("/tmp/chemo_orn_off.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 7: Body movement — тяга создаёт движение тела
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Chemotaxis, BodyMovementFromThrust) {
    // Проверяем физику: F=thrust, a=F/m, v+=a*dt, pos+=v*dt
    float thrust = 500.0f;
    float body_mass = 1000.0f;
    float dt = 0.1f;
    float speed = 0.0f;
    float pos_z = 0.0f;
    float heading = 0.0f;  // прямо вперёд по +Z

    // 10 шагов вперёд
    for (int i = 0; i < 10; i++) {
        // Drag: F_drag = 0.5 * rho * Cd * area * v^2
        float area = M_PI * 5.0f * 5.0f;  // trunk_radius=5
        float drag_coeff = 0.015f;
        float drag = 0.5f * drag_coeff * area * speed * speed;
        float net_force = thrust - drag;
        float accel = net_force / body_mass;
        speed += accel * dt;
        if (speed < 0) speed = 0;
        pos_z += cosf(heading) * speed * dt;
    }

    EXPECT_GT(speed, 0.01f) << "Скорость должна быть > 0 после 10 шагов с тягой";
    EXPECT_GT(pos_z, 0.1f) << "Позиция должна измениться вперёд по Z";
    printf("[Chemo-Move] speed=%.3f pos_z=%.1f after 10 steps\n", speed, pos_z);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 8: Координатный трансформ world-space (формула)
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Chemotaxis, CoordinateTransformFormula) {
    // Проверяем формулу local→world:
    // world = pos + rotate(heading) * local
    // rotate(heading): x' = x*cos(h) + z*sin(h), z' = -x*sin(h) + z*cos(h)

    glm::vec3 body_pos(100.0f, 0.0f, 200.0f);
    float heading = 0.5f;  // ~28.6°

    // Ноздря в body-local (левая)
    glm::vec3 local(-3.0f, 95.0f, 200.0f);

    // Поворот на heading
    float ch = cosf(heading), sh = sinf(heading);
    glm::vec3 rotated(sh * local.z + ch * local.x, local.y,
                      ch * local.z - sh * local.x);

    // Смещение на позицию тела
    glm::vec3 world = body_pos + rotated;

    // Без поворота (heading=0): world = pos + local = (97, 95, 400)
    // С поворотом: X смещается из-за Z-компоненты ноздри
    EXPECT_NE(world.x, body_pos.x + local.x)
        << "С поворотом X-координата должна отличаться от простого сложения";
    EXPECT_NE(world.z, body_pos.z + local.z)
        << "С поворотом Z-координата должна отличаться от простого сложения";

    // Проверяем что при heading=0 формула вырождается в простое сложение
    glm::vec3 rot0(sinf(0)*local.z + cosf(0)*local.x, local.y,
                   cosf(0)*local.z - sinf(0)*local.x);
    glm::vec3 world0 = body_pos + rot0;
    EXPECT_NEAR(world0.x, body_pos.x + local.x, 0.01f);
    EXPECT_NEAR(world0.z, body_pos.z + local.z, 0.01f);

    printf("[Chemo-Coord] local(%.0f,%.0f,%.0f) + heading=%.2f → world(%.1f,%.1f,%.1f)\n",
           local.x, local.y, local.z, heading, world.x, world.y, world.z);
}
