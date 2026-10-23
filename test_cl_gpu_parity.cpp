// test_cl_gpu_parity.cpp — изоляция Cl⁻ канала: CPU vs GPU один сегмент
#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/serialization.h"

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
#define GPU_BACKEND_DIR MINDCopy_ROOT "/BioNeuronCore/build"

static void waitGpuInit(SimulationBridge& sim) {
    int w=0; while(!sim.isGpuInitDone()&&w<5000){usleep(10000);w+=10;}
}

TEST(ClGpuParity, SingleSegmentClLeak) {
    // Минимальная модель: 1 сома с Cl⁻ каналом
    NetworkModel m;
    SegmentSnapshot s;
    s.id=0; s.neuron_id=0; s.type=SegmentType::SOMA;
    s.position={0,50,0}; s.direction={0,1,0};
    s.radius=5.0f; s.length=10.0f;
    s.V_m=-70.0f;
    // Физиологические концентрации
    s.intra_ions[0]=0.015f; s.intra_ions[1]=0.140f; s.intra_ions[2]=0.0001f;
    s.intra_ions[3]=0.005f; s.intra_ions[4]=1.0f;
    s.extra_ions[0]=0.140f; s.extra_ions[1]=0.005f; s.extra_ions[2]=0.002f;
    s.extra_ions[3]=0.130f; s.extra_ions[4]=1.0f;
    // Только Cl⁻ канал
    s.add_modifier(ModifierType::MOD_IONOTROPIC_CL, 0.1f);
    m.segments.push_back(s);
    m.neurons.push_back({0,0,1,0});

    SaveNetwork(m, "/tmp/test_cl_parity.bion");

    // CPU
    SimulationBridge cpu;
    cpu.loadNetwork("/tmp/test_cl_parity.bion");
    cpu.start(); std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cpu.stop();
    std::vector<SegmentSnapshot> cs; cpu.getSegmentsSnapshot(cs);
    float cpu_vm=cs[0].V_m, cpu_cl=cs[0].intra_ions[3];
    
    // GPU
    SimulationBridge gpu;
    gpu.initGpuBackend(GPU_BACKEND_DIR); waitGpuInit(gpu);
    if(!gpu.isGpuAvailable()){GTEST_SKIP()<<"GPU not available";}
    gpu.setBackend(SimulationBridge::Backend::GPU);
    gpu.loadNetwork("/tmp/test_cl_parity.bion");
    gpu.start(); std::this_thread::sleep_for(std::chrono::milliseconds(200));
    gpu.stop();
    std::vector<SegmentSnapshot> gs; gpu.getSegmentsSnapshot(gs);
    float gpu_vm=gs[0].V_m, gpu_cl=gs[0].intra_ions[3];

    printf("[CL-PARITY] CPU: V_m=%.3f Cl_i=%.6f | GPU: V_m=%.3f Cl_i=%.6f | dV=%.3f\n",
           cpu_vm,cpu_cl,gpu_vm,gpu_cl,fabsf(cpu_vm-gpu_vm));
    
    // Cl⁻ канал должен гиперполяризовать: V_m < -70mV (E_Cl≈-87mV)
    EXPECT_LT(cpu_vm, -65.0f) << "CPU: Cl⁻ должен гиперполяризовать";
    EXPECT_LT(gpu_vm, -65.0f) << "GPU: Cl⁻ должен гиперполяризовать";
    // В физиологическом диапазоне
    EXPECT_GT(cpu_vm, -200.0f); EXPECT_LT(cpu_vm, 150.0f);
    EXPECT_GT(gpu_vm, -200.0f); EXPECT_LT(gpu_vm, 150.0f);
    // Паритет в разумных пределах
    EXPECT_LT(fabsf(cpu_vm-gpu_vm), 30.0f) << "CPU/GPU Cl⁻ delta > 30mV";
    std::remove("/tmp/test_cl_parity.bion");
}
