// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_orch_agent_e2e.cpp — E2E тест: orchestrator + agent полный жизненный цикл.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdlib.h>
#include <unistd.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <string>

static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) FAIL(msg); } while(0)

#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
static const char* ORCH_BIN  = MINDCopy_ROOT "/BioNeuronOrchestrator/build/orchestrator-server";
static const char* AGENT_BIN = MINDCopy_ROOT "/BioNeuronOrchestrator/build/biocore-agent";
static const int    TEST_PORT = 18090;
static pid_t g_orch_pid = 0;

// ── HTTP helpers ────────────────────────────────────────────────────────
static int connectHttp(const char* host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    return fd;
}

static std::string httpPost(const char* host, int port, const char* path,
                             const char* body, const char* auth = nullptr) {
    int fd = connectHttp(host, port);
    if (fd < 0) return "";
    char req[8192];
    int len;
    if (auth) {
        len = snprintf(req, sizeof(req),
            "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
            "Authorization: Bearer %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            path, host, port, auth, strlen(body), body);
    } else {
        len = snprintf(req, sizeof(req),
            "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
            path, host, port, strlen(body), body);
    }
    write(fd, req, len);
    char resp[8192];
    int n = read(fd, resp, sizeof(resp) - 1);
    close(fd);
    if (n <= 0) return "";
    resp[n] = '\0';
    // Найти тело ответа (после \r\n\r\n)
    const char* body_start = strstr(resp, "\r\n\r\n");
    return body_start ? std::string(body_start + 4) : "";
}

static std::string httpGet(const char* host, int port, const char* path,
                            const char* auth = nullptr) {
    int fd = connectHttp(host, port);
    if (fd < 0) return "";
    char req[4096];
    if (auth) {
        snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\nHost: %s:%d\r\nAuthorization: Bearer %s\r\n"
            "Connection: close\r\n\r\n",
            path, host, port, auth);
    } else {
        snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
            path, host, port);
    }
    write(fd, req, strlen(req));
    char resp[8192];
    int n = read(fd, resp, sizeof(resp) - 1);
    close(fd);
    if (n <= 0) return "";
    resp[n] = '\0';
    const char* body_start = strstr(resp, "\r\n\r\n");
    return body_start ? std::string(body_start + 4) : "";
}

// ── Orchestrator process management ─────────────────────────────────────
static bool startOrchestrator() {
    // Создать тестовый конфиг с нужным портом
    char cfg_path[] = "/tmp/orch_e2e_cfg_XXXXXX";
    int cfg_fd = mkstemp(cfg_path);
    if (cfg_fd < 0) return false;
    char cfg_json[512];
    snprintf(cfg_json, sizeof(cfg_json),
        "{\"bind_port\":%d,\"db_host\":\"127.0.0.1\",\"db_port\":3306,"
        "\"db_name\":\"mindorc\",\"db_user\":\"mcpbaseusrdev\",\"db_password\":\"dX5sD1tV7k\","
        "\"table_prefix\":\"orch_test_\","
        "\"max_queue_size\":100,\"default_timeout\":3600,"
        "\"max_retries\":3,\"heartbeat_timeout\":90,"
        "\"log_max_size_mb\":1,\"log_max_files\":2}",
        TEST_PORT);
    write(cfg_fd, cfg_json, strlen(cfg_json));
    close(cfg_fd);

    pid_t pid = fork();
    if (pid < 0) { unlink(cfg_path); return false; }
    if (pid == 0) {
        execl(ORCH_BIN, ORCH_BIN, cfg_path, nullptr);
        _exit(1);
    }
    g_orch_pid = pid;
    // Ждём пока оркестратор запустится (до 5 секунд)
    for (int i = 0; i < 50; i++) {
        usleep(100000);
        int fd = connectHttp("127.0.0.1", TEST_PORT);
        if (fd >= 0) { close(fd); unlink(cfg_path); return true; }
    }
    unlink(cfg_path);
    return false;
}

static void stopOrchestrator() {
    if (g_orch_pid > 0) {
        kill(g_orch_pid, SIGTERM);
        waitpid(g_orch_pid, nullptr, 0);
        g_orch_pid = 0;
    }
}

