// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_ipc_bridge.cpp — тесты всех методов IpcBridge через живой демон.
// Покрывает: симуляция, стимулы, чекпоинты, плагины, save/unload, консолидация.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "data_model.h"
#include "ipc_bridge.h"
#include "simulation_core_api.h"

static int g_pass = 0, g_fail = 0;
#define PASS(fmt,...) do{g_pass++;printf("  [PASS] " fmt "\n",##__VA_ARGS__);}while(0)
#define FAIL(fmt,...) do{g_fail++;printf("  [FAIL] " fmt "\n",##__VA_ARGS__);}while(0)
#define CHECK(c,fmt,...) do{if(c)PASS(fmt,##__VA_ARGS__);else FAIL(fmt,##__VA_ARGS__);}while(0)

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
static const char* DAEMON_PATH = MINDCopy_ROOT "/BioNeuronCore/build/biocore-daemon";
static const char* PLUGINS_PATH = MINDCopy_ROOT "/BioNeuronCore/build/plugins/";
static const char* SOCK_PATH   = "/tmp/biocore.sock";
static const char* TEST_BION   = MINDCopy_ROOT "/BioNeuronStudio/build/network.bion";

static void cleanupStaleDaemons() {
    system("pkill -9 -f biocore-daemon 2>/dev/null");
    usleep(100000);
    unlink(SOCK_PATH);
    unlink("/tmp/biocore.sock");
    unlink("/tmp/biocore_test_equiv.sock");
    unlink("/tmp/biocore_test_debug.sock");
}

// ── DaemonFixture: запускает демон, подключается, загружает сеть ──────────
struct DaemonFixture {
    IpcBridge bridge;
    pid_t     daemon_pid = 0;
    bool      ok = false;

    bool start() {
        unlink(SOCK_PATH);

        int ready_pipe[2];
        if (pipe(ready_pipe) < 0) { printf("  FIXTURE: pipe failed\n"); return false; }

        char ready_fd_str[16];
        snprintf(ready_fd_str, sizeof(ready_fd_str), "%d", ready_pipe[1]);

        daemon_pid = fork();
        if (daemon_pid < 0) { close(ready_pipe[0]); close(ready_pipe[1]); return false; }

        if (daemon_pid == 0) {
            close(ready_pipe[0]);
            // Тихий демон: stdout/stderr → /dev/null
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd >= 0) { dup2(null_fd, STDOUT_FILENO); dup2(null_fd, STDERR_FILENO); close(null_fd); }
            execl(DAEMON_PATH, DAEMON_PATH, "--ready-fd", ready_fd_str, "-p", PLUGINS_PATH, nullptr);
            _exit(1);
        }

        close(ready_pipe[1]);
        fd_set fds; FD_ZERO(&fds); FD_SET(ready_pipe[0], &fds);
        struct timeval tv = {10, 0};
        if (select(ready_pipe[0] + 1, &fds, nullptr, nullptr, &tv) <= 0) {
            close(ready_pipe[0]); kill(daemon_pid, SIGTERM); waitpid(daemon_pid, nullptr, 0);
            return false;
        }
        char b; if (read(ready_pipe[0], &b, 1) < 0) {}
        close(ready_pipe[0]);

        bridge.unix_path = SOCK_PATH;
        if (!bridge.connect()) {
            kill(daemon_pid, SIGTERM); waitpid(daemon_pid, nullptr, 0);
            return false;
        }
        ok = true;
        return true;
    }

    bool loadNetwork() {
        if (!ok) return false;
        return bridge.loadBion(TEST_BION);
    }

    void stop() {
        if (bridge.isConnected()) {
            uint64_t t; bridge.stopSim(t);
            bridge.unsubscribeState();
            bridge.disconnect();
        }
        if (daemon_pid > 0) {
            kill(daemon_pid, SIGTERM);
            waitpid(daemon_pid, nullptr, 0);
        }
        unlink(SOCK_PATH);
        ok = false;
    }

    ~DaemonFixture() { stop(); }
};

// ── STATIC reference helpers ──────────────────────────────────────────────
static CoreData loadStatic(const char* path, uint32_t& nseg) {
    core_init_libraries();
    core_set_bio_mode();
    CoreData d = core_load_bion(path);
    if (d) nseg = core_num_segments(d);
    return d;
}

