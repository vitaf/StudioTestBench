// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Проверка: packed-структуры GPU-пути + стимуляция через Core API.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include "simulation_core_api.h"
#include "data_model.h"
#include "serialization.h"

#include <unistd.h>
#ifndef STUDIO_HOME
#define STUDIO_HOME "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio"
#endif

int P = 0, F = 0;
#define CHK(c,f,...) do{if(c){P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}else{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}}while(0)
#define SEC(n) printf("\n=== %s ===\n",n)

static float rvm(CoreData d,uint32_t tid){uint32_t n=core_num_segments(d);
  for(uint32_t i=0;i<n;i++){void*s=(char*)core_segments_ptr(d)+i*988;
    if(core_seg_id(s)==tid)return core_seg_vm(s);}return-999;}
static void rtk(CoreData d,int n,float dt=0.1f){void*st,*sc,*vc,*vcc,*rm,*rmc,*io,*ioc,*li,*lic; void*me,*mec;
  core_get_stimulus_buffers(&st,&sc,&vc,&vcc,&rm,&rmc,&io,&ioc,&li,&lic, &me, &mec);uint64_t tick=0;
  for(int t=0;t<n;t++)core_run_tick(core_segments_ptr(d),core_num_segments(d),
    core_connections_ptr(d),core_num_connections(d),core_synapses_ptr(d),core_num_synapses(d),
    0,&tick,dt,st,sc,vc,vcc,rm,rmc,io,ioc,li,lic, me, mec);}

// Точные копии структур из simulation_bridge.cpp GPU-пути
// CurrentStimulus: {uint32_t seg_id; float amp_pA; float dur_ms; uint64_t start_tick;} (packed)
// VoltageClamp:    {uint32_t seg_id; float target_mV; uint64_t start_tick; uint64_t end_tick;} (packed)
struct __attribute__((packed)) GPUCStim  { uint32_t seg_id; float amp_pA, dur_ms; uint64_t start; };
struct __attribute__((packed)) GPUVClamp { uint32_t seg_id; float target_mV; uint64_t start, end; };

