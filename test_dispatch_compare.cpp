// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_dispatch_compare.cpp — сравнение .so vs fallback dispatch на CPU

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstring>

// Core includes
#include "types.h"
#include "modifier_interface.h"
#include "kernel_dispatch.h"

// Ручная установка сегмента и модификатора — БЕЗ .bion и симуляции
TEST(DispatchCompare, K2pPluginVsFallback) {
    // Инициализируем библиотеки (загружает .so плагины)
    for (int i = 0; i < MOD_TYPE_COUNT; i++) {
        biological_library[i] = nullptr;
        arbitrary_library[i] = nullptr;
    }
    init_biological_library();

    // Ручной сегмент
    Segment seg;
    memset(&seg, 0, sizeof(seg));
    seg.id = 0;
    seg.neuron_id = 0;
    seg.segment_type = 1; // SOMA
    seg.radius = 5.0f;
    seg.length = 10.0f;
    seg.V_m = -65.0f;
    seg.intra_ions[0] = 0.015f;   // Na
    seg.intra_ions[1] = 0.140f;   // K
    seg.intra_ions[2] = 0.00005f; // Ca
    seg.intra_ions[3] = 0.005f;   // Cl
    seg.extra_ions[0] = 0.140f;
    seg.extra_ions[1] = 0.005f;
    seg.extra_ions[2] = 0.0012f;
    seg.extra_ions[3] = 0.130f;

    // Модификатор K2P
    Modifier mod;
    memset(&mod, 0, sizeof(mod));
    mod.type = MOD_K2P_LEAK;
    mod.density = 0.03f;

    // Устанавливаем state[7] = effective area (как делает simulation_host / GPU kernel)
    mod.state[7] = 4.0f * PI * seg.radius * seg.radius; // SOMA area

    // ── Тест 1: прямой вызов fallback функции ──
    float fallback_result = k2p_leak_current(&mod, &seg, 0.1f);
    printf("[DISPATCH] Fallback k2p_leak_current: %.6f nA\n", fallback_result);

    // ── Тест 2: через dispatch (CPU-путь без .so) ──
    // Очищаем modifier_current_table чтобы гарантировать fallback
    void* saved = modifier_current_table[MOD_K2P_LEAK];
    modifier_current_table[MOD_K2P_LEAK] = nullptr;
    float dispatch_fallback = dispatch_modifier_current(0, MOD_K2P_LEAK, &mod, &seg, 0.1f);
    modifier_current_table[MOD_K2P_LEAK] = (ModifierFunc)saved;
    printf("[DISPATCH] dispatch (fallback): %.6f nA\n", dispatch_fallback);
    EXPECT_FLOAT_EQ(fallback_result, dispatch_fallback);

    // ── Тест 3: через dispatch с .so плагином ──
    // На CPU с загруженными плагинами должен использоваться .so
    float dispatch_plugin = dispatch_modifier_current(0, MOD_K2P_LEAK, &mod, &seg, 0.1f);
    printf("[DISPATCH] dispatch (plugin): %.6f nA\n", dispatch_plugin);
    EXPECT_FLOAT_EQ(fallback_result, dispatch_plugin);

    // ── Тест 4: NaK_HH сравнение ──
    mod.type = MOD_AXON_NAV12;
    mod.density = 1.0f;
    float hh_fallback = hh_nak_current(&mod, &seg, 0.1f);
    saved = modifier_current_table[MOD_AXON_NAV12];
    modifier_current_table[MOD_AXON_NAV12] = nullptr;
    float hh_dispatch_fb = dispatch_modifier_current(0, MOD_AXON_NAV12, &mod, &seg, 0.1f);
    modifier_current_table[MOD_AXON_NAV12] = (ModifierFunc)saved;
    float hh_dispatch_plugin = dispatch_modifier_current(0, MOD_AXON_NAV12, &mod, &seg, 0.1f);
    printf("[DISPATCH] NaK_HH: fallback=%.6f dispatch_fb=%.6f dispatch_plugin=%.6f\n",
           hh_fallback, hh_dispatch_fb, hh_dispatch_plugin);
    EXPECT_FLOAT_EQ(hh_fallback, hh_dispatch_fb);
    EXPECT_FLOAT_EQ(hh_fallback, hh_dispatch_plugin);

    // ── Тест 5: Pump сравнение ──
    seg.ATP_level = 0.1f; // ATP=0.1 для ненулевого тока насоса
    mod.type = MOD_PUMP_NAK;
    mod.density = 0.2f;
    float pump_fallback = nak_pump_current(&mod, &seg, 0.1f);
    saved = modifier_current_table[MOD_PUMP_NAK];
    modifier_current_table[MOD_PUMP_NAK] = nullptr;
    float pump_dispatch_fb = dispatch_modifier_current(0, MOD_PUMP_NAK, &mod, &seg, 0.1f);
    modifier_current_table[MOD_PUMP_NAK] = (ModifierFunc)saved;
    float pump_dispatch_plugin = dispatch_modifier_current(0, MOD_PUMP_NAK, &mod, &seg, 0.1f);
    printf("[DISPATCH] Pump: fallback=%.6f dispatch_fb=%.6f dispatch_plugin=%.6f\n",
           pump_fallback, pump_dispatch_fb, pump_dispatch_plugin);
    EXPECT_FLOAT_EQ(pump_fallback, pump_dispatch_fb);
    EXPECT_FLOAT_EQ(pump_fallback, pump_dispatch_plugin);

    // ── Тест 6: Kv7 M-current сравнение ──
    mod.type = MOD_KV7_MCURRENT;
    mod.density = 1.0f;
    mod.state[0] = 0.0f;    // m_val start closed
    mod.state[1] = -38.0f;  // V_half
    mod.state[2] = 10.0f;   // k
    mod.state[3] = 0.5f;    // g_max
    mod.state[4] = 100.0f;  // tau_act
    mod.state[5] = 60.0f;   // tau_deact
    mod.state[6] = 1.0f;    // PIP2
    float kv7_fallback = kv7_mcurrent_current(&mod, &seg, 0.1f);
    saved = modifier_current_table[MOD_KV7_MCURRENT];
    modifier_current_table[MOD_KV7_MCURRENT] = nullptr;
    float kv7_dispatch_fb = dispatch_modifier_current(0, MOD_KV7_MCURRENT, &mod, &seg, 0.1f);
    modifier_current_table[MOD_KV7_MCURRENT] = (ModifierFunc)saved;
    float kv7_dispatch_plugin = dispatch_modifier_current(0, MOD_KV7_MCURRENT, &mod, &seg, 0.1f);
    printf("[DISPATCH] Kv7: fallback=%.6f dispatch_fb=%.6f dispatch_plugin=%.6f\n",
           kv7_fallback, kv7_dispatch_fb, kv7_dispatch_plugin);
    EXPECT_FLOAT_EQ(kv7_fallback, kv7_dispatch_fb);
    EXPECT_FLOAT_EQ(kv7_fallback, kv7_dispatch_plugin);
    // Проверка знака: Kv7 должен генерировать отрицательный ток (K⁺ выходит = гиперполяризующий)
    EXPECT_LT(kv7_fallback, 0.0f) << "Kv7 M-current должен быть гиперполяризующим (K⁺ выходит)";

    // ── Тест 7: Kv7 PIP2=0 блокировка ──
    mod.state[6] = 0.0f;  // PIP2=0 → канал закрыт
    float kv7_pip2_zero = kv7_mcurrent_current(&mod, &seg, 0.1f);
    printf("[DISPATCH] Kv7 PIP2=0: current=%.6f nA\n", kv7_pip2_zero);
    EXPECT_FLOAT_EQ(kv7_pip2_zero, 0.0f) << "Kv7 должен давать ноль при PIP2=0";
}
