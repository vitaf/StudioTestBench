// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_mcp_memory.cpp — тест на утечки памяти при длительной работе Studio.
// Запускает Studio в фоне (fork/exec), мониторит RSS, проверяет стабильность.
// Требует: собранный BioNeuronStudio, DISPLAY=:1

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>  // socketpair
#include <pty.h>     // openpty
#include <termios.h>  // tcgetattr/tcsetattr, ECHO

// ── Хелпер: чтение RSS из /proc ──────────────────────────────────────
static long read_rss_kb(pid_t pid) {
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    long rss = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            rss = strtol(line + 6, nullptr, 10);
            break;
        }
    }
    fclose(f);
    return rss;
}

// ── Поиск бинарника Studio ───────────────────────────────────────────
static const char* find_studio() {
    static const char* candidates[] = {
        "./BioNeuronStudio",
        "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio",
    };
    for (auto c : candidates) {
        if (access(c, X_OK) == 0) return c;
    }
    return nullptr;
}

// ── MCP хелпер: два pipe вместо PTY (без блокировок и terminal-семантики) ─
struct PtyMCP {
    int stdin_fd = -1;    // пишем MCP-команды ребёнку
    int stdout_fd = -1;   // читаем MCP-ответы от ребёнка
    pid_t child_pid = 0;

    ~PtyMCP() { disconnect(); }

    bool start(const char* studio_path) {
        int stdin_pipe[2], stdout_pipe[2];
        if (pipe(stdin_pipe) != 0) return false;
        if (pipe(stdout_pipe) != 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); return false; }

        pid_t pid = fork();
        if (pid == -1) {
            close(stdin_pipe[0]); close(stdin_pipe[1]);
            close(stdout_pipe[0]); close(stdout_pipe[1]);
            return false;
        }

        if (pid == 0) {
            close(stdin_pipe[1]);   // пишущий конец stdin
            close(stdout_pipe[0]);  // читающий конец stdout
            setpgid(0, 0);
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            int err_fd = open("/tmp/studio_mcp_stderr.log", O_WRONLY|O_CREAT|O_TRUNC, 0644);
            if (err_fd >= 0) { dup2(err_fd, STDERR_FILENO); close(err_fd); }
            if (stdin_pipe[0] > 2) close(stdin_pipe[0]);
            if (stdout_pipe[1] > 2) close(stdout_pipe[1]);
            setenv("DISPLAY", ":99", 1);
            execl(studio_path, studio_path, "--mcp", nullptr);
            _exit(1);
        }

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        // Неблокирующий режим на stdout pipe: read_json() не зависнет в read()
        fcntl(stdout_pipe[0], F_SETFL, fcntl(stdout_pipe[0], F_GETFL) | O_NONBLOCK);
        stdin_fd = stdin_pipe[1];
        stdout_fd = stdout_pipe[0];
        child_pid = pid;
        return true;
    }

    void send(const std::string& json) {
        std::string msg = json + "\n";
        write(stdin_fd, msg.c_str(), msg.size());
    }

    bool read_json(std::string& resp, int timeout_sec = 30) {
        char buf[65536];
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
        while (std::chrono::steady_clock::now() < deadline) {
            int pos = 0;
            while (pos < (int)sizeof(buf) - 1) {
                char c;
                int n = read(stdout_fd, &c, 1);
                if (n <= 0) { if (n == 0) return false; if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false; break; }
                if (c == '\n') break;
                buf[pos++] = c;
            }
            if (pos == 0) continue;
            buf[pos] = '\0';
            if (buf[0] == '{') { resp = buf; return true; }
        }
        return false;
    }

    bool wait_ready(int timeout_sec = 15) {
        std::string resp;
        auto t0 = std::chrono::steady_clock::now();
        bool first = true;
        while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(timeout_sec)) {
            if (first) {
                send(R"({"jsonrpc":"2.0","method":"initialize","id":1})");
                first = false;
            }
            // Быстрый дренаж: DLOG_INFO пишет ~129 строк логов в stdout ДО MCP-ответа.
            // Читаем все строки подряд пока не найдём JSON (без сна между ними).
            if (!is_alive()) { printf("  [mcp] Studio child died during init\n"); (void)system("cat /tmp/studio_mcp_stderr.log 2>/dev/null | tail -30"); return false; }
            if (read_json(resp) && resp.find("BioNeuronStudio") != std::string::npos) return true;
        }
        return false;
    }

    void disconnect() {
        if (stdin_fd >= 0)  { ::close(stdin_fd);  stdin_fd = -1; }
        if (stdout_fd >= 0) { ::close(stdout_fd); stdout_fd = -1; }
        if (child_pid > 0)  { kill(-child_pid, SIGKILL); waitpid(child_pid, nullptr, 0); child_pid = 0; }
    }

    long rss_kb() const { return read_rss_kb(child_pid); }
    bool is_alive() const { return child_pid > 0 && waitpid(child_pid, nullptr, WNOHANG) == 0; }
};