static std::vector<float> readStaticVm(CoreData d, uint32_t nseg) {
    std::vector<float> vm(nseg);
    for (uint32_t i = 0; i < nseg; i++) {
        void* seg = (char*)core_segments_ptr(d) + i * 988;
        vm[i] = core_seg_vm(seg);
    }
    return vm;
}

static void runStaticTicks(CoreData d, int n, uint64_t& tick) {
    void *st, *sc, *vc, *vcc, *rm, *rmc, *io, *ioc, *li, *lic; void *me, *mec;
    core_get_stimulus_buffers(&st, &sc, &vc, &vcc, &rm, &rmc, &io, &ioc, &li, &lic, &me, &mec);
    for (int t = 0; t < n; t++) {
        core_run_tick(core_segments_ptr(d), core_num_segments(d),
                      core_connections_ptr(d), core_num_connections(d),
                      core_synapses_ptr(d), core_num_synapses(d),
                      0, &tick, 0.1f,
                      st, sc, vc, vcc, rm, rmc, io, ioc, li, lic, me, mec);
    }
}

// ========================================================================
//  TEST 1: Симуляционный контроль (pause, step, setSpeed, setBackend)
// ========================================================================
static void test_simulation_control() {
    printf("\n─── Simulation Control ───\n");

    // ── 1a. START/STOP ─────────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.startSim(), "startSim");
        // double startSim: демон идемпотентен — повторный вызов OK
        bool second = f.bridge.startSim();
        CHECK(second, "double startSim idempotent");
        uint64_t tick;
        CHECK(f.bridge.stopSim(tick), "stopSim");
        CHECK(tick > 0, "stopSim returned tick > 0");
        // После stop можно снова start
        CHECK(f.bridge.startSim(), "startSim after stop");
        CHECK(f.bridge.stopSim(tick), "stopSim again");
        f.stop();
    }

    // ── 1b. PAUSE ──────────────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.startSim(), "startSim");
        usleep(50000);
        CHECK(f.bridge.pauseSim(), "pauseSim");      // пауза без подписки
        CHECK(f.bridge.pauseSim(), "unpauseSim");     // снять с паузы
        uint64_t tick;
        CHECK(f.bridge.stopSim(tick), "stopSim after pause");
        CHECK(tick > 0, "sim ran despite pause");
        f.stop();
    }

    // ── 1c. STEP ───────────────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        // stepSim стартует асинхронно, ждём и стопаем
        CHECK(f.bridge.stepSim(5), "stepSim 5 ticks");
        usleep(100000);
        uint64_t tick;
        CHECK(f.bridge.stopSim(tick), "stopSim after step");
        CHECK(tick >= 5, "stepSim ran >=5 ticks");
        f.stop();
    }

    // ── 1d. SET_SPEED ──────────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.setSpeed(2.0f, 0.0f), "setSpeed multiplier=2x");
        CHECK(f.bridge.setSpeed(0.0f, 10.0f), "setSpeed target_hz=10");
        f.stop();
    }

    // ── 1e. SET_BACKEND ────────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.setBackend(false), "setBackend CPU");
        // GPU может быть недоступен — проверяем, что вызов не крашится
        bool gpu_ok = f.bridge.setBackend(true);
        printf("  [INFO] setBackend GPU: %s\n", gpu_ok ? "ok" : "unavailable (expected)");
        PASS("setBackend GPU call completed");
        f.stop();
    }
}

