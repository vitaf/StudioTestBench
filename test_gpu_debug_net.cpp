// test_gpu_debug_net.cpp — диагностика GPU-краша с отладочной сетью
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_debug_network.h"
#include "../BioNeuronStudio/src/serialization.h"

typedef void* (*create_t)();
typedef int   (*load_t)(void*, const char*);
typedef void  (*run_t)(void*, float);
typedef void  (*destroy_t)(void*);
typedef void* (*data_t)(void*);

TEST(GpuDebugNet, LoadAndRunTicks) {
    // 1. Генерируем debug network и сохраняем в .bion
    NetworkModel model;
    generate_debug_tadpole_network(model);
    const char* bion_path = "/tmp/test_gpu_debug_diag.bion";
    ASSERT_TRUE(SaveNetwork(model, bion_path));
    printf("[TEST] Saved %zu segs, %zu syns to %s\n",
           model.segments.size(), model.synapses.size(), bion_path);

    // 2. Загружаем GPU backend .so
    void* lib = dlopen("/home/vitaf/workarea/develop/MindCopy/BioNeuronCore/build/libbiocore_gpu_backend_intel.so", RTLD_NOW | RTLD_DEEPBIND);
    if (!lib) {
        fprintf(stderr, "[TEST] dlopen failed: %s\n", dlerror());
        GTEST_SKIP() << "GPU backend .so not loadable";
        return;
    }

    auto create  = (create_t)  dlsym(lib, "gpu_backend_create");
    auto load    = (load_t)    dlsym(lib, "gpu_backend_load_bion");
    auto run     = (run_t)     dlsym(lib, "gpu_backend_run_tick");
    auto destroy = (destroy_t) dlsym(lib, "gpu_backend_destroy");

    if (!create || !load || !run || !destroy) {
        fprintf(stderr, "[TEST] missing symbols: c=%p l=%p r=%p d=%p\n",
                (void*)create, (void*)load, (void*)run, (void*)destroy);
        dlclose(lib);
        GTEST_SKIP() << "GPU backend symbols missing";
        return;
    }

    // 3. Создаём и загружаем
    void* sim = create();
    ASSERT_NE(sim, nullptr) << "gpu_backend_create failed";

    int ret = load(sim, bion_path);
    ASSERT_EQ(ret, 0) << "gpu_backend_load_bion failed: " << ret;
    printf("[TEST] GPU load OK, running ticks...\n"); fflush(stdout);

    // 4. Запускаем тики с диагностикой
    for (int tick = 0; tick < 100; tick++) {
        fprintf(stderr, "\n=== GPU TICK %d ===\n", tick); fflush(stderr);
        run(sim, 0.1f);
        if (tick % 25 == 0) {
            printf("[TEST] tick %d OK\n", tick); fflush(stdout);
        }
    }

    printf("[TEST] All 100 GPU ticks completed!\n");
    destroy(sim);
    dlclose(lib);
    std::remove(bion_path);
}