// ═══════════════════════════════════════════════════════════════════════
// Тест 1: RSS стабилен при простое (30 секунд)
// ═══════════════════════════════════════════════════════════════════════

TEST(McpMemory, RssStableIdle) {
    const char* studio_path = find_studio();
    if (!studio_path) GTEST_SKIP() << "BioNeuronStudio binary not found";

    // Pipe ДО fork — иначе родитель и ребёнок получат разные pipe
    int stdin_pipe[2];
    if (pipe(stdin_pipe) != 0) { stdin_pipe[0] = -1; stdin_pipe[1] = -1; }

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        setpgid(0, 0);
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        if (stdin_pipe[0] >= 0) {
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
        }
        if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
        setenv("DISPLAY", ":99", 1);
        execl(studio_path, studio_path, "--mcp", nullptr);
        _exit(1);
    }

    close(stdin_pipe[0]);
    // stdin_pipe[1] открыт в родителе → getchar() в ребёнке блокируется (не EOF)

    // Ждём инициализации
    std::this_thread::sleep_for(std::chrono::seconds(8));

    // Проверяем что процесс жив
    int status;
    ASSERT_EQ(waitpid(pid, &status, WNOHANG), 0) << "Studio died on startup";

    long rss_start = read_rss_kb(pid);
    ASSERT_GT(rss_start, 10000) << "RSS=" << rss_start;
    printf("  RSS idle start: %ld KB\n", rss_start);

    // Мониторим 30 секунд
    long rss_max = rss_start;
    for (int i = 0; i < 30; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        long rss = read_rss_kb(pid);
        if (rss > rss_max) rss_max = rss;
        if (i % 10 == 9) printf("  RSS idle t=%ds: %ld KB\n", i + 1, rss);
    }
    long rss_final = read_rss_kb(pid);

    long delta = rss_final - rss_start;
    long threshold = 50000;  // 50 MB
    printf("  RSS idle final: %ld KB, delta: %+ld KB, max: %ld KB\n",
           rss_final, delta, rss_max);
    EXPECT_LT(delta, threshold) << "RSS grew by " << delta << " KB in 30s idle";

    if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
    kill(-pid, SIGKILL);  // убиваем всю группу процессов (включая hardware-скрипты)
    waitpid(pid, &status, 0);
}

// ═══════════════════════════════════════════════════════════════════════
// Тест 2: RSS стабилен после повторных запусков (проверка на утечки
//         при инициализации/деинициализации)
// ═══════════════════════════════════════════════════════════════════════

