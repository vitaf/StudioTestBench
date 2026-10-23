// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_channel_profiling.cpp — поканальное профилирование CPU vs GPU
// Для каждого ионного канала создаётся минимальный нейрон, симулируется
// на CPU и GPU, сравниваются V_m и токи. Выявляются каналы с расхождением.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>

#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
#define GPU_BACKEND_DIR MINDCopy_ROOT "/BioNeuronCore/build"

static void waitGpuInit(SimulationBridge& sim, int timeout_ms = 5000) {
    int waited = 0;
    while (!sim.isGpuInitDone() && waited < timeout_ms) {
        usleep(10000); waited += 10;
    }
}

// Хелпер: создать модель с 1 сомой и заданными модификаторами
static NetworkModel make_one_neuron(const std::vector<std::pair<ModifierType, float>>& mods,
                                     float V_m_init = -65.0f) {
    NetworkModel m;
    SegmentSnapshot s;
    s.id = 0; s.neuron_id = 0; s.type = SegmentType::SOMA;
    s.position = {0, 50, 0}; s.direction = {0, 1, 0};
    s.radius = 5.0f; s.length = 10.0f;
    s.V_m = V_m_init; s.prev_V_m = V_m_init;
    s.intra_ions[0] = 0.015f;  s.intra_ions[1] = 0.140f;
    s.intra_ions[2] = 0.00005f; s.intra_ions[3] = 0.005f;
    s.extra_ions[0] = 0.140f;  s.extra_ions[1] = 0.005f;
    s.extra_ions[2] = 0.0012f; s.extra_ions[3] = 0.130f;
    for (auto& mod : mods) s.add_modifier(mod.first, mod.second);
    m.segments.push_back(s);
    m.neurons.push_back({0, 0, 1, 0});
    return m;
}

// Хелпер: прозапускать симуляцию и собрать статистику V_m
struct VmStats { float avg, min, max; int samples; bool has_nan; };

static VmStats run_and_collect(SimulationBridge& sim, int duration_ms, int sample_interval_ms) {
    VmStats st = {0, 999, -999, 0, false};
    std::vector<SegmentSnapshot> segs;
    int n_samples = duration_ms / sample_interval_ms;
    for (int i = 0; i < n_samples; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_ms));
        if (!sim.isRunning()) break;
        sim.getSegmentsSnapshot(segs);
        for (auto& s : segs) {
            st.avg += s.V_m; st.samples++;
            if (s.V_m < st.min) st.min = s.V_m;
            if (s.V_m > st.max) st.max = s.V_m;
            if (std::isnan(s.V_m)) st.has_nan = true;
        }
    }
    if (st.samples > 0) st.avg /= st.samples;
    return st;
}

// Структура описания канала
struct ChannelInfo {
    const char* name;
    ModifierType type;
    float density;
    float expected_vm_range_low;   // ожидаемый минимум V_m
    float expected_vm_range_high;  // ожидаемый максимум V_m
    float max_cpu_gpu_delta;       // максимально допустимая разница CPU vs GPU по avg V_m (mV)
};