// ── JSON helpers ────────────────────────────────────────────────────────
static std::string extractField(const std::string& json, const std::string& field) {
    std::string search = "\"" + field + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos + search.size());
    if (pos == std::string::npos) return "";
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos) return "";
    if (json[pos] == '"') {
        auto end = json.find("\"", pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }
    auto end = json.find_first_of(", \t\n\r}]", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

// ═══════════════════════════════════════════════════════════════════════

int e2e_orchestrator_starts() {
    TEST("E2E: orchestrator starts and responds to GET /");
    if (!startOrchestrator()) FAIL("orchestrator did not start");
    auto resp = httpGet("127.0.0.1", TEST_PORT, "/");
    CHECK(!resp.empty(), "no response from orchestrator");
    CHECK(resp.find("MindCopy") != std::string::npos || resp.find("orchestrator") != std::string::npos,
          "dashboard not found in response");
    stopOrchestrator();
    PASS();
    return 0;
}

int e2e_node_register() {
    TEST("E2E: node registration via API");
    if (!startOrchestrator()) FAIL("orchestrator did not start");

    auto resp = httpPost("127.0.0.1", TEST_PORT, "/api/v1/node/register",
        "{\"node_key\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"hostname\":\"e2e-test-node\",\"node_type\":1,\"gpu_count\":1}");
    CHECK(!resp.empty(), "no response");
    CHECK(extractField(resp, "ok") == "true", "registration failed");

    auto node_id_str = extractField(resp, "node_id");
    CHECK(!node_id_str.empty(), "no node_id returned");
    printf(" node_id=%s", node_id_str.c_str());

    stopOrchestrator();
    PASS();
    return 0;
}

int e2e_task_lifecycle() {
    TEST("E2E: full task lifecycle (register→claim→heartbeat)");
    if (!startOrchestrator()) FAIL("orchestrator did not start");

    const char* key = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    std::string auth = std::string("Bearer ") + key;

    // 1. Register
    auto reg = httpPost("127.0.0.1", TEST_PORT, "/api/v1/node/register",
        (std::string("{\"node_key\":\"") + key +
         "\",\"hostname\":\"e2e-node\",\"node_type\":1,\"gpu_count\":1}").c_str());
    CHECK(extractField(reg, "ok") == "true", "registration failed");

    // 2. Heartbeat
    auto hb = httpPost("127.0.0.1", TEST_PORT, "/api/v1/task/heartbeat",
        "{\"cpu_load\":50,\"gpu_load\":30,\"mem_used_bytes\":1048576}", key);
    CHECK(extractField(hb, "ok") == "true", "heartbeat failed");

    // 3. Claim (пустая очередь — ожидаем null task_id)
    auto claim = httpPost("127.0.0.1", TEST_PORT, "/api/v1/task/claim", "{}", key);
    CHECK(!claim.empty(), "no claim response");
    printf(" claim=%s", claim.c_str());

    // 4. Dashboard
    auto dash = httpGet("127.0.0.1", TEST_PORT, "/");
    CHECK(dash.find("e2e-node") != std::string::npos, "node not visible in dashboard");

    stopOrchestrator();
    PASS();
    return 0;
}

int e2e_auth_required() {
    TEST("E2E: protected endpoints require auth");
    if (!startOrchestrator()) FAIL("orchestrator did not start");

    auto resp = httpPost("127.0.0.1", TEST_PORT, "/api/v1/task/claim", "{}");
    CHECK(!resp.empty(), "no response");
    CHECK(extractField(resp, "ok") == "false", "unauthenticated request should fail");

    stopOrchestrator();
    PASS();
    return 0;
}

int e2e_multiple_requests() {
    TEST("E2E: orchestrator survives 100 rapid requests");
    if (!startOrchestrator()) FAIL("orchestrator did not start");

    const char* key = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    for (int i = 0; i < 100; i++) {
        char body[512];
        snprintf(body, sizeof(body),
            "{\"node_key\":\"%s\",\"hostname\":\"node%d\",\"node_type\":1,\"gpu_count\":1}",
            key, i);
        auto resp = httpPost("127.0.0.1", TEST_PORT, "/api/v1/node/register", body);
        if (resp.empty()) FAIL("orchestrator crashed under load");
    }
    printf(" 100 requests OK");

    stopOrchestrator();
    PASS();
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════

int main() {
    printf("=== Orchestrator E2E Tests ===\n\n");

    // Проверить наличие бинарников
    if (access(ORCH_BIN, X_OK) != 0) {
        printf("SKIP: orchestrator binary not found at %s\n", ORCH_BIN);
        return 77;
    }

    // Проверить, что порт не занят
    int test_fd = connectHttp("127.0.0.1", TEST_PORT);
    if (test_fd >= 0) {
        close(test_fd);
        printf("SKIP: port %d is already in use\n", TEST_PORT);
        return 77;
    }

    // Проверить MySQL доступность (оркестратор требует БД)
    int mysql_fd = connectHttp("127.0.0.1", 3306);
    if (mysql_fd < 0) {
        printf("SKIP: MySQL not available on port 3306 (required for orchestrator E2E)\n");
        return 77;
    }
    close(mysql_fd);

    (void)mkdir("/var/lib/biocore/orchestrator/", 0755);

    int failed = 0;
    failed += e2e_orchestrator_starts();
    failed += e2e_node_register();
    failed += e2e_task_lifecycle();
    failed += e2e_auth_required();
    failed += e2e_multiple_requests();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (failed > 0) { printf("FAILED: %d test(s)\n", failed); return 1; }
    printf("ALL E2E TESTS PASSED\n");
    return 0;
}