TEST(McpMemory, NoLeakAfterRestart) {
    const char* studio_path = find_studio();
    if (!studio_path) GTEST_SKIP() << "BioNeuronStudio binary not found";

    long rss_after[3] = {};

    for (int run = 0; run < 3; run++) {
        pid_t pid = fork();
        ASSERT_NE(pid, -1);

        if (pid == 0) {
            setpgid(0, 0);  // своя группа процессов
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
            setenv("DISPLAY", ":99", 1);
            execl(studio_path, studio_path, "--mcp", nullptr);
            _exit(1);
        }

        std::this_thread::sleep_for(std::chrono::seconds(8));
        rss_after[run] = read_rss_kb(pid);
        printf("  RSS run %d: %ld KB\n", run + 1, rss_after[run]);

        int status;
        kill(-pid, SIGKILL);
        waitpid(pid, &status, 0);

        if (run < 2) std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    // Каждый последующий запуск не должен потреблять сильно больше предыдущего
    long delta_1_2 = rss_after[1] - rss_after[0];
    long delta_1_3 = rss_after[2] - rss_after[0];
    printf("  RSS delta run1→run2: %+ld KB, run1→run3: %+ld KB\n", delta_1_2, delta_1_3);
    EXPECT_LT(delta_1_3, 100000) << "RSS grew " << delta_1_3 << " KB across 3 restarts";
}

// ═══════════════════════════════════════════════════════════════════════
// Тест 3: полный сценарий — GPU_ALL + запись + ресайз + поворот
// ═══════════════════════════════════════════════════════════════════════

TEST(McpMemory, FullScenarioGpuAllRecordRotate) {
    const char* studio_path = find_studio();
    if (!studio_path) GTEST_SKIP() << "BioNeuronStudio binary not found";

    const char* network_path = "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/network.bion";
    if (access(network_path, R_OK) != 0) {
        GTEST_SKIP() << "Network file not found: " << network_path;
    }

    PtyMCP mcp;
    ASSERT_TRUE(mcp.start(studio_path));
    ASSERT_TRUE(mcp.wait_ready()) << "Studio MCP init failed";

    std::string resp;
    long rss_start = mcp.rss_kb();
    ASSERT_GT(rss_start, 10000);
    printf("  RSS start: %ld KB\n", rss_start);

    // ── Загрузка сети ──────────────────────────────────────────────
    printf("  Loading network: %s\n", network_path);
    std::string load_cmd = R"({"jsonrpc":"2.0","method":"tools/call","params":{)";
    load_cmd += R"("name":"studio_menu","arguments":{"path":["file","open",")";
    load_cmd += network_path;
    load_cmd += R"("]}},"id":10})";
    mcp.send(load_cmd);
    ASSERT_TRUE(mcp.read_json(resp));
    std::this_thread::sleep_for(std::chrono::seconds(15));

    long rss_loaded = mcp.rss_kb();
    printf("  RSS after load: %ld KB (+%ld KB)\n", rss_loaded, rss_loaded - rss_start);

    // ── GPU_ALL + старт симуляции ──────────────────────────────────
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","backend","gpu_all"]}},"id":11})");
    ASSERT_TRUE(mcp.read_json(resp));
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","start"]}},"id":12})");
    ASSERT_TRUE(mcp.read_json(resp));
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // ── Запись видео ───────────────────────────────────────────────
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["record","start"]}},"id":13})");
    ASSERT_TRUE(mcp.read_json(resp));
    long rss_recording = mcp.rss_kb();
    printf("  RSS recording: %ld KB (+%ld KB)\n", rss_recording, rss_recording - rss_loaded);

    // ── Поворот вьюпорта (studio_click) ────────────────────────────
    for (int angle = 0; angle < 4; angle++) {
        char cmd_buf[256];
        snprintf(cmd_buf, sizeof(cmd_buf),
                 R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_click","arguments":{"x":%d,"y":450,"button":"left"}},"id":%d})",
                 400 + angle * 200, 20 + angle);
        mcp.send(cmd_buf);
        ASSERT_TRUE(mcp.read_json(resp));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // ── Ресайз окна (xdotool) ──────────────────────────────────────
    int ret = system("xdotool search --name 'BioNeuronStudio' windowsize 1200 800 2>/dev/null");
    if (ret == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        system("xdotool search --name 'BioNeuronStudio' windowsize 1600 1000 2>/dev/null");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // ── Скрыть сеть ────────────────────────────────────────────────
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["view","show_network","false"]}},"id":30})");
    ASSERT_TRUE(mcp.read_json(resp));

    // ── Поворот на 180° ────────────────────────────────────────────
    for (int angle = 0; angle < 4; angle++) {
        char cmd_buf[256];
        snprintf(cmd_buf, sizeof(cmd_buf),
                 R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_click","arguments":{"x":%d,"y":450,"button":"left"}},"id":%d})",
                 800 - angle * 200, 40 + angle);
        mcp.send(cmd_buf);
        ASSERT_TRUE(mcp.read_json(resp));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));

    // ── Стоп ───────────────────────────────────────────────────────
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["record","stop"]}},"id":50})");
    ASSERT_TRUE(mcp.read_json(resp));
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","stop"]}},"id":51})");
    ASSERT_TRUE(mcp.read_json(resp));

    // ── Финальный RSS ──────────────────────────────────────────────
    std::this_thread::sleep_for(std::chrono::seconds(2));
    long rss_final = mcp.rss_kb();
    long delta_total = rss_final - rss_start;
    printf("  RSS final: %ld KB (total +%ld KB)\n", rss_final, delta_total);
    EXPECT_LT(delta_total, 2000000) << "Total RSS growth " << delta_total << " KB exceeds 2GB";
}