int main(){
    // Пропускаем если нет модели (не хардкодим абсолютный путь)
    if (access(STUDIO_HOME "/experiments/test_single_neuron.bion", F_OK) != 0) {
        printf("SKIP: model file not found at " STUDIO_HOME "/experiments/test_single_neuron.bion\n");
        return 77;
    }
    core_init_libraries(); core_set_bio_mode();

    SEC("1. STRUCT SIZES: GPU packed structs match expected");
    {
        // CurrentStimulus: 4+4+4+8=20 bytes packed
        CHK(sizeof(GPUCStim) == 20, "sizeof(GPUCStim)=%zu (expect 20)", sizeof(GPUCStim));
        // VoltageClamp: 4+4+8+8=24 bytes packed
        CHK(sizeof(GPUVClamp) == 24, "sizeof(GPUVClamp)=%zu (expect 24)", sizeof(GPUVClamp));

        // Проверяем, что БЕЗ packed размер другой (баг!)
        struct NonPackedCStim { uint32_t seg_id; float amp_pA, dur_ms; uint64_t start; };
        CHK(sizeof(NonPackedCStim) == 24,
            "sizeof(NonPackedCStim)=%zu (expect 24 — padding!)", sizeof(NonPackedCStim));
    }

    SEC("2. GPU-STIM: эмуляция с packed-структурами");
    {
        const char* bion_path = STUDIO_HOME "/experiments/test_single_neuron.bion";
        CoreData d = core_load_bion(bion_path);
        CHK(d != nullptr, "load model");

        uint32_t nseg = core_num_segments(d);
        CHK(nseg == 16, "segments: %u", nseg);

        // Находим сегмент 7
        void* seg7 = nullptr;
        for (uint32_t i = 0; i < nseg; i++) {
            void* s = (char*)core_segments_ptr(d) + i * 988;
            if (core_seg_id(s) == 7) { seg7 = s; break; }
        }
        CHK(seg7 != nullptr, "segment 7 found");
        float v0 = rvm(d, 7);
        printf("  seg7 initial Vm=%.2f mV\n", v0);

        core_clear_stimuli();

        // Добавляем стимулы как в GPU-пути (через core_add_current_stimulus)
        // start_tick В ТИКАХ! delay_ms / 0.1
        core_add_current_stimulus(7, 100.0f, 10.0f, (uint64_t)(5.0f / 0.1f));    // sub: start=50
        core_add_current_stimulus(7, 2000.0f, 10.0f, (uint64_t)(30.0f / 0.1f));  // supra: start=300

        // Получаем доступ к буферам и кастим как GPU-путь
        void *st, *sc, *vc_ptr, *vcc, *rm, *rmc, *io, *ioc, *li, *lic; void *me, *mec;
        core_get_stimulus_buffers(&st, &sc, &vc_ptr, &vcc, &rm, &rmc, &io, &ioc, &li, &lic, &me, &mec);

        uint32_t sc_n = sc ? *(uint32_t*)sc : 0;
        printf("  stimuli count: %u\n", sc_n);
        CHK(sc_n == 2, "2 stimuli buffered");

        // Кастим с packed-структурами (как теперь в simulation_bridge.cpp)
        GPUCStim* sc_arr = (GPUCStim*)st;
        printf("  stim[0]: seg=%u amp=%.1f dur=%.1f start=%lu\n",
               sc_arr[0].seg_id, sc_arr[0].amp_pA, sc_arr[0].dur_ms, (unsigned long)sc_arr[0].start);
        printf("  stim[1]: seg=%u amp=%.1f dur=%.1f start=%lu\n",
               sc_arr[1].seg_id, sc_arr[1].amp_pA, sc_arr[1].dur_ms, (unsigned long)sc_arr[1].start);

        CHK(sc_arr[0].seg_id == 7, "stim[0] seg_id=%u", sc_arr[0].seg_id);
        CHK(sc_arr[0].amp_pA == 100.0f, "stim[0] amp=%.1f pA", sc_arr[0].amp_pA);
        CHK(sc_arr[0].start == 50, "stim[0] start=%lu (expect 50)", (unsigned long)sc_arr[0].start);

        CHK(sc_arr[1].seg_id == 7, "stim[1] seg_id=%u", sc_arr[1].seg_id);
        CHK(sc_arr[1].amp_pA == 2000.0f, "stim[1] amp=%.1f pA", sc_arr[1].amp_pA);
        CHK(sc_arr[1].start == 300, "stim[1] start=%lu (expect 300)", (unsigned long)sc_arr[1].start);

        // Запускаем симуляцию (CPU-путь: core_run_tick обрабатывает стимулы сам)
        float vm_at_60 = -999, vm_at_320 = -999;
        for (int t = 0; t < 400; t++) {
            rtk(d, 1);
            if (t == 60) vm_at_60 = rvm(d, 7);
            if (t == 320) vm_at_320 = rvm(d, 7);
        }

        float v_final = rvm(d, 7);
        printf("  tick  60 Vm=%.3f (sub active, delta=%.3f)\n", vm_at_60, vm_at_60 - v0);
        printf("  tick 320 Vm=%.3f (supra active, delta=%.3f)\n", vm_at_320, vm_at_320 - v0);
        printf("  final Vm=%.3f (delta=%.3f)\n", v_final, v_final - v0);

        CHK(vm_at_60 > v0 + 0.5f, "sub stim depolarizes: %.2f > %.2f", vm_at_60, v0);
        CHK(vm_at_320 > -40.0f, "supra stim depolarizes strongly: %.2f > -40", vm_at_320);
        CHK(fabsf(v_final - v0) > 2.0f, "overall Vm change: d=%.2f mV", v_final - v0);

        core_unload_bion(d);
    }

    int t = P + F;
    printf("\n=== REPORT: %d PASS, %d FAIL (%d%%) ===\n", P, F, t > 0 ? P * 100 / t : 0);
    return F > 0 ? 1 : 0;
}