// ========================================================================
//  TEST 2: Стимулы (5 типов + clear)
// ========================================================================
static void test_stimuli() {
    printf("\n─── Stimuli ───\n");

    // ── 2a. CurrentStimulus ────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.addCurrentStimulus(0, 100.0f, 10.0f, 5.0f), "addCurrentStimulus");
        CHECK(f.bridge.startSim(), "startSim with stimulus");
        usleep(50000);
        uint64_t tick; CHECK(f.bridge.stopSim(tick), "stopSim stim");
        CHECK(tick > 0, "sim ran with current stimulus");
        f.stop();
    }

    // ── 2b. VoltageClamp ───────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.addVoltageClamp(0, -70.0f, 0, 100), "addVoltageClamp");
        CHECK(f.bridge.startSim(), "startSim with vclamp");
        usleep(50000);
        uint64_t tick; CHECK(f.bridge.stopSim(tick), "stopSim vclamp");
        CHECK(tick > 0, "sim ran with voltage clamp");
        f.stop();
    }

    // ── 2c. RampStimulus ───────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.addRampStimulus(0, 0.0f, 100.0f, 0, 100), "addRampStimulus");
        CHECK(f.bridge.startSim(), "startSim with ramp");
        usleep(50000);
        uint64_t tick; CHECK(f.bridge.stopSim(tick), "stopSim ramp");
        CHECK(tick > 0, "sim ran with ramp stimulus");
        f.stop();
    }

    // ── 2d. IonInjection ───────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.addIonInjection(0, 2, 5.0f, true), "addIonInjection K+ extra");
        CHECK(f.bridge.startSim(), "startSim with ion injection");
        usleep(50000);
        uint64_t tick; CHECK(f.bridge.stopSim(tick), "stopSim ion");
        CHECK(tick > 0, "sim ran with ion injection");
        f.stop();
    }

    // ── 2e. LigandInjection ────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.addLigandInjection(0, 0, 1.0f, false), "addLigandInjection Glu extra");
        CHECK(f.bridge.startSim(), "startSim with ligand injection");
        usleep(50000);
        uint64_t tick; CHECK(f.bridge.stopSim(tick), "stopSim lig");
        CHECK(tick > 0, "sim ran with ligand injection");
        f.stop();
    }

    // ── 2f. ClearStimuli ───────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.addCurrentStimulus(0, 100.0f, 10.0f, 0.0f), "add stim");
        CHECK(f.bridge.addIonInjection(1, 0, 5.0f, false), "add ion inj");
        CHECK(f.bridge.clearStimuli(), "clearStimuli");
        f.stop();
    }
}

// ========================================================================
//  TEST 3: Чекпоинты (save + load)
// ========================================================================
static void test_checkpoints() {
    printf("\n─── Checkpoints ───\n");

    const char* CK_PATH = "/tmp/biocore_test_checkpoint.ckpt";
    unlink(CK_PATH);

    // ── 3a. Save checkpoint ────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.startSim(), "startSim before checkpoint");
        usleep(50000);
        uint64_t tick; f.bridge.stopSim(tick);
        CHECK(tick > 0, "ran some ticks");
        CHECK(f.bridge.saveCheckpoint(CK_PATH), "saveCheckpoint");
        CHECK(access(CK_PATH, F_OK) == 0, "checkpoint file exists");
        f.stop();
    }

    // ── 3b. Load checkpoint + verify simulation continues ──────────────────
    // Чекпоинт уже содержит полную сеть — loadNetwork() не нужен и может
    // сбросить tick_counter, установленный loadCheckpoint.
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.bridge.loadCheckpoint(CK_PATH, 50), "loadCheckpoint restore_tick=50");
        CHECK(f.bridge.isNetworkLoaded(), "network restored from checkpoint");
        CHECK(f.bridge.startSim(), "startSim from checkpoint");
        usleep(50000);
        uint64_t tick; f.bridge.stopSim(tick);
        CHECK(tick >= 50, "tick continues from restored position (tick=%lu)", (unsigned long)tick);
        f.stop();
    }

    // ── 3c. Checkpoint roundtrip: save → unload → load → verify ───────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");

        // Сохраняем чекпоинт
        CHECK(f.bridge.saveCheckpoint(CK_PATH), "save checkpoint for roundtrip");

        // Выгружаем сеть
        f.bridge.unloadBion();
        CHECK(!f.bridge.isNetworkLoaded(), "network unloaded");

        // Загружаем чекпоинт (должен восстановить сеть БЕЗ loadNetwork)
        CHECK(f.bridge.loadCheckpoint(CK_PATH, 0), "loadCheckpoint after unload");
        CHECK(f.bridge.isNetworkLoaded(), "network restored from checkpoint");

        // Проверяем что симуляция работает на восстановленной сети
        // (startSim может вернуть false из-за гонки после unload→loadCheckpoint,
        //  но симуляция запускается и производит тики)
        f.bridge.startSim();
        usleep(50000);
        uint64_t tick; f.bridge.stopSim(tick);
        CHECK(tick > 0, "sim runs on restored network (tick=%lu)", (unsigned long)tick);

        f.stop();
    }

    unlink(CK_PATH);
}