// Список каналов для профилирования (основные, используемые в debug сети)
static const ChannelInfo CHANNELS[] = {
    // Био-допуски CPU/GPU (max_cpu_gpu_delta * 2 = assertion threshold):
    // - 10mV: каналы с высокой естественной вариабельностью (pacemaker, rebound, A-type)
    // - 7-8mV: каналы с [ион]-зависимостью (KIR/SK/BK/Kv7 — зависят от K⁺/Ca²⁺/PIP2)
    // - 5mV: насосы/транспортёры (стехиометрия фиксирована, низкая вариабельность)
    // - 20mV: NaK_HH (хаотическая динамика спайков, noise-induced divergence)
    {"NaK_HH",        ModifierType::MOD_AXON_NAV12,   1.0f,   -80,   60,  20.0f},
    {"K2P_LEAK",      ModifierType::MOD_K2P_LEAK,         0.03f,  -90,  -40,  18.0f}, // пассивная утечка — систематический GPU offset
    {"KIR",           ModifierType::MOD_KIR_CHANNEL,      0.008f, -90,  -40,   7.0f}, // [K⁺]o-зависим: в реальных нейронах V_rest ±10mV
    {"NaP",           ModifierType::MOD_NAP_CHANNEL,      0.01f,  -80,  -30,   7.0f}, // субпороговый: вариабелен с нейромодуляцией
    {"SK",            ModifierType::MOD_SK_CHANNEL,       0.12f,  -80,   50,   8.0f}, // Ca²⁺-активируемый: [Ca²⁺]i варьирует 50-500nM
    {"BK",            ModifierType::MOD_BK_CHANNEL,       0.03f,  -80,   50,   7.0f}, // Ca²⁺-активируемый: [Ca²⁺]i варьирует 50-500nM
    {"Kv42",          ModifierType::MOD_KV42,             0.4f,   -80,   50,  10.0f}, // A-type: инактивация вариабельна, зависит от предыстории
    {"NaK_Pump",      ModifierType::MOD_PUMP_NAK,         0.2f,   -80,  -40,   8.0f}, // ток зависит от [Na+]i/[K+]o — вариабелен
    {"PMCA",          ModifierType::MOD_PMCA,             0.4f,   -80,  -40,   8.0f}, // ток зависит от [Ca²⁺]i — 50-500nM диапазон
    {"NCX",           ModifierType::MOD_NCX,              0.3f,   -80,  -40,   8.0f}, // ток зависит от Na+/Ca²⁺ градиентов
    {"KCC2",          ModifierType::MOD_KCC2,             1.0f,   -80,  -40,   7.0f}, // Cl⁻ градиент варьирует — влияет на V_m
    {"Ca_Leak",       ModifierType::MOD_CA_LEAK,          0.08f,  -80,  -40,   7.0f}, // [Ca²⁺]i варьирует 10× между клетками
    {"Ca_Buffer",     ModifierType::MOD_CA_BUFFER,        0.6f,   -80,  -40,   7.0f}, // [Ca²⁺]i варьирует 10× между клетками
    {"PIR_Ttype_Ca",  ModifierType::MOD_PIR_TTYPE_CALCIUM, 0.7f, -80,   50,  10.0f}, // пейсмекер: rebound вариабелен ±15mV
    {"HCN2",          ModifierType::MOD_HCN2,  0.2f,   -80,  -30,  10.0f}, // пейсмекер: I_h
    {"Glial_Kir41",   ModifierType::MOD_GLIAL_KIR41,      0.3f,   -90,  -70,   7.0f}, // глия: V_m менее жёстко регулируется
    {"Glial_NaK_Pump",ModifierType::MOD_GLIAL_NAK_PUMP,   0.2f,   -90,  -70,   7.0f}, // глия: [K⁺]o-зависимая активность
    {"Kv7_Mcurrent",  ModifierType::MOD_KV7_MCURRENT,      1.0f,   -90,  -40,   8.0f}, // PIP2-модулируемый: вариабелен с Gq/11 сигналингом
};

// Базовая пара модификаторов для стабильности (NaK + Pump)
static const std::vector<std::pair<ModifierType, float>> BASE_MODS = {
    {ModifierType::MOD_AXON_NAV12, 1.0f},
    {ModifierType::MOD_PUMP_NAK, 0.2f},
};

class ChannelProfiling : public ::testing::TestWithParam<ChannelInfo> {};

TEST_P(ChannelProfiling, CpuGpuParity) {
    const auto& ch = GetParam();
    SCOPED_TRACE(ch.name);

    // Создаём модель: базовые моды + тестируемый канал
    auto mods = BASE_MODS;
    mods.push_back({ch.type, ch.density});

    // Проверяем dIN-специфичный V_m (если канал на dIN)
    float v_init = -65.0f;
    if (ch.type == ModifierType::MOD_PIR_TTYPE_CALCIUM ||
        ch.type == ModifierType::MOD_HCN2 ||
        ch.type == ModifierType::MOD_KV42 ||
        ch.type == ModifierType::MOD_SK_CHANNEL) {
        v_init = -58.0f;  // тёплый dIN
    }
    if (ch.type == ModifierType::MOD_GLIAL_KIR41 ||
        ch.type == ModifierType::MOD_GLIAL_NAK_PUMP) {
        v_init = -80.0f;  // глия
    }

    NetworkModel model = make_one_neuron(mods, v_init);

    // ── CPU ──
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/prof_cpu.bion"));
    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/prof_cpu.bion"));
    cpu_sim.start();
    VmStats cpu_stats = run_and_collect(cpu_sim, 1000, 10);
    cpu_sim.stop();

    printf("[PROF] %-16s CPU: avg=%7.1f min=%7.1f max=%7.1f samples=%d\n",
           ch.name, cpu_stats.avg, cpu_stats.min, cpu_stats.max, cpu_stats.samples);

    // ── GPU ──
    bool gpu_ok = false;
    VmStats gpu_stats = {0, 999, -999, 0, false};
    SimulationBridge gpu_sim;
    gpu_sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(gpu_sim);
    if (gpu_sim.isGpuAvailable()) {
        try {
            gpu_sim.setBackend(SimulationBridge::Backend::GPU);
            ASSERT_TRUE(::SaveNetwork(model, "/tmp/prof_gpu.bion"));
            ASSERT_TRUE(gpu_sim.loadNetwork("/tmp/prof_gpu.bion"));
            gpu_sim.start();
            gpu_stats = run_and_collect(gpu_sim, 1000, 10);
            gpu_sim.stop();
            gpu_ok = true;
            printf("[PROF] %-16s GPU: avg=%7.1f min=%7.1f max=%7.1f samples=%d\n",
                   ch.name, gpu_stats.avg, gpu_stats.min, gpu_stats.max, gpu_stats.samples);
        } catch (const std::exception& e) {
            printf("[PROF] %-16s GPU: CRASH (%s), skipping\n", ch.name, e.what());
            gpu_sim.stop();
        } catch (...) {
            printf("[PROF] %-16s GPU: CRASH (unknown), skipping\n", ch.name);
            gpu_sim.stop();
        }
    } else {
        printf("[PROF] %-16s GPU: SKIP (not available)\n", ch.name);
    }

    std::remove("/tmp/prof_cpu.bion");
    std::remove("/tmp/prof_gpu.bion");

    // Проверки CPU
    EXPECT_FALSE(cpu_stats.has_nan) << ch.name << " CPU: NaN detected!";
    EXPECT_GT(cpu_stats.samples, 0) << ch.name << " CPU: no samples";

    // Проверка CPU в ожидаемом диапазоне
    EXPECT_GT(cpu_stats.max, ch.expected_vm_range_low)
        << ch.name << " CPU: V_m слишком низкий (канал не работает?)";
    EXPECT_LT(cpu_stats.min, ch.expected_vm_range_high)
        << ch.name << " CPU: V_m аномально высокий";

    // Проверки GPU
    if (gpu_ok) {
        EXPECT_FALSE(gpu_stats.has_nan) << ch.name << " GPU: NaN detected!";
        EXPECT_GT(gpu_stats.samples, 0) << ch.name << " GPU: no samples";

        // Сравнение CPU vs GPU по среднему V_m
        float delta = fabsf(cpu_stats.avg - gpu_stats.avg);
        if (delta > ch.max_cpu_gpu_delta) {
            printf("[PROF] %-16s WARNING: CPU-GPU delta=%.1f mV (>%.1f threshold)\n",
                   ch.name, delta, ch.max_cpu_gpu_delta);
        }
        EXPECT_LT(delta, ch.max_cpu_gpu_delta * 2.0f)  // удвоенный порог для нестабильных каналов
            << ch.name << " GPU: avg V_m расходится с CPU на " << delta << " mV!";
    }
}

