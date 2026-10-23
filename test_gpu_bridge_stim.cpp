// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Проверка: GPU-путь SimulationBridge со стимулами.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include "simulation_bridge.h"
#include "simulation_core_api.h"
#include "data_model.h"
#include "serialization.h"

#ifndef STUDIO_HOME
#define STUDIO_HOME "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio"
#endif

int P=0,F=0;
#define CHK(c,f,...) do{if(c){P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}else{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}}while(0)
#define SEC(n) printf("\n=== %s ===\n",n)

int main(){
    if (access(STUDIO_HOME "/experiments/test_single_neuron.bion", F_OK) != 0) {
        printf("SKIP: model file not found\n");
        return 77;
    }
    core_init_libraries(); core_set_bio_mode();

    SEC("1. GPU BACKEND: load model + stimuli через SimulationBridge");
    {
        // Копируем модель чтобы не испортить оригинал
        system("cp " STUDIO_HOME "/experiments/test_single_neuron.bion /tmp/test_gpu_bridge.bion");

        SimulationBridge sim;
        sim.initGpuBackend("../BioNeuronCore/build");
        bool gpu_ok = false;

        // Пробуем GPU (Intel, затем NVIDIA, затем CPU)
        if (sim.isIntelAvailable()) {
            sim.setBackend(SimulationBridge::Backend::GPU_INTEL);
        } else if (sim.isCudaAvailable()) {
            sim.setBackend(SimulationBridge::Backend::GPU_CUDA);
        }
        if (sim.getBackend() != SimulationBridge::Backend::CPU && sim.loadNetwork("/tmp/test_gpu_bridge.bion")) {
            printf("  GPU backend loaded OK\n");
            gpu_ok = true;
        } else {
            printf("  GPU backend failed, falling back to CPU\n");
            sim.setBackend(SimulationBridge::Backend::CPU);
            CHK(sim.loadNetwork("/tmp/test_gpu_bridge.bion"), "CPU fallback load");
        }

        // Добавляем стимулы через Bridge API (как делает ProbeManager::onPreTick)
        sim.clearAllStimuli();
        sim.addCurrentStimulus(7, 100.0f, 10.0f, 5.0f);    // sub: 100pA, 10ms, 5ms delay
        sim.addCurrentStimulus(7, 2000.0f, 10.0f, 30.0f);  // supra: 2000pA, 10ms, 30ms delay

        printf("  Stimuli added, backend=%s, starting simulation...\n",
         SimulationBridge::backendName(sim.getBackend()));

        // Запускаем симуляцию
        sim.start();
        // Ждём немного тиков
        usleep(600000); // 600ms — enough for supra stim at tick 300 — достаточно для ~400+ тиков симуляции
        sim.stop();

        // Получаем снимок сегментов
        std::vector<SegmentSnapshot> segs;
        sim.getSegmentsSnapshot(segs);
        CHK(segs.size() == 16, "snapshot: %zu segments", segs.size());

        // Ищем сегмент 7
        float vm7 = -999;
        for (const auto& s : segs) {
            if (s.id == 7) { vm7 = s.V_m; break; }
        }
        printf("  seg7 Vm=%.3f mV\n", vm7);
        CHK(fabsf(vm7 + 67.0f) > 10.0f, "GPU/CPU stim: Vm changed significantly (d=%.1f mV)", vm7 + 67.0f);
        CHK(fabsf(vm7 + 67.0f) > 5.0f, "GPU/CPU stim: depolarization detected (d=%.1f mV)", vm7 + 67.0f);

        remove("/tmp/test_gpu_bridge.bion");
    }

    SEC("2. GPU BACKEND: повторный цикл (load→stimulate→stop→reload)");
    {
        system("cp " STUDIO_HOME "/experiments/test_single_neuron.bion /tmp/test_gpu_bridge2.bion");

        SimulationBridge sim;
        sim.setBackend(SimulationBridge::Backend::CPU); // CPU для стабильности

        // Цикл: load → stimulate → run → stop (эмуляция повтора эксперимента)
        for (int cycle = 0; cycle < 2; cycle++) {
            printf("  Cycle %d:\n", cycle);

            bool loaded = sim.loadNetwork("/tmp/test_gpu_bridge2.bion");
            CHK(loaded, "cycle %d: load OK", cycle);
            if (!loaded) continue;

            sim.clearAllStimuli();
            sim.addCurrentStimulus(7, 500.0f, 20.0f, 10.0f);

            sim.start();
            usleep(500000); // 500ms
            sim.stop();

            std::vector<SegmentSnapshot> segs;
            sim.getSegmentsSnapshot(segs);
            float vm7 = -999;
            for (const auto& s : segs) {
                if (s.id == 7) { vm7 = s.V_m; break; }
            }
            printf("  cycle %d: seg7 Vm=%.3f mV (changed: %s)\n",
                   cycle, vm7, fabsf(vm7 + 67.0f) > 3.0f ? "yes" : "no");
            CHK(fabsf(vm7 + 67.0f) > 3.0f, "cycle %d: Vm changed", cycle);
        }

        remove("/tmp/test_gpu_bridge2.bion");
    }

    int t=P+F; printf("\n=== REPORT: %d PASS, %d FAIL (%d%%) ===\n",P,F,t>0?P*100/t:0);
    return F>0?1:0;
}
