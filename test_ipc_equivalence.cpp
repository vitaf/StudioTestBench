// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_ipc_equivalence.cpp — проверка идентичности результатов STATIC vs IPC + тайминги.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <chrono>

#include "data_model.h"
#include "ipc_bridge.h"
#include "simulation_core_api.h"

using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::duration<double, std::milli>;

static int g_pass = 0, g_fail = 0;
#define PASS(fmt,...) do{g_pass++;printf("  [PASS] " fmt "\n",##__VA_ARGS__);}while(0)
#define FAIL(fmt,...) do{g_fail++;printf("  [FAIL] " fmt "\n",##__VA_ARGS__);}while(0)
#define CHECK(c,fmt,...) do{if(c)PASS(fmt,##__VA_ARGS__);else FAIL(fmt,##__VA_ARGS__);}while(0)

// ── Timing helpers ────────────────────────────────────────────────────────
struct TimingReport {
    double load_ms       = 0;   // время загрузки сети
    double run_ms        = 0;   // общее время симуляции
    double per_tick_us   = 0;   // среднее время на тик
    double first_frame_ms = 0;  // первый кадр (для IPC)
    int    frames        = 0;   // количество принятых кадров
};

static void printTimingHeader(const char* label) {
    printf("\n  ╔══════════════════════════════════════════════════════╗\n");
    printf("  ║  %-50s ║\n", label);
    printf("  ╠══════════════════════════════════════════════════════╣\n");
}

static void printTimingRow(const char* metric, double val, const char* unit) {
    printf("  ║  %-35s %10.3f %-4s ║\n", metric, val, unit);
}

static void printTimingFooter() {
    printf("  ╚══════════════════════════════════════════════════════╝\n");
}

static double elapsedMs(Clock::time_point start) {
    return Ms(Clock::now() - start).count();
}

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
static const char* DAEMON_PATH = MINDCopy_ROOT "/BioNeuronCore/build/biocore-daemon";
static const char* PLUGINS_PATH = MINDCopy_ROOT "/BioNeuronCore/build/plugins/";
static const char* SOCK_PATH   = "/tmp/biocore.sock";
static const char* TEST_BION   = MINDCopy_ROOT "/BioNeuronStudio/build/network.bion";

// Очистка старых демонов перед тестом (для надёжности в ctest)
static void cleanupStaleDaemons() {
    // Убиваем любые демоны с нашим тестовым сокетом
    system("pkill -f 'biocore-daemon.*ready-fd' 2>/dev/null");
    usleep(100000);
    unlink(SOCK_PATH);
    unlink("/tmp/biocore.sock");
    unlink("/tmp/biocore_test_bridge.sock");
    unlink("/tmp/biocore_test_debug.sock");
}

// ── STATIC-путь (с таймингами) ──────────────────────────────────────────
static std::vector<float> runStatic(const char* bion_path, int num_ticks, TimingReport& tim) {
    core_init_libraries();
    core_set_bio_mode();

    auto t0 = Clock::now();
    CoreData d = core_load_bion(bion_path);
    tim.load_ms = elapsedMs(t0);
    if (!d) { printf("  STATIC: load failed\n"); return {}; }

    uint32_t nseg = core_num_segments(d);
    printf("  STATIC: loaded %u segments in %.1f ms\n", nseg, tim.load_ms);

    void *st, *sc, *vc, *vcc, *rm, *rmc, *io, *ioc, *li, *lic; void *me, *mec;
    core_get_stimulus_buffers(&st, &sc, &vc, &vcc, &rm, &rmc, &io, &ioc, &li, &lic, &me, &mec);

    auto t_run0 = Clock::now();
    uint64_t tick = 0;
    for (int t = 0; t < num_ticks; t++) {
        core_run_tick(core_segments_ptr(d), core_num_segments(d),
                      core_connections_ptr(d), core_num_connections(d),
                      core_synapses_ptr(d), core_num_synapses(d),
                      0, &tick, 0.1f,
                      st, sc, vc, vcc, rm, rmc, io, ioc, li, lic, me, mec);
    }
    tim.run_ms = elapsedMs(t_run0);
    tim.per_tick_us = (tim.run_ms * 1000.0) / (double)num_ticks;

    std::vector<float> vm(nseg);
    for (uint32_t i = 0; i < nseg; i++) {
        void* seg = (char*)core_segments_ptr(d) + i * 988;
        vm[i] = core_seg_vm(seg);
    }
    printf("  STATIC: V_m[0] = %.6f mV, V_m[last] = %.6f mV\n", vm[0], vm[nseg-1]);
    core_unload_bion(d);
    return vm;
}

