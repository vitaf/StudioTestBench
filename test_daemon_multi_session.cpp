// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.
// test_daemon_multi_session.cpp — тесты сессий демона (создание, удаление, валидация)

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <cstdint>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "../BioNeuronCore/src/daemon_protocol.h"
using namespace biocore_proto;

// Подключение к демону (Unix или TCP)
static int connectToDaemon(const char* unix_path = "/tmp/biocore.sock",
                            const char* tcp_host = "127.0.0.1", uint16_t tcp_port = 6789) {
    // Сначала пробуем Unix
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, unix_path, sizeof(addr.sun_path) - 1);
        if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) return fd;
        close(fd);
    }
    // Затем TCP
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(tcp_port);
        inet_pton(AF_INET, tcp_host, &addr.sin_addr);
        if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) return fd;
        close(fd);
    }
    return -1;
}

static bool sendMsg(int fd, uint32_t type, const uint8_t* payload, uint32_t len) {
    uint8_t hdr[8];
    hdr[0] = type & 0xFF; hdr[1] = (type >> 8) & 0xFF;
    hdr[2] = (type >> 16) & 0xFF; hdr[3] = (type >> 24) & 0xFF;
    hdr[4] = len & 0xFF; hdr[5] = (len >> 8) & 0xFF;
    hdr[6] = (len >> 16) & 0xFF; hdr[7] = (len >> 24) & 0xFF;
    if (write(fd, hdr, 8) != 8) return false;
    if (len > 0 && write(fd, payload, len) != (ssize_t)len) return false;
    return true;
}

static bool recvMsg(int fd, uint32_t& out_type, std::vector<uint8_t>& out_payload, int timeout_ms = 3000) {
    fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) return false;

    uint8_t hdr[8];
    ssize_t n = read(fd, hdr, 8);
    if (n != 8) return false;
    out_type = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
               ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    uint32_t plen = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                    ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    if (plen > 10 * 1024 * 1024) return false;  // защита от гигантских фреймов
    out_payload.resize(plen);
    if (plen > 0) {
        size_t off = 0;
        while (off < plen) {
            n = read(fd, out_payload.data() + off, plen - off);
            if (n <= 0) return false;
            off += (size_t)n;
        }
    }
    return true;
}