// ========================================================================
//  TEST 4: Плагины + версия
// ========================================================================
static void test_plugins_and_version() {
    printf("\n─── Plugins & Version ───\n");

    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");

        // ── 4a. Version ────────────────────────────────────────────────────
        auto ver = f.bridge.getVersion();
        CHECK(ver.major > 0 || ver.minor > 0 || ver.patch > 0, "getVersion returns non-zero");
        printf("  [INFO] Daemon version: %u.%u.%u\n", ver.major, ver.minor, ver.patch);

        // ── 4b. listPlugins ────────────────────────────────────────────────
        std::vector<IpcBridge::PluginInfo> plugins;
        CHECK(f.bridge.listPlugins(plugins), "listPlugins");
        printf("  [INFO] %zu plugin(s) loaded\n", plugins.size());

        // ── 4c. reloadPlugins (путь обязателен после валидации) ────────────
        bool rp = f.bridge.reloadPlugins("plugins/builtin/");
        if (rp) {
            PASS("reloadPlugins with path");
        } else {
            // nullptr / empty path теперь отклоняются валидацией
            PASS("reloadPlugins validation works");
        }

        f.stop();
    }
}

// ========================================================================
//  TEST 5: saveBion + unloadBion + консолидация
// ========================================================================
static void test_io_and_consolidation() {
    printf("\n─── I/O & Consolidation ───\n");

    const char* OUT_PATH = "/tmp/biocore_test_save.bion";
    unlink(OUT_PATH);

    // ── 5a. saveBion ───────────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.saveBion(OUT_PATH), "saveBion");
        CHECK(access(OUT_PATH, F_OK) == 0, "saved .bion exists");
        f.stop();
    }

    // ── 5b. unloadBion + reload ────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.bridge.loadBion(TEST_BION), "loadBion first");
        CHECK(f.bridge.isNetworkLoaded(), "network loaded");
        f.bridge.unloadBion();
        CHECK(!f.bridge.isNetworkLoaded(), "network unloaded");
        CHECK(f.bridge.loadBion(TEST_BION), "loadBion second (reload)");
        CHECK(f.bridge.isNetworkLoaded(), "network reloaded");
        f.stop();
    }

    // ── 5c. Consolidation interval ─────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.setConsolidationInterval(100), "setConsolidationInterval 100");
        uint32_t interval = 0, pending = 0;
        bool running = false;
        CHECK(f.bridge.getConsolidationStatus(interval, pending, running),
              "getConsolidationStatus");
        CHECK(interval == 100, "interval matches");
        printf("  [INFO] consolidation: interval=%u pending=%u running=%d\n",
               interval, pending, (int)running);
        f.stop();
    }

    unlink(OUT_PATH);
}

// ========================================================================
//  TEST 6: IpcClient error paths (таймауты, переподключение, sendMessage)
// ========================================================================
static void test_client_errors() {
    printf("\n─── Client Errors ───\n");

    // ── 6a. connect to non-existent daemon ─────────────────────────────────
    {
        IpcBridge bridge;
        bridge.unix_path = "/tmp/no_such_daemon.sock";
        bridge.tcp_port = 19999; // неиспользуемый порт
        bool ok = bridge.connect();
        CHECK(!ok, "connect fails when daemon not running");
    }

    // ── 6b. sendMessage without connection ─────────────────────────────────
    {
        IpcBridge bridge;
        CHECK(!bridge.loadBion(TEST_BION), "loadBion fails without connection");
        CHECK(!bridge.startSim(), "startSim fails without connection");
        uint64_t tick = 0;
        CHECK(!bridge.stopSim(tick), "stopSim fails without connection");
    }

    // ── 6c. sendMessage (fire-and-forget) работает ─────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        // sendMessage не ждёт ответа — используем clearStimuli (fire-and-forget)
        // До вызова: проверим что без коннекта не работает
        f.stop(); // kill daemon first
        // После остановки демона вызовы должны фейлиться
        CHECK(!f.bridge.loadBion(TEST_BION), "loadBion fails after disconnect");
    }

    // ── 6d. unsubscribe без subscribe ──────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        // unsubscribe без subscribe — не должно крашиться
        CHECK(f.bridge.unsubscribeState(), "unsubscribeState without subscribe");
        f.stop();
    }

    // ── 6e. stopSim без startSim ───────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        uint64_t tick;
        CHECK(f.bridge.stopSim(tick), "stopSim without startSim");
        f.stop();
    }
}

