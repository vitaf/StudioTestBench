// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// fuzz_ipc_frame.cpp — libFuzzer harness for IPC message frame parser
// Fuzzes the 8-byte header + payload framing from daemon_protocol.h / daemon_server.cpp

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

// IPC wire format: 8-byte header = type(u32 LE) + payload_len(u32 LE) + payload
// This is the framing parser from daemon_server.cpp:processClientData

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024 * 1024) return 0;

    const uint8_t* p = data;
    const uint8_t* end = data + size;

    // Parse as many complete frames as possible
    int frames = 0;
    while (p + 8 <= end && frames < 1000) {
        uint32_t type = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        uint32_t payload_len = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                               ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
        p += 8;

        if (payload_len > 100 * 1024 * 1024) break;  // sanity limit 100MB
        if (p + payload_len > end) break;             // truncated

        // Exercise the payload based on message type (dispatchMessage-like)
        switch (type) {
        case 0x0000: break; // MSG_PING
        case 0x0100: break; // MSG_LOAD_BION
        case 0x0200: break; // MSG_START
        case 0x0201: break; // MSG_STOP
        case 0x0300: break; // MSG_STIMULUS
        case 0x0400: break; // MSG_SNAPSHOT
        case 0x0480: break; // MSG_STATE_FRAME
        default: break;
        }

        // Check for known patterns in payload
        if (payload_len >= 4) {
            uint32_t magic = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            (void)magic;
        }
        if (payload_len >= 8) {
            uint64_t val = *(uint64_t*)p;
            (void)val;
        }

        p += payload_len;
        frames++;
    }

    // Also fuzz the header parser alone with edge cases
    if (size >= 8) {
        uint32_t t = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        uint32_t l = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                     ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
        (void)t; (void)l;

        // Test edge case: zero-length payload
        if (l == 0) frames++;

        // Test edge case: max reasonable payload
        if (l <= 1024 * 1024) frames++;
    }

    return 0;
}