// ═══════════════════════════════════════════════════════════════════════
// Тест 4: GPU_ALL БЕЗ записи — 10 минут мониторинга RSS
// Гипотеза: если утечка в видео-тракте, то RSS будет стабилен.
//           Если утечка в GPU/OpenGL — продолжит расти.
// ═══════════════════════════════════════════════════════════════════════

TEST(McpMemory, GpuAllNoRecordingLeak) {
    const char* studio_path = find_studio();
    if (!studio_path) GTEST_SKIP() << "BioNeuronStudio binary not found";

    const char* network_path = "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/network.bion";
    if (access(network_path, R_OK) != 0) {
        GTEST_SKIP() << "Network file not found: " << network_path;
    }

    PtyMCP mcp;
    ASSERT_TRUE(mcp.start(studio_path));
    ASSERT_TRUE(mcp.wait_ready()) << "Studio MCP init failed";

    std::string resp;
    long rss_start = mcp.rss_kb();
    ASSERT_GT(rss_start, 10000);
    printf("  RSS start (idle): %ld KB\n", rss_start);

    printf("  Loading network...\n");
    std::string load_cmd = R"({"jsonrpc":"2.0","method":"tools/call","params":{)";
    load_cmd += R"("name":"studio_menu","arguments":{"path":["file","open",")";
    load_cmd += network_path;
    load_cmd += R"("]}},"id":10})";
    mcp.send(load_cmd);
    ASSERT_TRUE(mcp.read_json(resp));
    std::this_thread::sleep_for(std::chrono::seconds(15));

    long rss_loaded = mcp.rss_kb();
    printf("  RSS after load: %ld KB (+%ld KB)\n", rss_loaded, rss_loaded - rss_start);

    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","backend","gpu_all"]}},"id":11})");
    ASSERT_TRUE(mcp.read_json(resp));
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","start"]}},"id":12})");
    ASSERT_TRUE(mcp.read_json(resp));
    std::this_thread::sleep_for(std::chrono::seconds(3));

    long rss_sim_start = mcp.rss_kb();
    printf("  RSS simulation start: %ld KB (+%ld KB)\n", rss_sim_start, rss_sim_start - rss_loaded);
    printf("  Monitoring RSS for 10 minutes (NO recording)...\n");

    long rss_max = rss_sim_start;
    long rss_min = rss_sim_start;
    for (int i = 0; i < 600; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        long rss = mcp.rss_kb();
        if (rss > rss_max) rss_max = rss;
        if (rss < rss_min) rss_min = rss;

        int elapsed = i + 1;
        if (elapsed % 30 == 0) {
            long delta = rss - rss_sim_start;
            double rate_mb_per_min = (delta / 1024.0) / (elapsed / 60.0);
            printf("  t=%ds  RSS=%ld KB  delta=%+ld KB  rate=%.1f MB/min  [min=%ld max=%ld]\n",
                   elapsed, rss, delta, rate_mb_per_min, rss_min, rss_max);
        }

        if (!mcp.is_alive()) {
            printf("  ERROR: Studio died at t=%ds!\n", elapsed);
            FAIL() << "Studio crashed during test";
            break;
        }
    }

    long rss_final = mcp.rss_kb();
    long delta_total = rss_final - rss_sim_start;
    double elapsed_min = 600.0 / 60.0;
    double rate_mb_per_min = (delta_total / 1024.0) / elapsed_min;

    printf("  === FINAL ===\n");
    printf("  RSS final: %ld KB (total delta: %+ld KB = %+.1f MB)\n",
           rss_final, delta_total, delta_total / 1024.0);
    printf("  Rate: %.1f MB/min\n", rate_mb_per_min);
    printf("  RSS range: [%ld, %ld] KB\n", rss_min, rss_max);

    long threshold_kb = 500000;
    EXPECT_LT(delta_total, threshold_kb)
        << "GPU_ALL WITHOUT recording LEAK: " << delta_total << " KB in 10 min ("
        << rate_mb_per_min << " MB/min)";

    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","stop"]}},"id":50})");
    ASSERT_TRUE(mcp.read_json(resp));
}