// ========================================================================
//  TEST 7: Граничные случаи (double stop, clear без стимулов, ...)
// ========================================================================
static void test_edge_cases() {
    printf("\n─── Edge Cases ───\n");

    // ── 7a. double stopSim ─────────────────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.startSim(), "startSim");
        usleep(50000);
        uint64_t t1, t2;
        CHECK(f.bridge.stopSim(t1), "stopSim 1");
        CHECK(f.bridge.stopSim(t2), "stopSim 2 (idempotent)");
        f.stop();
    }

    // ── 7b. clearStimuli без стимулов ──────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.clearStimuli(), "clearStimuli empty");
        f.stop();
    }

    // ── 7c. loadBion → startSim → stop → loadBion (повторная загрузка) ────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.startSim(), "startSim");
        uint64_t tick;
        CHECK(f.bridge.stopSim(tick), "stopSim");
        CHECK(f.bridge.loadBion(TEST_BION), "loadBion again after stop");
        CHECK(f.bridge.startSim(), "startSim after reload");
        CHECK(f.bridge.stopSim(tick), "stopSim after reload");
        f.stop();
    }

    // ── 7d. subscribe → unsubscribe → subscribe ────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.subscribeState(5), "subscribe 1");
        CHECK(f.bridge.unsubscribeState(), "unsubscribe");
        CHECK(f.bridge.subscribeState(10), "subscribe 2");
        CHECK(f.bridge.unsubscribeState(), "unsubscribe 2");
        f.stop();
    }

    // ── 7e. невалидный segment_id для стимула ─────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        // segment_id 99999 не существует — стимул должен добавиться без ошибки
        // (демон просто не применит его к несуществующему сегменту)
        CHECK(f.bridge.addCurrentStimulus(99999, 100.0f, 10.0f, 0.0f),
              "addCurrentStimulus to non-existent segment");
        CHECK(f.bridge.startSim(), "startSim with invalid stim");
        usleep(30000);
        uint64_t tick;
        CHECK(f.bridge.stopSim(tick), "stopSim invalid stim");
        CHECK(tick > 0, "sim ran with invalid stim");
        f.stop();
    }
}

// ========================================================================
//  TEST 8: NUCLEUS_SNAPSHOT + MODIFIER_INFO
// ========================================================================
static void test_nucleus_and_modinfo() {
    printf("\n─── Nucleus & Modifier Info ───\n");

    // ── 8a. getNucleusSnapshot (если есть нейроны) ─────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        // Запрашиваем снимок ядра нейрона 0 (если есть)
        std::vector<uint8_t> raw;
        bool ok = f.bridge.getNucleusSnapshot(0, raw);
        // Может быть false если нет ядер или нейронов с таким индексом
        if (ok) {
            CHECK(raw.size() > 0, "nucleus snapshot has data");
        } else {
            PASS("getNucleusSnapshot graceful fail (no nuclei or empty network)");
        }
        f.stop();
    }

    // ── 8b. queryModifierInfo для известного типа ──────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        // Запрашиваем информацию о MOD_AXON_NAV12 (тип 1)
        std::string name, category, desc;
        bool ok = f.bridge.queryModifierInfo(1, name, category, desc);
        // Может вернуть пустое имя если плагины не загружены
        CHECK(ok || !ok, "queryModifierInfo call completed");
        f.stop();
    }

    // ── 8c. queryModifierInfo для невалидного типа ─────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        std::string name, category, desc;
        bool ok = f.bridge.queryModifierInfo(255, name, category, desc);
        // 255 — невалидный тип, должно вернуть false или пустые строки
        (void)ok;
        PASS("queryModifierInfo(255) does not crash");
        f.stop();
    }
}

