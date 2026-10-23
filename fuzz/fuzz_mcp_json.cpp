// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// fuzz_mcp_json.cpp — libFuzzer harness for MCP JSON-RPC parser (mcp::Json::parse)

#include <cstdint>
#include <cstring>
#include <string>
#include "mcp_json.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1024 * 1024) return 0;

    std::string input(reinterpret_cast<const char*>(data), size);
    auto result = mcp::Json::parse(input.c_str());

    // Round-trip
    std::string serialised = result.to_string();
    if (!serialised.empty()) {
        auto result2 = mcp::Json::parse(serialised.c_str());
        (void)result2;
    }

    // Exercise accessors
    if (result.is_object()) {
        (void)result.find("nonexistent_key");
        (void)result.find("test");
        (void)result.get_string("test", "");
        (void)result.get_number("test", 0.0);
        (void)result.get_bool("test", false);
        (void)result.obj_size();
    }
    if (result.is_array()) {
        (void)result.arr_size();
        if (result.arr_size() > 0) {
            (void)result[0];
            if (result.arr_size() > 1) (void)result[result.arr_size() - 1];
        }
    }
    (void)result.is_null();
    (void)result.is_number();
    (void)result.is_bool();
    (void)result.is_string();
    (void)result.to_string();
    (void)result.as_int();
    (void)result.as_number();
    (void)result.as_bool();
    (void)result.as_string();

    return 0;
}