// Пропустить STATE_FRAME пока не получим нужный тип ответа
static bool recvResponse(int fd, uint32_t expected_type, std::vector<uint8_t>& out, int timeout = 3000) {
    for (int i = 0; i < 20; i++) {
        uint32_t t;
        if (!recvMsg(fd, t, out, timeout)) return false;
        if (t == expected_type) return true;
        if (t == MSG_ERROR) return false;
        if (t == MSG_PONG) continue;
        // Push-фреймы пропускаем
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════
// Требуется запущенный демон (biocore-daemon).
// Если демон не запущен — тесты SKIP.
// ═══════════════════════════════════════════════════════════════

static bool daemonAvailable() {
    int fd = connectToDaemon();
    if (fd < 0) return false;
    // Проверить связь
    std::vector<uint8_t> resp;
    bool ok = sendMsg(fd, MSG_PING, nullptr, 0) && recvResponse(fd, MSG_PONG, resp, 1000);
    close(fd);
    return ok;
}

TEST_CASE("Daemon session validation — invalid GPU index", "[session][validation]") {
    if (!daemonAvailable()) { WARN("Daemon not running"); return; }
    int fd = connectToDaemon();
    REQUIRE(fd >= 0);

    // Попытка создать сессию с несуществующим GPU
    int32_t bad_gpu = 999;
    std::vector<uint8_t> resp;
    bool sent = sendMsg(fd, MSG_CREATE_SESSION, (uint8_t*)&bad_gpu, 4);
    REQUIRE(sent);
    uint32_t resp_type;
    REQUIRE(recvMsg(fd, resp_type, resp));
    // Либо MSG_ERROR, либо MSG_CREATE_SESSION_RESULT с ok=0
    bool valid = (resp_type == MSG_ERROR) || (resp_type == MSG_CREATE_SESSION_RESULT && resp.size() >= 1 && resp[0] == 0);
    REQUIRE(valid);
    close(fd);
}

TEST_CASE("Daemon session validation — destroy default session rejected", "[session][validation]") {
    if (!daemonAvailable()) { WARN("Daemon not running"); return; }
    int fd = connectToDaemon();
    REQUIRE(fd >= 0);

    // Попытка удалить сессию 0
    uint32_t sid = 0;
    std::vector<uint8_t> resp;
    REQUIRE(sendMsg(fd, MSG_DESTROY_SESSION, (uint8_t*)&sid, 4));
    uint32_t resp_type;
    REQUIRE(recvMsg(fd, resp_type, resp));
    bool valid = (resp_type == MSG_ERROR) || (resp_type == MSG_DESTROY_SESSION_RESULT && resp.size() >= 1 && resp[0] == 0);
    REQUIRE(valid);
    close(fd);
}

TEST_CASE("Daemon session validation — destroy nonexistent session", "[session][validation]") {
    if (!daemonAvailable()) { WARN("Daemon not running"); return; }
    int fd = connectToDaemon();
    REQUIRE(fd >= 0);

    // Попытка удалить несуществующую сессию
    uint32_t sid = 99999;
    std::vector<uint8_t> resp;
    REQUIRE(sendMsg(fd, MSG_DESTROY_SESSION, (uint8_t*)&sid, 4));
    uint32_t resp_type;
    REQUIRE(recvMsg(fd, resp_type, resp));
    bool valid = (resp_type == MSG_ERROR) || (resp_type == MSG_DESTROY_SESSION_RESULT && resp.size() >= 1 && resp[0] == 0);
    REQUIRE(valid);
    close(fd);
}

TEST_CASE("Daemon MSG_FLAG_SESSION — session id extraction", "[session][protocol]") {
    // Проверяем что константа определена и работает
    REQUIRE(MSG_FLAG_SESSION == 0x80000000u);
    uint32_t type_with_session = MSG_LOAD_BION | MSG_FLAG_SESSION;
    REQUIRE(msg_has_session(type_with_session));
    REQUIRE(msg_base_type(type_with_session) == MSG_LOAD_BION);

    uint32_t type_without = MSG_LOAD_BION;
    REQUIRE_FALSE(msg_has_session(type_without));
    REQUIRE(msg_base_type(type_without) == MSG_LOAD_BION);
}

TEST_CASE("Daemon MSG_LIST_DEVICES — response format", "[session][gpu]") {
    if (!daemonAvailable()) { WARN("Daemon not running"); return; }
    int fd = connectToDaemon();
    REQUIRE(fd >= 0);

    std::vector<uint8_t> resp;
    REQUIRE(sendMsg(fd, MSG_LIST_DEVICES, nullptr, 0));
    uint32_t resp_type;
    REQUIRE(recvMsg(fd, resp_type, resp));
    REQUIRE(resp_type == MSG_LIST_DEVICES_RESULT);
    REQUIRE(resp.size() >= 4);
    close(fd);
}

TEST_CASE("Daemon session validation — RUN_TASK empty bion path", "[session][validation]") {
    if (!daemonAvailable()) { WARN("Daemon not running"); return; }
    int fd = connectToDaemon();
    REQUIRE(fd >= 0);

    // payload: [bion_len:u16=0][ticks:u32=1000]
    uint8_t payload[6] = {0, 0, 0xE8, 0x03, 0, 0};  // len=0, ticks=1000
    std::vector<uint8_t> resp;
    REQUIRE(sendMsg(fd, MSG_RUN_TASK, payload, 6));
    uint32_t resp_type;
    REQUIRE(recvMsg(fd, resp_type, resp));
    bool valid = (resp_type == MSG_ERROR) || (resp_type == MSG_RUN_TASK_RESULT && resp.size() >= 1 && resp[0] == 0);
    REQUIRE(valid);
    close(fd);
}

TEST_CASE("Daemon MSG_LIST_SESSIONS — initially has session 0", "[session]") {
    if (!daemonAvailable()) { WARN("Daemon not running"); return; }
    int fd = connectToDaemon();
    REQUIRE(fd >= 0);

    std::vector<uint8_t> resp;
    REQUIRE(sendMsg(fd, MSG_LIST_SESSIONS, nullptr, 0));
    uint32_t resp_type;
    REQUIRE(recvMsg(fd, resp_type, resp));
    REQUIRE(resp_type == MSG_LIST_SESSIONS_RESULT);
    REQUIRE(resp.size() >= 4);
    // Должен быть хотя бы сессия 0
    uint32_t count = (uint32_t)resp[0] | ((uint32_t)resp[1] << 8) |
                     ((uint32_t)resp[2] << 16) | ((uint32_t)resp[3] << 24);
    REQUIRE(count >= 1);
    close(fd);
}
