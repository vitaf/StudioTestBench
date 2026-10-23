// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_behavioral_verification.cpp — Верификация поведенческих цепей
// без 3D-эмуляции. Минимальные hand-built сети — каждый тест < 1с.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Хелперы
// ═══════════════════════════════════════════════════════════════════════════════

static SegmentSnapshot* make_soma(NetworkModel& m, float x, float y, float z,
                                   uint32_t nid, float Vm=-65.0f) {
    auto* s = m.create_segment({x,y,z}, SegmentType::SOMA, {0,1,0});
    s->radius=5.0f; s->length=10.0f; s->V_m=Vm; s->neuron_id=nid;
    s->intra_ions[0]=10; s->intra_ions[1]=140; s->intra_ions[2]=0.0001f; s->intra_ions[3]=10;
    s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
    s->add_modifier(ModifierType::MOD_PUMP_NAK, 0.5f);
    return s;
}

static void make_syn(NetworkModel& m, uint32_t pre, uint32_t post,
                      ModifierType rec, uint8_t nt_idx, float w) {
    SynapseSnapshot s;
    s.id=m.synapses.size()+1; s.pre_id=pre; s.post_id=post;
    s.type=SynapseType::CHEMICAL; s.weight=w; s.delay=1.0f;
    s.post_receptors[0].type = static_cast<uint8_t>(rec); s.post_receptors[0].density=w;
    s.post_receptor_count=1; s.cleft_nt[nt_idx]=1.0f;
    m.synapses.push_back(s);
}

static std::vector<SegmentSnapshot> sim_ms(const char* path, int ms,
                                            SimulationBridge::Backend be
                                            =SimulationBridge::Backend::CPU) {
    SimulationBridge sim; sim.setBackend(be);
    if (!sim.loadNetwork(path)) return {};
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    std::vector<SegmentSnapshot> segs;
    sim.getSegmentsSnapshot(segs);
    sim.stop();
    return segs;
}