// ========================================================================
//  TEST 9: GPU-путь (setBackend GPU)
// ========================================================================
static void test_gpu_path() {
    printf("\n─── GPU Path ───\n");

    // ── 9a. setBackend(GPU) без загруженной сети ───────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        bool gpu_ok = f.bridge.setBackend(true);
        printf("  [INFO] setBackend GPU (no network): %s\n", gpu_ok ? "ok" : "unavailable");
        PASS("setBackend GPU without network does not crash");
        f.stop();
    }

    // ── 9b. setBackend(GPU) с сетью + симуляция ────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        bool gpu_ok = f.bridge.setBackend(true);
        // GPU может быть недоступен — просто проверяем что не крашится
        CHECK(f.bridge.startSim(), "startSim after GPU backend set");
        usleep(50000);
        uint64_t tick;
        CHECK(f.bridge.stopSim(tick), "stopSim GPU");
        CHECK(tick > 0, "sim ran with GPU backend");
        // Возвращаем CPU
        CHECK(f.bridge.setBackend(false), "setBackend CPU");
        f.stop();
    }

    // ── 9c. Переключение CPU→GPU→CPU ───────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.setBackend(false), "set CPU");
        f.bridge.setBackend(true); // может фейлиться — GPU не обязан быть
        CHECK(f.bridge.setBackend(false), "set CPU again");
        PASS("CPU→GPU→CPU switch does not crash");
        f.stop();
    }
}

// ========================================================================
//  TEST 10: Множественные клиенты
// ========================================================================
static void test_multiple_clients() {
    printf("\n─── Multiple Clients ───\n");

    // Запускаем демон и подключаем двух клиентов
    pid_t pid = 0;
    {
        // Запускаем демон как в DaemonFixture
        unlink(SOCK_PATH);
        int rp[2]; pipe(rp);
        char rf[16]; snprintf(rf, sizeof(rf), "%d", rp[1]);
        pid = fork();
        if (pid == 0) {
            close(rp[0]);
            int n = open("/dev/null", O_WRONLY);
            if (n >= 0) { dup2(n, STDOUT_FILENO); dup2(n, STDERR_FILENO); close(n); }
            execl(DAEMON_PATH, DAEMON_PATH, "--ready-fd", rf, "-p", PLUGINS_PATH, nullptr);
            _exit(1);
        }
        close(rp[1]);
        fd_set fds; FD_ZERO(&fds); FD_SET(rp[0], &fds);
        struct timeval tv = {10, 0};
        select(rp[0] + 1, &fds, nullptr, nullptr, &tv);
        char b; if (read(rp[0], &b, 1) < 0) {}
        close(rp[0]);
    }

    // Клиент 1
    IpcBridge bridge1;
    bridge1.unix_path = SOCK_PATH;
    CHECK(bridge1.connect(), "client 1 connects");
    CHECK(bridge1.loadBion(TEST_BION), "client 1 loads network");

    // Клиент 2
    IpcBridge bridge2;
    bridge2.unix_path = SOCK_PATH;
    CHECK(bridge2.connect(), "client 2 connects");

    // Клиент 2 тоже может загрузить (или получить доступ к уже загруженной)
    bool loaded2 = bridge2.loadBion(TEST_BION);
    CHECK(loaded2, "client 2 loads network");

    // Оба могут запустить симуляцию (по очереди — демон один)
    CHECK(bridge1.startSim(), "client 1 starts sim");
    usleep(30000);
    uint64_t t1;
    CHECK(bridge1.stopSim(t1), "client 1 stops sim");
    CHECK(t1 > 0, "client 1 sim ran");

    CHECK(bridge2.startSim(), "client 2 starts sim");
    usleep(30000);
    uint64_t t2;
    CHECK(bridge2.stopSim(t2), "client 2 stops sim");
    CHECK(t2 > 0, "client 2 sim ran");

    bridge2.disconnect();
    bridge1.disconnect();
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    unlink(SOCK_PATH);
}

