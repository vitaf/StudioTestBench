// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// fuzz_plugin_desc.cpp — libFuzzer harness for .desc JSON parser
// Reimplements parse_plugin_descriptor logic extracted from plugin_manager.cpp

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>
#include <cstdio>

namespace {

struct FuzzPluginInfo {
    char name[128];
    char version[64];
    char author[128];
    char category[64];
    int modifier_count;
};

static const char* fuzz_find_str(const char* haystack, const char* key, char* out, size_t out_sz) {
    const char* pos = strstr(haystack, key);
    if (!pos) return nullptr;
    pos += strlen(key);
    while (*pos && isspace((unsigned char)*pos)) pos++;
    if (*pos != '"') return nullptr;
    pos++;
    size_t i = 0;
    while (*pos && *pos != '"' && i < out_sz - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return pos;
}

static int fuzz_find_int(const char* haystack, const char* key, int default_val) {
    const char* pos = strstr(haystack, key);
    if (!pos) return default_val;
    pos += strlen(key);
    while (*pos && isspace((unsigned char)*pos)) pos++;
    if (*pos == ':') pos++;
    while (*pos && isspace((unsigned char)*pos)) pos++;
    return (int)strtol(pos, nullptr, 10);
}

static int fuzz_parse_plugin_descriptor(const char* json, FuzzPluginInfo* info) {
    fuzz_find_str(json, "\"name\"", info->name, sizeof(info->name));
    fuzz_find_str(json, "\"version\"", info->version, sizeof(info->version));
    fuzz_find_str(json, "\"author\"", info->author, sizeof(info->author));
    fuzz_find_str(json, "\"category\"", info->category, sizeof(info->category));

    const char* mods_start = strstr(json, "\"modifiers\"");
    if (!mods_start) { info->modifier_count = 0; return 0; }

    const char* bracket = strchr(mods_start, '[');
    if (!bracket) { info->modifier_count = 0; return 0; }

    int count = 0;
    const char* p = bracket + 1;
    while (*p && count < 128) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == ']') break;
        if (*p == '{') {
            const char* close = strchr(p, '}');
            if (!close) break;
            // Parse id from this modifier block
            std::string block(p, close - p + 1);
            (void)fuzz_find_int(block.c_str(), "\"id\"", -1);
            count++;
            p = close + 1;
        } else {
            p++;
        }
    }
    info->modifier_count = count;
    return count;
}

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 256 * 1024) return 0;

    std::string input(reinterpret_cast<const char*>(data), size);

    FuzzPluginInfo info;
    memset(&info, 0, sizeof(info));
    fuzz_parse_plugin_descriptor(input.c_str(), &info);

    // Edge cases
    fuzz_parse_plugin_descriptor("{}", &info);
    fuzz_parse_plugin_descriptor("{\"name\":\"test\",\"modifiers\":[]}", &info);
    fuzz_parse_plugin_descriptor("{\"modifiers\":[{\"id\":1},{\"id\":2}]}", &info);

    return 0;
}