// ═══════════════════════════════════════════════════════════════════════
// Тест 5: GPU_ALL БЕЗ рендеринга (сеть скрыта) — 10 минут мониторинга
// Гипотеза: если утечка в OpenGL-рендеринге, то без рендеринга RSS стабилен.
//           Если утечка в GPU-бэкенде/симуляции — продолжит расти.
// ═══════════════════════════════════════════════════════════════════════

TEST(McpMemory, GpuAllNoRenderLeak) {
    const char* studio_path = find_studio();
    if (!studio_path) GTEST_SKIP() << "BioNeuronStudio binary not found";

    const char* network_path = "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/network.bion";
    if (access(network_path, R_OK) != 0) {
        GTEST_SKIP() << "Network file not found: " << network_path;
    }

    PtyMCP mcp;
    ASSERT_TRUE(mcp.start(studio_path));
    ASSERT_TRUE(mcp.wait_ready()) << "Studio MCP init failed";

    std::string resp;
    long rss_start = mcp.rss_kb();
    ASSERT_GT(rss_start, 10000);
    printf("  RSS start (idle): %ld KB\n", rss_start);

    printf("  Loading network...\n");
    std::string load_cmd = R"({"jsonrpc":"2.0","method":"tools/call","params":{)";
    load_cmd += R"("name":"studio_menu","arguments":{"path":["file","open",")";
    load_cmd += network_path;
    load_cmd += R"("]}},"id":10})";
    mcp.send(load_cmd);
    ASSERT_TRUE(mcp.read_json(resp));
    std::this_thread::sleep_for(std::chrono::seconds(15));

    long rss_loaded = mcp.rss_kb();
    printf("  RSS after load: %ld KB (+%ld KB)\n", rss_loaded, rss_loaded - rss_start);
    ASSERT_GT(rss_loaded, rss_start + 200000) << "Network did NOT load!";

    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","backend","gpu_all"]}},"id":11})");
    ASSERT_TRUE(mcp.read_json(resp));
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","start"]}},"id":12})");
    ASSERT_TRUE(mcp.read_json(resp));
    std::this_thread::sleep_for(std::chrono::seconds(3));

    printf("  Hiding network (no OpenGL rendering)...\n");
    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["view","show_network","false"]}},"id":15})");
    ASSERT_TRUE(mcp.read_json(resp));
    std::this_thread::sleep_for(std::chrono::seconds(2));

    long rss_sim_start = mcp.rss_kb();
    printf("  RSS simulation start (hidden): %ld KB (+%ld KB)\n", rss_sim_start, rss_sim_start - rss_loaded);
    printf("  Monitoring RSS for 10 minutes (GPU_ALL, NO render, NO record)...\n");

    long rss_max = rss_sim_start;
    long rss_min = rss_sim_start;
    for (int i = 0; i < 600; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        long rss = mcp.rss_kb();
        if (rss > rss_max) rss_max = rss;
        if (rss < rss_min) rss_min = rss;

        int elapsed = i + 1;
        if (elapsed % 30 == 0) {
            long delta = rss - rss_sim_start;
            double rate_mb_per_min = (delta / 1024.0) / (elapsed / 60.0);
            printf("  t=%ds  RSS=%ld KB  delta=%+ld KB  rate=%.1f MB/min  [min=%ld max=%ld]\n",
                   elapsed, rss, delta, rate_mb_per_min, rss_min, rss_max);
        }

        if (!mcp.is_alive()) {
            printf("  ERROR: Studio died at t=%ds!\n", elapsed);
            FAIL() << "Studio crashed during test";
            break;
        }
    }

    long rss_final = mcp.rss_kb();
    long delta_total = rss_final - rss_sim_start;
    double elapsed_min = 600.0 / 60.0;
    double rate_mb_per_min = (delta_total / 1024.0) / elapsed_min;

    printf("  === FINAL (NO RENDER) ===\n");
    printf("  RSS final: %ld KB (total delta: %+ld KB = %+.1f MB)\n",
           rss_final, delta_total, delta_total / 1024.0);
    printf("  Rate: %.1f MB/min\n", rate_mb_per_min);
    printf("  RSS range: [%ld, %ld] KB\n", rss_min, rss_max);

    long threshold_kb = 500000;
    EXPECT_LT(delta_total, threshold_kb)
        << "GPU_ALL WITHOUT render LEAK: " << delta_total << " KB in 10 min ("
        << rate_mb_per_min << " MB/min)";

    mcp.send(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_menu","arguments":{"path":["simulation","stop"]}},"id":50})");
    ASSERT_TRUE(mcp.read_json(resp));
}

