// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// F1: Fuzz-тест для mcp::Json::parse() — in-memory JSON парсер.
#include "mcp_json.h"
#include "fuzz_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static std::string g_deep_nest(size_t depth) {
    std::string s(depth, '[');
    s += "0";
    s += std::string(depth, ']');
    return s;
}

int main(int argc, char** argv) {
    int iters = 5000;
    if (argc > 1) iters = atoi(argv[1]);

    printf("=== Fuzz MCP JSON Parser ===\n");
    int crashes = 0;

    // Фаза 1: случайные строки
    crashes += fuzz::fuzz_loop("mcp_json_random", [](const uint8_t* data, size_t size) {
        std::string s((const char*)data, size);
        mcp::Json::parse(s.c_str());
    }, nullptr, 0, iters, 15);

    // Фаза 2: глубоко вложенные массивы (тест на stack overflow)
    crashes += fuzz::fuzz_loop("mcp_json_nested", [](const uint8_t* data, size_t size) {
        size_t depth = 1 + (size > 0 ? (data[0] % 128) : 0);
        std::string s = g_deep_nest(depth);
        mcp::Json::parse(s.c_str());
    }, nullptr, 0, std::min(iters, 256), 10);

    // Фаза 3: strings с escape-последовательностями
    crashes += fuzz::fuzz_loop("mcp_json_escape", [](const uint8_t* data, size_t size) {
        std::string s = "\"";
        for (size_t i = 0; i < size && i < 4096; i++) {
            if (data[i] == '\\' || data[i] == '"' || data[i] < 32) {
                s += '\\';
                s += (char)((data[i] % 4) + '0');
            }
            s += (char)((data[i] % 95) + 32);
        }
        s += "\"";
        mcp::Json::parse(s.c_str());
    }, nullptr, 0, std::min(iters, 2000), 10);

    printf("\n=== MCP JSON Fuzz: %d total crashes ===\n", crashes);
    return crashes > 0 ? 1 : 0;
}