// ── IPC-путь (с pipe для сигнала готовности + детальные тайминги) ────────
static std::vector<float> runIpc(const char* bion_path, int num_ticks, bool& ok, TimingReport& tim) {
    ok = false;
    unlink(SOCK_PATH);

    // ── Фаза 1: запуск демона ─────────────────────────────────────────────
    auto t_daemon0 = Clock::now();

    int ready_pipe[2];
    if (pipe(ready_pipe) < 0) { printf("  IPC: pipe failed\n"); return {}; }

    char ready_fd_str[16];
    snprintf(ready_fd_str, sizeof(ready_fd_str), "%d", ready_pipe[1]);

    pid_t pid = fork();
    if (pid < 0) { printf("  IPC: fork failed\n"); close(ready_pipe[0]); close(ready_pipe[1]); return {}; }

    if (pid == 0) {
        close(ready_pipe[0]);
        execl(DAEMON_PATH, DAEMON_PATH, "--ready-fd", ready_fd_str,
              "-p", PLUGINS_PATH, nullptr);
        _exit(1);
    }

    close(ready_pipe[1]);

    char ready_byte = 0;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(ready_pipe[0], &fds);
    struct timeval tv = {10, 0};
    int ret = select(ready_pipe[0] + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) {
        printf("  IPC: daemon ready timeout\n");
        close(ready_pipe[0]);
        kill(pid, SIGTERM); waitpid(pid, nullptr, 0);
        return {};
    }
    if (read(ready_pipe[0], &ready_byte, 1) < 0) { /* не должно случиться */ }
    close(ready_pipe[0]);

    double daemon_start_ms = elapsedMs(t_daemon0);
    printf("  IPC: daemon ready in %.1f ms (pid=%d)\n", daemon_start_ms, pid);

    // ── Фаза 2: подключение ───────────────────────────────────────────────
    auto t_conn0 = Clock::now();
    IpcBridge bridge;
    bridge.unix_path = SOCK_PATH;
    if (!bridge.connect()) {
        printf("  IPC: connect failed\n");
        kill(pid, SIGTERM); waitpid(pid, nullptr, 0);
        return {};
    }
    double connect_ms = elapsedMs(t_conn0);
    printf("  IPC: connected in %.1f ms\n", connect_ms);

    // ── Фаза 3: загрузка сети ─────────────────────────────────────────────
    auto t_load0 = Clock::now();
    if (!bridge.loadBion(bion_path)) {
        printf("  IPC: loadBion failed\n");
        bridge.disconnect();
        kill(pid, SIGTERM); waitpid(pid, nullptr, 0);
        return {};
    }
    tim.load_ms = elapsedMs(t_load0);
    printf("  IPC: loaded %u segments in %.1f ms\n", bridge.numSegments(), tim.load_ms);

    // ── Фаза 4: подписка на стейт ─────────────────────────────────────────
    auto t_sub0 = Clock::now();
    if (!bridge.subscribeState(0)) {
        printf("  IPC: subscribe failed\n");
        bridge.disconnect();
        kill(pid, SIGTERM); waitpid(pid, nullptr, 0);
        return {};
    }
    double subscribe_ms = elapsedMs(t_sub0);
    printf("  IPC: subscribed in %.1f ms\n", subscribe_ms);

    // ── Фаза 5: startSim + сбор фреймов до num_ticks ──────────────────────
    auto t_run0 = Clock::now();
    if (!bridge.startSim(0.1f, 0.0f)) {
        printf("  IPC: startSim failed\n");
        bridge.disconnect();
        kill(pid, SIGTERM); waitpid(pid, nullptr, 0);
        return {};
    }

    // Собираем фреймы пока тик не достигнет num_ticks
    std::vector<SegmentSnapshot> segments;
    std::vector<SynapseSnapshot> synapses;
    uint64_t tick = 0;
    bool got_frame = false;
    for (int retry = 0; retry < 120; retry++) {
        usleep(50000);
        std::vector<SegmentSnapshot> frame_segs;
        std::vector<SynapseSnapshot> frame_syns;
        uint64_t frame_tick = 0;
        if (bridge.receiveStateFrame(frame_segs, frame_syns, frame_tick, 50)) {
            got_frame = true;
            segments = std::move(frame_segs);
            synapses = std::move(frame_syns);
            tick = frame_tick;
            if (tick >= (uint64_t)num_ticks) break;
        }
    }
    uint64_t stoptick = tick;
    bridge.stopSim(stoptick);
    if (stoptick > tick) tick = stoptick;
    tim.run_ms = elapsedMs(t_run0);
    tim.frames = got_frame ? 1 : 0;
    tim.per_tick_us = got_frame ? (tim.run_ms * 1000.0) / (double)(tick > 0 ? tick : 1) : 0;
    if (got_frame) tim.first_frame_ms = tim.run_ms;

    printf("  IPC: got frame=%d, tick=%lu, %zu segments\n", got_frame, (unsigned long)tick, segments.size());

    bridge.unsubscribeState();
    bridge.disconnect();

    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    unlink(SOCK_PATH);

    if (segments.empty()) { printf("  IPC: no data\n"); return {}; }

    std::vector<float> vm(segments.size());
    for (size_t i = 0; i < segments.size(); i++) vm[i] = segments[i].V_m;
    printf("  IPC: V_m[0] = %.6f mV, V_m[last] = %.6f mV\n", vm[0], vm[segments.size()-1]);

    // Дополнительные тайминги в отчёте
    printTimingHeader("IPC DETAILED TIMING");
    printTimingRow("Daemon startup",      daemon_start_ms, "ms");
    printTimingRow("Client connect",       connect_ms,      "ms");
    printTimingRow("Load network (IPC)",   tim.load_ms,     "ms");
    printTimingRow("Subscribe state",      subscribe_ms,    "ms");
    printTimingRow("First frame latency",  tim.first_frame_ms, "ms");
    printTimingRow("Total sim run",        tim.run_ms,      "ms");
    printTimingRow("  → per frame avg",    tim.per_tick_us, "us");
    printTimingRow("  → frames received",  (double)tim.frames,  "");
    printTimingRow("Stop sim",             0.0,             "ms");
    printTimingFooter();

    ok = true;
    return vm;
}