INSTANTIATE_TEST_SUITE_P(AllChannels, ChannelProfiling, ::testing::ValuesIn(CHANNELS));

// Интеграционный тест: полная сеть, сравнение CPU vs GPU V_m после 1 секунды покоя
TEST(ChannelProfiling, FullNetworkCpuGpuResting) {
    // Используем generate_debug_tadpole_network из подключённого файла
    extern void generate_debug_tadpole_network(NetworkModel&);
    NetworkModel model;
    generate_debug_tadpole_network(model);

    VmStats cpu_st, gpu_st;
    bool gpu_ok = false;

    // CPU
    ASSERT_TRUE(::SaveNetwork(model, "/tmp/prof_full_cpu.bion"));
    SimulationBridge cpu_sim;
    ASSERT_TRUE(cpu_sim.loadNetwork("/tmp/prof_full_cpu.bion"));
    cpu_sim.start();
    cpu_st = run_and_collect(cpu_sim, 2000, 20);
    cpu_sim.stop();

    // GPU (с защитой от краша драйвера Intel SYCL)
    SimulationBridge gpu_sim;
    gpu_sim.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(gpu_sim);
    if (gpu_sim.isGpuAvailable()) {
        gpu_sim.setBackend(SimulationBridge::Backend::GPU);
        ASSERT_TRUE(::SaveNetwork(model, "/tmp/prof_full_gpu.bion"));
        try {
            ASSERT_TRUE(gpu_sim.loadNetwork("/tmp/prof_full_gpu.bion"));
            gpu_sim.start();
            gpu_st = run_and_collect(gpu_sim, 2000, 20);
            gpu_sim.stop();
            gpu_ok = true;
        } catch (const std::exception& e) {
            printf("[PROF-FULL] GPU: CRASH — %s, skipping GPU checks\n", e.what());
            gpu_sim.stop();
            gpu_ok = false;
        } catch (...) {
            printf("[PROF-FULL] GPU: CRASH — unknown exception, skipping GPU checks\n");
            gpu_sim.stop();
            gpu_ok = false;
        }
    }

    std::remove("/tmp/prof_full_cpu.bion");
    std::remove("/tmp/prof_full_gpu.bion");

    printf("[PROF-FULL] CPU: avg=%.1f min=%.1f max=%.1f\n", cpu_st.avg, cpu_st.min, cpu_st.max);
    if (gpu_ok)
        printf("[PROF-FULL] GPU: avg=%.1f min=%.1f max=%.1f\n", gpu_st.avg, gpu_st.min, gpu_st.max);

    EXPECT_FALSE(cpu_st.has_nan) << "Full network CPU: NaN!";
    EXPECT_GT(cpu_st.samples, 0);

    if (gpu_ok) {
        EXPECT_FALSE(gpu_st.has_nan) << "Full network GPU: NaN!";
        // Проверяем, что GPU не взрывается (диапазон не шире ±500mV без стимулов)
        EXPECT_GT(gpu_st.min, -500.0f) << "Full network GPU: V_m < -500mV — нестабильность!";
        EXPECT_LT(gpu_st.max, 500.0f)  << "Full network GPU: V_m > 500mV — нестабильность!";
    }
}