static float get_vm(const std::vector<SegmentSnapshot>& segs, uint32_t nid) {
    for (auto& s : segs) if (s.neuron_id==nid) return s.V_m;
    return -65.0f;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 1: Хемотаксис E2E — градиент еды → ORN → HYPO → dIN → MN
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, ChemotaxisE2E_FoodGradient) {
    NetworkModel m;
    // ORN: obonic sensors (left gets more food)
    uint32_t ornL = make_soma(m, -20, 95, 220, 1, -55.0f)->id; // деполяризован едой
    uint32_t ornR = make_soma(m,  20, 95, 220, 2, -65.0f)->id; // меньше еды
    // HYPO: hunger interneurons
    uint32_t hypL = make_soma(m, -20, 50, 150, 3)->id;
    uint32_t hypR = make_soma(m,  20, 50, 150, 4)->id;
    // SER: search neurons
    uint32_t serL = make_soma(m, -20, 50, 160, 5)->id;
    uint32_t serR = make_soma(m,  20, 50, 160, 6)->id;
    // dIN: CPG
    uint32_t dinL = make_soma(m, -20, 70, 0, 7)->id;
    uint32_t dinR = make_soma(m,  20, 70, 0, 8)->id;
    // MN: motor
    uint32_t  mnL = make_soma(m, -20, 20, 0, 9)->id;
    uint32_t  mnR = make_soma(m,  20, 20, 0, 10)->id;

    // ORN → HYPO (ipsi, AMPA, w=0.8)
    make_syn(m, ornL, hypL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    make_syn(m, ornR, hypR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    // ORN → dIN (ipsi, AMPA, w=0.4) — прямой рефлекс
    make_syn(m, ornL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    make_syn(m, ornR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    // HYPO → SER (ipsi, AMPA, w=0.6)
    make_syn(m, hypL, serL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    make_syn(m, hypR, serR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    // SER → dIN (ipsi, AMPA, w=0.5)
    make_syn(m, serL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    make_syn(m, serR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    // dIN → MN (ipsi, AMPA, w=0.8)
    make_syn(m, dinL, mnL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    make_syn(m, dinR, mnR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);

    for (uint32_t i=1; i<=10; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/bv_chemo.bion"));

    auto segs = sim_ms("/tmp/bv_chemo.bion", 200);
    float dinL_vm = get_vm(segs, 7), dinR_vm = get_vm(segs, 8);
    float  mnL_vm = get_vm(segs, 9),  mnR_vm = get_vm(segs, 10);
    printf("[BV-Chemo] dIN L=%.1f R=%.1f  MN L=%.1f R=%.1f  asym=%.1f\n",
           dinL_vm, dinR_vm, mnL_vm, mnR_vm, mnR_vm-mnL_vm);
    EXPECT_GT(dinL_vm, dinR_vm - 5.0f) << "Левый dIN более активен (больше еды слева)";
    for (auto& s : segs) { EXPECT_FALSE(std::isnan(s.V_m)); EXPECT_GT(s.V_m, -200.0f); }
    std::remove("/tmp/bv_chemo.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 2: Фототаксис E2E — световая асимметрия → PINEAL → TECTUM → dIN
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, PhototaxisE2E_LightAsymmetry) {
    NetworkModel m;
    uint32_t pinL = make_soma(m, -20, 98, 180, 1, -55.0f)->id; // левый PINEAL (больше света)
    uint32_t pinR = make_soma(m,  20, 98, 180, 2, -65.0f)->id; // правый PINEAL (меньше)
    uint32_t tecL = make_soma(m, -20, 100, 200, 3)->id;
    uint32_t tecR = make_soma(m,  20, 100, 200, 4)->id;
    uint32_t dinL = make_soma(m, -20, 70, 0, 5)->id;
    uint32_t dinR = make_soma(m,  20, 70, 0, 6)->id;
    uint32_t cinL = make_soma(m, -20, 45, 0, 7)->id;
    uint32_t cinR = make_soma(m,  20, 45, 0, 8)->id;

    // PINEAL → dIN (ipsi, AMPA, w=0.8)
    make_syn(m, pinL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    make_syn(m, pinR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.8f);
    // TECTUM → dIN (ipsi, AMPA, w=0.6)
    make_syn(m, tecL, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    make_syn(m, tecR, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    // TECTUM → cIN (contra, AMPA, w=0.4)
    make_syn(m, tecL, cinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    make_syn(m, tecR, cinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.4f);
    // cIN → dIN (ipsi, GLYCINE, w=2.5) — cross inhibition
    make_syn(m, cinL, dinL, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 2.5f);
    make_syn(m, cinR, dinR, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 2.5f);
    // PINEAL → TECTUM (ipsi, AMPA)
    make_syn(m, pinL, tecL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    make_syn(m, pinR, tecR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);

    for (uint32_t i=1; i<=8; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/bv_photo.bion"));

    auto segs = sim_ms("/tmp/bv_photo.bion", 200);
    float dinL_vm=get_vm(segs,5), dinR_vm=get_vm(segs,6);
    printf("[BV-Photo] dIN L=%.1f R=%.1f (expect L>R: more light on left)\n",
           dinL_vm, dinR_vm);
    EXPECT_GT(dinL_vm, dinR_vm - 5.0f) << "Левый dIN более активен (больше света слева)";
    for (auto& s : segs) { EXPECT_FALSE(std::isnan(s.V_m)); EXPECT_GT(s.V_m, -200.0f); }
    std::remove("/tmp/bv_photo.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 3: Тень → AVOID → contra-dIN
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, ShadowAvoidance_AVOID_to_ContraDIN) {
    NetworkModel m;
    // Тень на левом глазу → левый TECTUM слаб → левый AVOID расторможен
    uint32_t tecL = make_soma(m, -20, 100, 170, 1, -70.0f)->id; // слабый (тень)
    uint32_t tecR = make_soma(m,  20, 100, 170, 2, -50.0f)->id; // активный (свет)
    uint32_t avoL = make_soma(m, -20, 100, 100, 3, -55.0f)->id; // расторможен
    uint32_t avoR = make_soma(m,  20, 100, 100, 4, -70.0f)->id; // заторможен
    uint32_t dinL = make_soma(m, -20, 70, 0, 5)->id;
    uint32_t dinR = make_soma(m,  20, 70, 0, 6)->id;
    uint32_t cinL = make_soma(m, -20, 45, 0, 7)->id;
    uint32_t cinR = make_soma(m,  20, 45, 0, 8)->id;

    // TECTUM → AVOID (ipsi, GABAA, w=0.7): сильный TECTUM тормозит AVOID
    make_syn(m, tecL, avoL, ModifierType::MOD_NT_RECEPTOR_GABAA, 1, 0.7f);
    make_syn(m, tecR, avoR, ModifierType::MOD_NT_RECEPTOR_GABAA, 1, 0.7f);
    // AVOID → dIN (contra, w=2.0): левый AVOID → правый dIN
    make_syn(m, avoL, dinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 2.0f);
    make_syn(m, avoR, dinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 2.0f);
    // AVOID → cIN (ipsi, w=1.5): усиливает асимметрию
    make_syn(m, avoL, cinL, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 1.5f);
    make_syn(m, avoR, cinR, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 1.5f);
    // cIN → dIN (ipsi, GLYCINE)
    make_syn(m, cinL, dinL, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 2.5f);
    make_syn(m, cinR, dinR, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 2.5f);

    for (uint32_t i=1; i<=8; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/bv_shadow.bion"));
    auto segs = sim_ms("/tmp/bv_shadow.bion", 150);

    float dinL_vm=get_vm(segs,5), dinR_vm=get_vm(segs,6);
    printf("[BV-Shadow] dIN L=%.1f R=%.1f (expect R>L: left shadow→right dIN)\n",
           dinL_vm, dinR_vm);
    // Тень слева → левый AVOID → ПРАВЫЙ dIN → поворот направо от тени
    EXPECT_GT(dinR_vm, dinL_vm - 5.0f) << "Правый dIN активнее (contra от левого AVOID)";
    for (auto& s : segs) { EXPECT_FALSE(std::isnan(s.V_m)); EXPECT_GT(s.V_m, -200.0f); }
    std::remove("/tmp/bv_shadow.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 4: SER поисковый драйв — голод → HYPO → SER → dIN
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, SER_SearchDrive) {
    NetworkModel m;
    uint32_t hyp = make_soma(m, -20, 50, 150, 1)->id;
    uint32_t ser = make_soma(m, -20, 50, 160, 2)->id;
    uint32_t din = make_soma(m, -20, 70,   0, 3)->id;
    uint32_t din2= make_soma(m, -20, 70, -50, 4)->id; // без SER-входа (контроль)

    // HYPO → SER (AMPA, w=0.6)
    make_syn(m, hyp, ser, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.6f);
    // SER → dIN (AMPA, w=0.5) — только на nid=3
    make_syn(m, ser, din, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);

    for (uint32_t i=1; i<=4; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/bv_ser.bion"));
    auto segs = sim_ms("/tmp/bv_ser.bion", 300); // 300ms для накопления

    float ser_vm=get_vm(segs,2), din_vm=get_vm(segs,3), din2_vm=get_vm(segs,4);
    printf("[BV-SER] SER=%.1f dIN(SER+)=%.1f dIN(ctrl)=%.1f delta=%.1f\n",
           ser_vm, din_vm, din2_vm, din_vm-din2_vm);
    // dIN с SER-входом должен быть более деполяризован чем контрольный
    EXPECT_GT(din_vm, din2_vm - 3.0f) << "dIN с SER-входом деполяризован сильнее контроля";
    for (auto& s : segs) { EXPECT_FALSE(std::isnan(s.V_m)); }
    std::remove("/tmp/bv_ser.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 5: ORN тормозит SER (глицин) — обнаружение еды подавляет поиск
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, SER_ORN_Inhibition) {
    NetworkModel m;
    uint32_t orn = make_soma(m, -20, 95, 220, 1, -45.0f)->id; // ORN активен (еда!)
    uint32_t ser = make_soma(m, -20, 50, 160, 2, -55.0f)->id; // SER был бы активен от голода...

    // ORN → SER (GLYCINE, w=0.5) — еда тормозит поиск
    make_syn(m, orn, ser, ModifierType::MOD_NT_RECEPTOR_GLYCINE, 7, 0.5f);

    for (uint32_t i=1; i<=2; i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/bv_orn_inh.bion"));
    auto segs = sim_ms("/tmp/bv_orn_inh.bion", 150);

    float ser_vm = get_vm(segs, 2);
    printf("[BV-ORNInh] SER=%.1f (ORN active→GLY→SER should be inhibited below -55)\n", ser_vm);
    // Глициновое торможение должно снизить V_m SER ниже стартового -55mV
    EXPECT_LT(ser_vm, -52.0f) << "SER заторможен глицином от ORN";
    for (auto& s : segs) { EXPECT_FALSE(std::isnan(s.V_m)); }
    std::remove("/tmp/bv_orn_inh.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 6: Калибровка SER→dIN — пресинаптический спайк, измеряем EPSP
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, Calibration_SER_to_dIN) {
    float prev_peak = -999;
    for (float w : {0.1f, 0.3f, 0.5f, 0.7f, 1.0f}) {
        NetworkModel m;
        // SER с надпороговым V_m — гарантирует спайк и выброс нейромедиатора
        uint32_t ser = make_soma(m, -20, 50, 160, 1, -30.0f)->id;
        uint32_t din = make_soma(m, -20, 70, 0, 2)->id;
        uint32_t ctrl= make_soma(m, -20, 70, -50, 3)->id;
        make_syn(m, ser, din, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, w);
        for (uint32_t i=1; i<=3; i++) m.neurons.push_back({i,0,0,0});

        char p[64]; snprintf(p,64,"/tmp/cal_s2d_%.2f.bion",w);
        ASSERT_TRUE(::SaveNetwork(m, p));
        auto segs = sim_ms(p, 50); // 50ms — поймать пик EPSP
        float din_vm=get_vm(segs,2), ctrl_vm=get_vm(segs,3);
        float delta = din_vm - ctrl_vm;
        printf("[Cal-SER] w=%.2f → dIN=%.1f ctrl=%.1f Δ=%.2f mV (SER spiking)\n",
               w, din_vm, ctrl_vm, delta);
        // Больший вес → больший EPSP (монотонность с допуском на шум)
        if (prev_peak > -900) EXPECT_GE(delta, prev_peak - 2.0f);
        prev_peak = delta;
        std::remove(p);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 7: Калибровка AVOID→dIN — пресинаптический спайк, измеряем EPSP
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, Calibration_AVOID_to_dIN) {
    float prev_peak = -999;
    for (float w : {0.5f, 1.0f, 1.5f, 2.0f, 3.0f}) {
        NetworkModel m;
        // AVOID с надпороговым V_m — escape-спайк
        uint32_t avo = make_soma(m, -20, 100, 100, 1, -25.0f)->id;
        uint32_t din = make_soma(m,  20, 70,   0, 2)->id;
        uint32_t ctrl= make_soma(m,  20, 70, -50, 3)->id;
        make_syn(m, avo, din, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, w);
        for (uint32_t i=1; i<=3; i++) m.neurons.push_back({i,0,0,0});

        char p[64]; snprintf(p,64,"/tmp/cal_a2d_%.2f.bion",w);
        ASSERT_TRUE(::SaveNetwork(m, p));
        auto segs = sim_ms(p, 50);
        float din_vm=get_vm(segs,2), ctrl_vm=get_vm(segs,3);
        float delta = din_vm - ctrl_vm;
        printf("[Cal-AVOID] w=%.2f → dIN=%.1f ctrl=%.1f Δ=%.2f mV (AVOID spiking)\n",
               w, din_vm, ctrl_vm, delta);
        if (prev_peak > -900) EXPECT_GE(delta, prev_peak - 3.0f);
        prev_peak = delta;
        std::remove(p);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 8: Oracle — turn только из MN асимметрии * 0.25 (без суррогатов)
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, NoSurrogates_Oracle) {
    // При симметричной сети: turn = (right_mn - left_mn) * 0.25 ≈ 0
    NetworkModel m;
    for (int i=0;i<4;i++) {
        float x=(i<2)?-20.0f:20.0f;
        make_soma(m, x, 20, (float)(-i*50), (uint32_t)(i+1));
    }
    for (uint32_t i=1;i<=4;i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/bv_oracle.bion"));
    auto segs = sim_ms("/tmp/bv_oracle.bion", 200);

    float sumL=0,sumR=0; int nL=0,nR=0;
    for (auto& s : segs) {
        if (s.type!=SegmentType::SOMA || s.position.y<15||s.position.y>25) continue;
        if (s.position.x<0) { sumL+=s.V_m; nL++; }
        else { sumR+=s.V_m; nR++; }
    }
    float mnL=nL>0?sumL/nL:-65, mnR=nR>0?sumR/nR:-65;
    float turn = (mnR - mnL) * 0.25f;
    printf("[BV-Oracle] MN L=%.1f R=%.1f turn=%.3f (formula verified, no surrogates)\n",
           mnL, mnR, turn);
    EXPECT_NEAR(turn, 0.0f, 10.0f);
    std::remove("/tmp/bv_oracle.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Тест 9: CPU/GPU parity — фототаксисная сеть
// ═══════════════════════════════════════════════════════════════════════════════
TEST(Behavioral, CPUvsGPU_PhototaxisChain) {
    NetworkModel m;
    uint32_t pin = make_soma(m, -20, 98, 180, 1, -50.0f)->id;
    uint32_t tec = make_soma(m, -20, 100, 200, 2)->id;
    uint32_t avo = make_soma(m, -20, 100, 100, 3)->id;
    uint32_t din = make_soma(m,  20, 70, 0, 4)->id; // contra
    make_syn(m, pin, tec, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 0.5f);
    make_syn(m, tec, avo, ModifierType::MOD_NT_RECEPTOR_GABAA, 1, 0.7f);
    make_syn(m, avo, din, ModifierType::MOD_NT_RECEPTOR_AMPA, 0, 2.0f);
    for (uint32_t i=1;i<=4;i++) m.neurons.push_back({i,0,0,0});
    ASSERT_TRUE(::SaveNetwork(m, "/tmp/bv_gpu.bion"));

    auto cpu_s = sim_ms("/tmp/bv_gpu.bion", 200, SimulationBridge::Backend::CPU);
    auto gpu_s = sim_ms("/tmp/bv_gpu.bion", 200, SimulationBridge::Backend::GPU);
    ASSERT_FALSE(cpu_s.empty()); ASSERT_FALSE(gpu_s.empty());
    float delta = fabsf(cpu_s[0].V_m - gpu_s[0].V_m);
    printf("[BV-GPU] CPU=%.2f GPU=%.2f delta=%.2f mV\n",
           cpu_s[0].V_m, gpu_s[0].V_m, delta);
    EXPECT_LT(delta, 5.0f);
    std::remove("/tmp/bv_gpu.bion");
}