// ═══════════════════════════════════════════════════════════════════════
// Тест 6: Смена сети (большая→маленькая) — swap-идиома освобождает
//         static-векторы. Проверяем что данные не повреждаются.
// ═══════════════════════════════════════════════════════════════════════

TEST(McpMemory, NetworkSwapShrinksVectors) {
    const char* studio_path = find_studio();
    if (!studio_path) GTEST_SKIP() << "BioNeuronStudio binary not found";

    // Сети с гарантированно разным числом сегментов (генерируются gen_test_networks.py)
    // Авто-генерация тестовых .bion v9 (идемпотентно)
    (void)system("python3 /home/vitaf/workarea/develop/MindCopy/StudioTestBench/gen_test_networks.py 2>/dev/null");
    const char* net_a = "/tmp/test_net_a.bion";  // 3 сегмента
    const char* net_b = "/tmp/test_net_b.bion";  // 7 сегментов
    if (access(net_a, R_OK) != 0) GTEST_SKIP() << "Network A not found";
    if (access(net_b, R_OK) != 0) GTEST_SKIP() << "Network B not found";

    // PTY вместо pipe: дочерний процесс видит терминал → line-buffered stdout
    int master_fd, slave_fd;
    ASSERT_EQ(openpty(&master_fd, &slave_fd, nullptr, nullptr, nullptr), 0);

    // Отключаем ECHO ДО fork (tcsetattr в дочернем процессе может падать после setsid)
    struct termios t;
    ASSERT_EQ(tcgetattr(slave_fd, &t), 0) << "tcgetattr: " << strerror(errno);
    t.c_lflag &= ~ECHO;
    ASSERT_EQ(tcsetattr(slave_fd, TCSANOW, &t), 0) << "tcsetattr: " << strerror(errno);

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        close(master_fd);
        setsid();
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        // stderr → файл для отладки (НЕ в PTY, чтобы не мешать JSON-парсингу)
        int err_fd = open("/tmp/studio_swap_test_stderr.log", O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (err_fd >= 0) { dup2(err_fd, STDERR_FILENO); close(err_fd); }
        if (slave_fd > 2) close(slave_fd);
        setenv("DISPLAY", ":99", 1);
        execl(studio_path, studio_path, "--mcp", nullptr);
        _exit(1);
    }

    close(slave_fd);
    // Сырые read/write через один fd: PTY сам разделяет ввод/вывод
    int mcp_fd = master_fd;

    auto send_cmd = [&](const std::string& json) {
        std::string msg = json + "\n";
        write(mcp_fd, msg.c_str(), msg.size());
    };

    auto read_json = [&](std::string& resp) -> bool {
        char buf[65536];
        int buf_pos = 0;
        for (int i = 0; i < 2000; i++) {
            // Читаем посимвольно до \n (fgets через write-read)
            buf_pos = 0;
            while (buf_pos < (int)sizeof(buf) - 1) {
                char c;
                ssize_t n = read(mcp_fd, &c, 1);
                if (n <= 0) {
                    printf("  [mcp] EOF/error at iter=%d pos=%d: n=%zd errno=%d\n",
                           i, buf_pos, n, errno); fflush(stdout);
                    return false;
                }
                if (c == '\n') break;
                buf[buf_pos++] = c;
            }
            buf[buf_pos] = '\0';
            if (buf[0] == '{') { resp = buf; return true; }
        }
        printf("  [mcp] exceeded 2000 lines\n"); fflush(stdout);
        return false;
    };

    // Извлекает число из JSON, в том числе вложенного (экранированные кавычки)
    auto extract_int = [](const std::string& json, const char* key) -> long {
        // Пробуем прямое вхождение: "key":число
        std::string search = std::string("\"") + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) {
            // Пробуем экранированное: \"key\":число (внутри вложенного JSON-строки)
            search = std::string("\\\"") + key + "\\\":";
            pos = json.find(search);
        }
        if (pos == std::string::npos) return -1;
        pos += search.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\\')) pos++;
        return strtol(json.c_str() + pos, nullptr, 10);
    };

    // ── Шлём все команды разом (как heredoc), потом читаем ответы ──
    std::string resp;
    printf("  Sending all commands...\n"); fflush(stdout);

    // init
    send_cmd(R"({"jsonrpc":"2.0","method":"initialize","id":1})");
    // file/open net_a
    std::string cmd = R"({"jsonrpc":"2.0","method":"tools/call","params":{)";
    cmd += R"("name":"studio_menu","arguments":{"path":["file","open",")";
    cmd += net_a;
    cmd += R"("]}},"id":10})";
    send_cmd(cmd);
    // studio_get_state (до запуска симуляции — tick=0)
    send_cmd(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_get_state","arguments":{"what":"simulation"}},"id":11})");

    // Читаем ответы
    long seg_count_a = -1;
    int responses = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (responses < 3) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(15)) break;
        if (!read_json(resp)) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
        responses++;
        if (resp.find("BioNeuronStudio") != std::string::npos) {
            printf("  MCP init OK\n"); fflush(stdout);
        } else if (resp.find("\"id\":10") != std::string::npos) {
            printf("  Load A: %.200s\n", resp.c_str()); fflush(stdout);
        } else if (resp.find("\"id\":11") != std::string::npos) {
            printf("  State A raw: %.300s\n", resp.c_str()); fflush(stdout);
            seg_count_a = extract_int(resp, "total_segments");
            printf("  Network A segments: %ld\n", seg_count_a);
        }
    }
    ASSERT_GT(responses, 0) << "No MCP responses";

    int child_status;
    ASSERT_EQ(waitpid(pid, &child_status, WNOHANG), 0) << "Studio died";

    // ── Фаза 2: Загружаем сеть B ──────────────────────────────────
    printf("=== Phase 2: Loading network B (%s)\n", net_b); fflush(stdout);

    cmd = R"({"jsonrpc":"2.0","method":"tools/call","params":{)";
    cmd += R"("name":"studio_menu","arguments":{"path":["file","open",")";
    cmd += net_b;
    cmd += R"("]}},"id":20})";
    send_cmd(cmd);
    send_cmd(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_get_state","arguments":{"what":"simulation"}},"id":21})");

    long seg_count_b = -1;
    while (seg_count_b < 0) {
        if (!read_json(resp)) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        if (resp.find("\"id\":20") != std::string::npos) {
            printf("  Load B: %.200s\n", resp.c_str()); fflush(stdout);
        } else if (resp.find("\"id\":21") != std::string::npos) {
            seg_count_b = extract_int(resp, "total_segments");
            printf("  Network B segments: %ld\n", seg_count_b);
        }
    }

    EXPECT_EQ(seg_count_a, 3) << "Phase 1: Network A should have 3 segments";
    EXPECT_EQ(seg_count_b, 7) << "Phase 1: Network B should have 7 segments";
    EXPECT_NE(seg_count_a, seg_count_b)
        << "Phase 1: segment counts should differ — swap 3→7 must have triggered";
    printf("  Phase 1 PASS: %ld → %ld (swap triggered)\n", seg_count_a, seg_count_b);

    // ── Фаза 3: Загружаем сеть A снова — проверяем shrink 7→3 ──
    printf("=== Phase 3: Reload network A (3 segs) — shrink 7→3\n"); fflush(stdout);
    cmd = R"({"jsonrpc":"2.0","method":"tools/call","params":{)";
    cmd += R"("name":"studio_menu","arguments":{"path":["file","open",")";
    cmd += net_a;
    cmd += R"("]}},"id":30})";
    send_cmd(cmd);
    send_cmd(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_get_state","arguments":{"what":"simulation"}},"id":31})");

    long seg_count_a2 = -1;
    while (seg_count_a2 < 0) {
        if (!read_json(resp)) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        if (resp.find("\"id\":31") != std::string::npos) {
            seg_count_a2 = extract_int(resp, "total_segments");
            printf("  Reload A segments: %ld\n", seg_count_a2);
        }
    }
    EXPECT_EQ(seg_count_a2, 3) << "Phase 3: reloaded network A should have 3 segments (swap 7→3)";
    printf("  Phase 3 PASS: %ld → %ld (swap shrink triggered)\n", seg_count_b, seg_count_a2);

    // ── Фаза 4: Загружаем ту же сеть A — swap НЕ должен испортить данные ──
    printf("=== Phase 4: Reload same network A — no swap expected\n"); fflush(stdout);
    cmd = R"({"jsonrpc":"2.0","method":"tools/call","params":{)";
    cmd += R"("name":"studio_menu","arguments":{"path":["file","open",")";
    cmd += net_a;
    cmd += R"("]}},"id":40})";
    send_cmd(cmd);
    send_cmd(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"studio_get_state","arguments":{"what":"simulation"}},"id":41})");

    long seg_count_a3 = -1;
    while (seg_count_a3 < 0) {
        if (!read_json(resp)) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        if (resp.find("\"id\":41") != std::string::npos) {
            seg_count_a3 = extract_int(resp, "total_segments");
            printf("  Same reload segments: %ld\n", seg_count_a3);
        }
    }
    EXPECT_EQ(seg_count_a3, 3) << "Phase 4: same-size reload should keep 3 segments (no swap needed)";
    printf("  Phase 4 PASS: %ld → %ld (steady-state, no swap)\n", seg_count_a2, seg_count_a3);

    close(mcp_fd);
    kill(-pid, SIGKILL);
    waitpid(pid, &child_status, 0);
}