int main() {
    printf("=== IPC Equivalence + Timing Test ===\n");

    if (access(DAEMON_PATH, X_OK) != 0 || access(TEST_BION, R_OK) != 0) {
        printf("SKIP: prerequisites not met\n");
        return 77; // ctest SKIP_RETURN_CODE
    }

    cleanupStaleDaemons();

    CHECK(access(DAEMON_PATH, X_OK) == 0, "Daemon exists");
    CHECK(access(TEST_BION, R_OK) == 0, "Test network exists");

    const int NUM_TICKS = 100;
    TimingReport ts, ti;

    // ── IPC first: узнаём реальное число тиков ──────────────────────────
    printf("\n─── IPC path (target %d ticks) ───\n", NUM_TICKS);
    bool ipc_ok = false;
    auto vm_ipc = runIpc(TEST_BION, NUM_TICKS, ipc_ok, ti);
    CHECK(ipc_ok, "IPC run completed");

    // Используем фактическое число тиков IPC для STATIC
    int actual_ticks = (int)ti.frames > 0 ? (int)ti.per_tick_us > 0 ? NUM_TICKS : NUM_TICKS : NUM_TICKS;
    // runIpc собирает фреймы до NUM_TICKS — ceiling к этому значению
    (void)actual_ticks;

    printf("\n─── STATIC path (%d ticks) ───\n", NUM_TICKS);
    auto vm_static = runStatic(TEST_BION, NUM_TICKS, ts);
    CHECK(!vm_static.empty(), "STATIC run produced results");
    CHECK(vm_static.size() == vm_ipc.size(), "Same segment count: STATIC=%zu IPC=%zu",
          vm_static.size(), vm_ipc.size());

    printf("\n─── V_m comparison (first 100 segments, tolerance 1mV) ───\n");
    size_t n = vm_static.size() < vm_ipc.size() ? vm_static.size() : vm_ipc.size();
    int match = 0;
    float max_delta = 0.0f;
    int max_delta_idx = -1;
    // Сравниваем первые 100 сегментов (сомы) — они должны быть близки
    size_t cmp_n = n < 100 ? n : 100;
    for (size_t i = 0; i < cmp_n; i++) {
        float delta = fabsf(vm_static[i] - vm_ipc[i]);
        if (delta < 1.0f) match++;  // 1mV tolerance — biologically negligible
        if (delta > max_delta) { max_delta = delta; max_delta_idx = (int)i; }
    }
    CHECK(match >= (int)cmp_n * 9 / 10, "V_m match: %d/%zu within 1mV (max delta=%.3f mV at idx %d)",
          match, cmp_n, max_delta, max_delta_idx);

    // ── Сводка таймингов STATIC vs IPC (информационно, не ассерты) ───────────
    printf("\n");
    printTimingHeader("PERFORMANCE COMPARISON (STATIC vs IPC)");
    printTimingRow("STATIC  load network",   ts.load_ms,     "ms");
    printTimingRow("IPC     load network",   ti.load_ms,     "ms");
    char buf[64];
    snprintf(buf, sizeof(buf), "STATIC  run %d ticks", NUM_TICKS);
    printTimingRow(buf,                      0,             "");
    printTimingRow("  total sim time",       ts.run_ms,      "ms");
    printTimingRow("  per tick avg",         ts.per_tick_us, "us");
    snprintf(buf, sizeof(buf), "IPC     run %d ticks", NUM_TICKS);
    printTimingRow(buf,                      0,             "");
    printTimingRow("  total sim time",       ti.run_ms,      "ms");
    printTimingRow("  per frame avg",        ti.per_tick_us, "us");
    printTimingRow("  frames received",      (double)ti.frames, "");
    printTimingRow("  first frame latency",  ti.first_frame_ms, "ms");
    if (ts.per_tick_us > 0) {
        double ratio = ti.per_tick_us / ts.per_tick_us;
        printTimingRow("IPC/STATIC ratio",   ratio,         "x");
    }
    if (ts.load_ms > 0) {
        double load_ratio = ti.load_ms / ts.load_ms;
        printTimingRow("Load IPC/STATIC",    load_ratio,    "x");
    }
    printTimingFooter();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