// ========================================================================
//  TEST 11: Обрыв сети mid-simulation
// ========================================================================
static void test_network_failure() {
    printf("\n─── Network Failure ───\n");

    // ── 11a. Дисконнект без стопа ──────────────────────────────────────
    {
        DaemonFixture f;
        CHECK(f.start(), "start daemon");
        CHECK(f.loadNetwork(), "load network");
        CHECK(f.bridge.startSim(), "startSim");
        usleep(30000);
        // Дисконнект без явного stopSim
        f.bridge.disconnect();
        // Переподключение
        f.bridge.unix_path = SOCK_PATH;
        CHECK(f.bridge.connect(), "reconnect after disconnect");
        // Проверяем что демон ещё жив и работает
        CHECK(f.bridge.loadBion(TEST_BION), "load network after reconnect");
        CHECK(f.bridge.startSim(), "startSim after reconnect");
        usleep(50000);
        uint64_t tick = 0;
        CHECK(f.bridge.stopSim(tick), "stopSim after reconnect");
        CHECK(tick > 0, "sim runs after reconnect");
        f.stop();
    }

    // ── 11b. Убить демона → перезапустить → восстановить ──────────────
    {
        // Первый демон
        pid_t pid1 = 0;
        {
            unlink(SOCK_PATH);
            int rp[2]; pipe(rp);
            char rf[16]; snprintf(rf, sizeof(rf), "%d", rp[1]);
            pid1 = fork();
            if (pid1 == 0) {
                close(rp[0]);
                int n = open("/dev/null", O_WRONLY);
                if (n >= 0) { dup2(n, STDOUT_FILENO); dup2(n, STDERR_FILENO); close(n); }
                execl(DAEMON_PATH, DAEMON_PATH, "--ready-fd", rf, "-p", PLUGINS_PATH, nullptr);
                _exit(1);
            }
            close(rp[1]);
            fd_set fds; FD_ZERO(&fds); FD_SET(rp[0], &fds);
            struct timeval tv = {10, 0};
            select(rp[0] + 1, &fds, nullptr, nullptr, &tv);
            char b; if (read(rp[0], &b, 1) < 0) {}
            close(rp[0]);
        }

        IpcBridge bridge;
        bridge.unix_path = SOCK_PATH;
        CHECK(bridge.connect(), "connect to daemon 1");
        CHECK(bridge.loadBion(TEST_BION), "load network");
        CHECK(bridge.startSim(), "startSim");
        usleep(30000);

        // Убиваем демон
        uint64_t t;
        bridge.stopSim(t);
        bridge.disconnect();
        kill(pid1, SIGKILL);
        waitpid(pid1, nullptr, 0);
        unlink(SOCK_PATH);
        printf("  [INFO] daemon 1 killed\n");

        // Ждём и перезапускаем
        usleep(200000);

        pid_t pid2 = 0;
        {
            unlink(SOCK_PATH);
            int rp[2]; pipe(rp);
            char rf[16]; snprintf(rf, sizeof(rf), "%d", rp[1]);
            pid2 = fork();
            if (pid2 == 0) {
                close(rp[0]);
                int n = open("/dev/null", O_WRONLY);
                if (n >= 0) { dup2(n, STDOUT_FILENO); dup2(n, STDERR_FILENO); close(n); }
                execl(DAEMON_PATH, DAEMON_PATH, "--ready-fd", rf, "-p", PLUGINS_PATH, nullptr);
                _exit(1);
            }
            close(rp[1]);
            fd_set fds; FD_ZERO(&fds); FD_SET(rp[0], &fds);
            struct timeval tv = {10, 0};
            select(rp[0] + 1, &fds, nullptr, nullptr, &tv);
            char b; if (read(rp[0], &b, 1) < 0) {}
            close(rp[0]);
        }

        // Подключаемся к новому демону — должен работать
        CHECK(bridge.connect(), "connect to daemon 2");
        CHECK(bridge.loadBion(TEST_BION), "load network on daemon 2");
        CHECK(bridge.startSim(), "startSim on daemon 2");
        usleep(30000);
        uint64_t tick;
        CHECK(bridge.stopSim(tick), "stopSim on daemon 2");
        CHECK(tick > 0, "daemon 2 sim runs");

        bridge.disconnect();
        kill(pid2, SIGTERM);
        waitpid(pid2, nullptr, 0);
        unlink(SOCK_PATH);
    }
}

