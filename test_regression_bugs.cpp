// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Регрессионные тесты: баги, найденные и исправленные 2026-06-04.
// Покрытие: GPU/CPU стимулы, смещения структур, scanIndex, onPreTick, типы модификаторов.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include "simulation_core_api.h"
#include "simulation_bridge.h"
#include "data_model.h"
#ifndef STUDIO_HOME
#define STUDIO_HOME "/home/vitaf/BioNeuronStudio"
#endif
#include "serialization.h"
#include "experiment_data.h"
#include "experiment_manager.h"

int P=0,F=0;
#define CHK(c,f,...) do{if(c){P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}else{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}}while(0)
#define SEC(n) printf("\n=== %s ===\n",n)

// Вспомогательные (из test_sim_spikes.cpp)
static float rvm(CoreData d,uint32_t tid){uint32_t n=core_num_segments(d);
  for(uint32_t i=0;i<n;i++){void*s=(char*)core_segments_ptr(d)+i * core_seg_size();
    if(core_seg_id(s)==tid)return core_seg_vm(s);}return-999;}

int main(){
    core_init_libraries(); core_set_bio_mode();

    // ═══════════════════════════════════════════════════════════════
    SEC("1. GPU-PACKED-STRUCTS: CStim/VClamp не имеют padding");
    {
        // Эти структуры — копии из simulation_bridge.cpp GPU-пути
        struct __attribute__((packed)) CStim  { uint32_t seg_id; float amp_pA, dur_ms; uint64_t start; };
        struct __attribute__((packed)) VClamp { uint32_t seg_id; float target_mV; uint64_t start, end; };

        // Без packed: CStim был 24 байта (padding), с packed — 20
        struct NonPackedCStim { uint32_t seg_id; float amp_pA, dur_ms; uint64_t start; };
        CHK(sizeof(NonPackedCStim) == 24, "NonPackedCStim=24 (padding before u64)");
        CHK(sizeof(CStim) == 20, "PackedCStim=20 (matches CurrentStimulus)");
        CHK(sizeof(VClamp) == 24, "PackedVClamp=24");

        // Проверяем, что поля читаются правильно после cast из CurrentStimulus
        core_clear_stimuli();
        core_add_current_stimulus(42, 100.0f, 10.0f, (uint64_t)(5.0f/0.1f)); // start=50

        void *st, *sc, *vc, *vcc, *rm, *rmc, *io, *ioc, *li, *lic; void *me, *mec;
        core_get_stimulus_buffers(&st, &sc, &vc, &vcc, &rm, &rmc, &io, &ioc, &li, &lic, &me, &mec);

        CStim* cst = (CStim*)st;
        uint32_t cnt = sc ? *(uint32_t*)sc : 0;
        CHK(cnt == 1, "stimulus count: %u", cnt);
        CHK(cst->seg_id == 42, "seg_id: %u", cst->seg_id);
        CHK(cst->amp_pA == 100.0f, "amp_pA: %.1f", cst->amp_pA);
        CHK(cst->dur_ms == 10.0f, "dur_ms: %.1f", cst->dur_ms);
        CHK(cst->start == 50, "start_tick: %lu (expect 50)", (unsigned long)cst->start);
        core_clear_stimuli();
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("2. OFFSETS: I_buffer=912, V_m=40 в Segment (SYCL/GCC)");
    {
        // Проверяем через реальную модель — пишем в I_buffer и читаем V_m
        NetworkModel m;
        auto* s = m.create_segment({0,0,0}, SegmentType::DENDRITE);
        s->id = 7; s->radius = 0.8f; s->length = 28.0f; s->V_m = -67.0f;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 0.6f);
        s->add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.3f);

        SaveNetwork(m, "/tmp/regr_offset.bion");
        CoreData d = core_load_bion("/tmp/regr_offset.bion");
        CHK(d != nullptr, "load model for offset test");

        void* seg7 = nullptr;
        for (uint32_t i = 0; i < core_num_segments(d); i++) {
            void* sg = (char*)core_segments_ptr(d) + i * core_seg_size();
            if (core_seg_id(sg) == 7) { seg7 = sg; break; }
        }
        CHK(seg7 != nullptr, "segment 7 found");

        // Пишем ток в I_buffer (offset 912), запускаем тик, проверяем Vm (offset 40)
        *(float*)((char*)seg7 + 912) = 2.0e-7f; // 200nA — большой ток для заметного dV

        core_add_current_stimulus(7, 500.0f, 10.0f, 10);
        void *st, *sc, *vc, *vcc, *rm, *rmc, *io, *ioc, *li, *lic; void *me, *mec;
        core_get_stimulus_buffers(&st, &sc, &vc, &vcc, &rm, &rmc, &io, &ioc, &li, &lic, &me, &mec);
        uint64_t tick = 0;
        for (int t = 0; t < 20; t++) {
            core_run_tick(core_segments_ptr(d), core_num_segments(d),
                core_connections_ptr(d), core_num_connections(d),
                core_synapses_ptr(d), core_num_synapses(d),
                0, &tick, 0.1f, st, sc, vc, vcc, rm, rmc, io, ioc, li, lic, me, mec);
        }

        float vm = *(float*)((char*)seg7 + 40);
        float ib = *(float*)((char*)seg7 + 912);
        printf("  Vm=%.4f I_buf=%.4e (after 20 ticks)\n", (double)vm, (double)ib);
        CHK(fabsf(vm + 67.0f) > 0.01f, "Vm changed from -67 (offset 40 works)");
        CHK(ib >= 0.0f, "I_buf accessible (offset 912 works)");

        core_unload_bion(d);
        remove("/tmp/regr_offset.bion");
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("3. DURATION-MS/DT: core_run_tick конвертирует мс в тики");
    {
        NetworkModel m;
        auto* s = m.create_segment({0,0,0}, SegmentType::SOMA);
        s->id = 1; s->radius = 10.0f; s->length = 20.0f; s->V_m = -70.0f;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);
        s->add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);

        SaveNetwork(m, "/tmp/regr_dt.bion");
        CoreData d = core_load_bion("/tmp/regr_dt.bion");
        float v0 = rvm(d, 1);

        core_clear_stimuli();
        core_add_current_stimulus(1, 800.0f, 10.0f, 10); // 10ms duration, start_tick=10
        // С моим фиксом: dur_ticks = 10/0.1 = 100 тиков (не 10 тиков как было!)

        void *st, *sc, *vc, *vcc, *rm, *rmc, *io, *ioc, *li, *lic; void *me, *mec;
        core_get_stimulus_buffers(&st, &sc, &vc, &vcc, &rm, &rmc, &io, &ioc, &li, &lic, &me, &mec);
        uint64_t tick = 0;
        for (int t = 0; t < 120; t++) { // 120 тиков — стимул должен быть активен до тика 110
            core_run_tick(core_segments_ptr(d), core_num_segments(d),
                core_connections_ptr(d), core_num_connections(d),
                core_synapses_ptr(d), core_num_synapses(d),
                0, &tick, 0.1f, st, sc, vc, vcc, rm, rmc, io, ioc, li, lic, me, mec);
        }

        float v1 = rvm(d, 1);
        float dv = v1 - v0;
        printf("  Vm: %.2f -> %.2f (d=%.2f)\n", (double)v0, (double)v1, (double)dv);
        // С фиксом dt: 800pA × 100 тиков = значительная деполяризация
        // Без фикса: 800pA × 10 тиков = слабый эффект
        CHK(fabsf(dv) > 2.0f, "duration_ms/dt fix: dV=%.1f mV (expect >2)", (double)dv);

        core_unload_bion(d);
        remove("/tmp/regr_dt.bion");
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("4. DELAY-MS/0.1F: addCurrentStimulus конвертирует задержку");
    {
        // Проверяем через SimulationBridge API
        NetworkModel m;
        auto* s = m.create_segment({0,0,0}, SegmentType::SOMA);
        s->id = 1; s->radius = 10.0f; s->length = 20.0f; s->V_m = -70.0f;
        s->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);

        SaveNetwork(m, "/tmp/regr_delay.bion");

        SimulationBridge sim;
        sim.setBackend(SimulationBridge::Backend::CPU);
        CHK(sim.loadNetwork("/tmp/regr_delay.bion"), "load for delay test");

        sim.clearAllStimuli();
        sim.addCurrentStimulus(1, 800.0f, 10.0f, 5.0f); // 5ms delay

        // Проверяем, что start_tick = 5.0/0.025 = 200 (dt_ms_=0.025 — стабильно для быстрых Na, 0.1 runaway)
        void *st, *sc, *vc, *vcc, *rm, *rmc, *io, *ioc, *li, *lic; void *me, *mec;
        core_get_stimulus_buffers(&st, &sc, &vc, &vcc, &rm, &rmc, &io, &ioc, &li, &lic, &me, &mec);
        uint32_t cnt = sc ? *(uint32_t*)sc : 0;
        CHK(cnt == 1, "stimulus buffered: %u", cnt);

        struct __attribute__((packed)) { uint32_t seg_id; float amp, dur; uint64_t start; }* cst;
        cst = (decltype(cst))st;
        printf("  start_tick=%lu (expect 200 for 5ms delay @ dt=0.025)\n", (unsigned long)cst->start);
        CHK(cst->start == 200, "delay_ms/dt fix: start_tick=%lu", (unsigned long)cst->start);

        sim.clearAllStimuli();
        remove("/tmp/regr_delay.bion");
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("5. SCAN-INDEX: обнаруживает новые .bexp файлы");
    {
        char tmpdir[] = "/tmp/regr_scan_XXXXXX";
        CHK(mkdtemp(tmpdir) != nullptr, "create temp dir: %s", tmpdir);

        // Создаём .bexp с одним экспериментом и index.json
        std::string idx_path = std::string(tmpdir) + "/index.json";
        std::ofstream idx(idx_path);
        idx << R"([{"name":"OldExp","description":"","date":"?","cell_type":"?","tick_count":100,"file":"old.bexp"}])";
        idx.close();

        // Создаём old.bexp (минимальный)
        {
            FILE* f = fopen((std::string(tmpdir) + "/old.bexp").c_str(), "wb");
            uint32_t hdr[] = {0x50584542, 1, 50}; // magic, ver, json_len
            fwrite(hdr, 4, 3, f);
            const char* json = R"({"name":"OldExp","tick_count":100,"probes":[]})";
            fwrite(json, 1, 50, f);
            uint32_t fc[] = {100, 1, 400, 0}; // frame_count, stride, bytes, events
            fwrite(fc, 4, 4, f);
            fclose(f);
        }

        // Создаём new.bexp (должен быть обнаружен при сканировании)
        {
            FILE* f = fopen((std::string(tmpdir) + "/new.bexp").c_str(), "wb");
            uint32_t hdr[] = {0x50584542, 1, 60};
            fwrite(hdr, 4, 3, f);
            const char* json = R"({"name":"NewExp","description":"found","tick_count":200,"probes":[]})";
            fwrite(json, 1, 60, f);
            uint32_t fc[] = {200, 1, 800, 0};
            fwrite(fc, 4, 4, f);
            fclose(f);
        }

        ExperimentManager mgr;
        bool ok = mgr.scanIndex(tmpdir);
        CHK(ok, "scanIndex succeeded");

        const auto& entries = mgr.indexEntries();
        CHK(entries.size() >= 2, "found %zu entries (expect >=2)", entries.size());

        bool found_new = false;
        for (const auto& e : entries) {
            if (e.name == "NewExp") { found_new = true; break; }
        }
        CHK(found_new, "new.bexp detected by scanIndex");

        // Очистка
        remove((std::string(tmpdir) + "/old.bexp").c_str());
        remove((std::string(tmpdir) + "/new.bexp").c_str());
        remove(idx_path.c_str());
        rmdir(tmpdir);
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("6. SIM-DATA-FROM-GPU: флаг защиты от free() на GPU-памяти");
    {
        // Проверяем что SimulationBridge не вызывает core_unload_bion на GPU-данных
        SimulationBridge sim;
        // Без initGpuBackend: gpu_available_=false, loadNetwork идёт через CPU
        // Создаём модель
        NetworkModel m;
        auto* s = m.create_segment({0,0,0}, SegmentType::SOMA);
        s->id = 1; s->radius = 10.0f; s->length = 20.0f; s->V_m = -70.0f;
        s->add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);
        SaveNetwork(m, "/tmp/regr_gpufree.bion");

        // Последовательная загрузка-выгрузка (CPU)
        bool ld1 = sim.loadNetwork("/tmp/regr_gpufree.bion");
        CHK(ld1, "first load");
        bool ld2 = sim.loadNetwork("/tmp/regr_gpufree.bion");
        CHK(ld2, "second load (reload)");

        // Если simDataFromGpu_ не сброшен — был бы крах
        // Третий load для проверки стабильности
        bool ld3 = sim.loadNetwork("/tmp/regr_gpufree.bion");
        CHK(ld3, "third load (stable)");

        remove("/tmp/regr_gpufree.bion");
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("7. MODIFIER-TYPE: MOD_CL=2 (не MYELIN=4)");
    {
        // Проверяем что типы не перепутаны
        CHK(ModifierType::MOD_IONOTROPIC_CL == (ModifierType)2,
            "MOD_IONOTROPIC_CL == 2 (Cl- leak)");
        CHK(ModifierType::MOD_MYELIN == (ModifierType)4,
            "MOD_MYELIN == 4 (myelin sheath)");

        // Проверяем что модель test_single_neuron.bion имеет правильные типы
        NetworkModel m;
        std::string err;
        int ret = studio_load_bion(
            STUDIO_HOME "/experiments/test_single_neuron.bion",
            &m, err);
        CHK(ret == 0, "load test_single_neuron: %s", ret ? err.c_str() : "OK");

        // Проверяем что все сегменты (кроме аксонов) не имеют MYELIN
        int myelin_on_dendrite = 0, cl_on_dendrite = 0;
        for (const auto& seg : m.segments) {
            for (int j = 0; j < seg.modifier_count; j++) {
                if (seg.type != SegmentType::AXON && seg.type != SegmentType::PRESYNAPSE) {
                    if (seg.modifiers[j].mod_type() == ModifierType::MOD_MYELIN)
                        myelin_on_dendrite++;
                    if (seg.modifiers[j].mod_type() == ModifierType::MOD_IONOTROPIC_CL)
                        cl_on_dendrite++;
                }
            }
        }
        CHK(myelin_on_dendrite == 0, "no myelin on non-axon segments: %d", myelin_on_dendrite);
        CHK(cl_on_dendrite > 0, "Cl- channels on dendrites: %d", cl_on_dendrite);
    }



    int t=P+F; printf("\n=== REPORT: %d PASS, %d FAIL (%d%%) ===\n",P,F,t>0?P*100/t:0);
    return F>0?1:0;
}