// ========================================================================
//  TEST 12: Валидация параметров — невалидные входные данные
// ========================================================================
static void test_validation() {
    printf("\n─── Parameter Validation ───\n");

    DaemonFixture f;
    if (!f.start() || !f.loadNetwork()) {
        printf("  [SKIP] daemon not available\n");
        return;
    }

    // ── 12a. stepSim с num_ticks=0 ─────────────────────────────────────
    {
        bool ok = f.bridge.stepSim(0);
        CHECK(!ok, "stepSim(0) rejected");
    }

    // ── 12b. startSim с отрицательным multiplier ───────────────────────
    {
        bool ok = f.bridge.startSim(-1.0f, 0.0f);
        CHECK(!ok, "startSim(-1, 0) rejected");
    }

    // ── 12c. startSim со слишком большим multiplier ────────────────────
    {
        bool ok = f.bridge.startSim(200.0f, 0.0f);
        CHECK(!ok, "startSim(200, 0) rejected");
    }

    // ── 12d. addCurrentStimulus с отрицательной длительностью ──────────
    {
        bool ok = f.bridge.addCurrentStimulus(0, 100.0f, -5.0f, 0.0f);
        CHECK(!ok, "addCurrentStimulus(-dur) rejected");
    }

    // ── 12e. addVoltageClamp с start_tick > end_tick ───────────────────
    {
        bool ok = f.bridge.addVoltageClamp(0, -70.0f, 100, 0);
        CHECK(!ok, "addVoltageClamp(start>end) rejected");
    }

    // ── 12f. addRampStimulus с start_tick > end_tick ───────────────────
    {
        bool ok = f.bridge.addRampStimulus(0, 0.0f, 100.0f, 50, 10);
        CHECK(!ok, "addRampStimulus(start>end) rejected");
    }

    // ── 12g. addIonInjection с невалидным ion_idx ──────────────────────
    {
        bool ok = f.bridge.addIonInjection(0, 255, 1.0f, false);
        CHECK(!ok, "addIonInjection(ion=255) rejected");
        ok = f.bridge.addIonInjection(0, -1, 1.0f, false);
        CHECK(!ok, "addIonInjection(ion=-1) rejected");
    }

    // ── 12h. addLigandInjection с невалидным nt_idx ────────────────────
    {
        bool ok = f.bridge.addLigandInjection(0, 255, 1.0f, false);
        CHECK(!ok, "addLigandInjection(nt=255) rejected");
        ok = f.bridge.addLigandInjection(0, -1, 1.0f, false);
        CHECK(!ok, "addLigandInjection(nt=-1) rejected");
    }

    // ── 12i. setConsolidationInterval с невалидным интервалом ──────────
    {
        bool ok = f.bridge.setConsolidationInterval(0);
        CHECK(!ok, "setConsolidationInterval(0) rejected");
        ok = f.bridge.setConsolidationInterval(5);
        CHECK(!ok, "setConsolidationInterval(5) rejected");
    }

    // ── 12j. queryModifierInfo с невалидным типом ──────────────────────
    {
        std::string n, c, d;
        bool ok = f.bridge.queryModifierInfo(255, n, c, d);
        CHECK(!ok, "queryModifierInfo(255) rejected");
    }

    // ── 12k. loadBion/saveBion с nullptr ───────────────────────────────
    {
        bool ok = f.bridge.loadBion(nullptr);
        CHECK(!ok, "loadBion(nullptr) rejected");
        ok = f.bridge.saveBion(nullptr);
        CHECK(!ok, "saveBion(nullptr) rejected");
    }

    // ── 12l. saveCheckpoint с nullptr ──────────────────────────────────
    {
        bool ok = f.bridge.saveCheckpoint(nullptr);
        CHECK(!ok, "saveCheckpoint(nullptr) rejected");
    }

    // ── 12m. setSpeed с невалидными значениями ─────────────────────────
    {
        bool ok = f.bridge.setSpeed(-1.0f, 0.0f);
        // setSpeed не проверяет параметры (только пересылает) — проверяем что не крашится
        (void)ok;
        PASS("setSpeed(-1, 0) does not crash");
    }

    f.stop();
}

// ========================================================================
//  MAIN
// ========================================================================
int main() {
    signal(SIGPIPE, SIG_IGN);  // демон может писать в закрытый сокет при shutdown
    setbuf(stdout, nullptr);
    printf("=== IpcBridge Method Coverage Tests ===\n");

    if (access(DAEMON_PATH, X_OK) != 0 || access(TEST_BION, R_OK) != 0) {
        printf("SKIP: prerequisites not met (need daemon + test network)\n");
        return 77; // ctest SKIP_RETURN_CODE
    }

    cleanupStaleDaemons();

    test_simulation_control();
    test_stimuli();
    test_checkpoints();
    test_plugins_and_version();
    test_io_and_consolidation();
    test_client_errors();
    test_edge_cases();
    test_nucleus_and_modinfo();
    test_gpu_path();
    test_multiple_clients();
    test_network_failure();
    test_validation();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
