// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// fuzz_helpers.h — общий фреймворк мутационного фаззинга для GCC.
// Без внешних зависимостей. Использует xorshift32 для воспроизводимых мутаций.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <functional>
#include <chrono>
#include <vector>
#include <string>
#include <signal.h>
#include <setjmp.h>

namespace fuzz {

// ── xorshift32 PRNG (детерминированный, воспроизводимый) ──────────────
inline uint32_t xorshift32(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// ── Мутаторы ──────────────────────────────────────────────────────────
enum MutOp : uint8_t { FLIP_BIT=0, FLIP_BYTE=1, INSERT_BYTE=2, DELETE_BYTE=3, DUP_CHUNK=4, OVERWRITE_CHUNK=5, OP_COUNT=6 };

inline void mutate(uint8_t* data, size_t& size, size_t cap, uint32_t& rng) {
    if (size == 0) { data[0] = (uint8_t)xorshift32(rng); size = 1; return; }
    uint8_t op = (uint8_t)(xorshift32(rng) % OP_COUNT);
    size_t pos = (size > 0) ? (xorshift32(rng) % size) : 0;

    switch ((MutOp)op) {
    case FLIP_BIT: {
        uint8_t bit = xorshift32(rng) & 7;
        data[pos] ^= (1u << bit);
        break;
    }
    case FLIP_BYTE:
        data[pos] = (uint8_t)xorshift32(rng);
        break;
    case INSERT_BYTE:
        if (size < cap) {
            memmove(data + pos + 1, data + pos, size - pos);
            data[pos] = (uint8_t)xorshift32(rng);
            size++;
        }
        break;
    case DELETE_BYTE:
        if (size > 1) {
            memmove(data + pos, data + pos + 1, size - pos - 1);
            size--;
        }
        break;
    case DUP_CHUNK: {
        size_t chunk_len = 1 + (xorshift32(rng) % (size > 256 ? 256 : size));
        size_t src = xorshift32(rng) % (size - chunk_len + 1);
        if (pos + chunk_len <= cap) {
            memmove(data + pos + chunk_len, data + pos, size - pos);
            memcpy(data + pos, data + src, chunk_len);
            size += chunk_len;
            if (size > cap) size = cap;
        }
        break;
    }
    case OVERWRITE_CHUNK: {
        size_t chunk_len = 1 + (xorshift32(rng) % (size > 64 ? 64 : size));
        if (pos + chunk_len <= size) {
            for (size_t i = 0; i < chunk_len; i++)
                data[pos + i] = (uint8_t)xorshift32(rng);
        }
        break;
    }
    default: break;
    }
}

// ── Crash handler (SIGSEGV/SIGABRT/SIGFPE) ────────────────────────────
struct CrashContext {
    volatile sig_atomic_t crashed;
    volatile sig_atomic_t signum;
    jmp_buf env;
};
static CrashContext* g_crash_ctx = nullptr;

inline void crash_handler(int sig) {
    if (g_crash_ctx) { g_crash_ctx->crashed = 1; g_crash_ctx->signum = sig; }
    longjmp(g_crash_ctx->env, 1);
}

// ── Сохранение crashing input ─────────────────────────────────────────
inline void save_crash(const char* name, const uint8_t* data, size_t size, int iter) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/fuzz_%s_crash_%d.bin", name, iter);
    FILE* f = fopen(path, "wb");
    if (f) { fwrite(data, 1, size, f); fclose(f); printf("  [CRASH] saved to %s (%zu bytes)\n", path, size); }
}

// ── Основной цикл фаззинга ────────────────────────────────────────────
// target вызывается с (data, size). Если краш — сохраняет crashing input.
// base_input: начальные байты (nullptr = random seed).
inline int fuzz_loop(const char* name,
                     std::function<void(const uint8_t* data, size_t size)> target,
                     const uint8_t* base_input = nullptr, size_t base_size = 0,
                     int max_iter = 10000, int timeout_sec = 30) {
    printf("[fuzz:%s] starting: %d iterations, %ds timeout\n", name, max_iter, timeout_sec);

    std::vector<uint8_t> buf(65536);
    size_t size = 0;
    uint32_t rng = 12345;
    int crashes = 0;

    // Инициализация: base_input или random seed
    if (base_input && base_size > 0) {
        memcpy(buf.data(), base_input, base_size);
        size = base_size;
    } else {
        size = std::min((size_t)(xorshift32(rng) % 256 + 1), buf.size());
        for (size_t i = 0; i < size; i++) buf[i] = (uint8_t)xorshift32(rng);
    }

    CrashContext ctx = {};
    g_crash_ctx = &ctx;
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);

    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < max_iter; i++) {
        // Проверка таймаута каждые 100 итераций
        if (i % 100 == 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= timeout_sec) {
                printf("[fuzz:%s] timeout after %d iters, %d crashes\n", name, i, crashes);
                break;
            }
        }

        // Мутация
        if (i > 0) mutate(buf.data(), size, buf.size(), rng);

        // Вызов цели с защитой от краша
        if (setjmp(ctx.env) == 0) {
            target(buf.data(), size);
        } else {
            crashes++;
            save_crash(name, buf.data(), size, i);
            // Реинициализация после краша
            size = xorshift32(rng) % 256 + 1;
            for (size_t j = 0; j < size; j++) buf[j] = (uint8_t)xorshift32(rng);
        }
    }

    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGFPE, SIG_DFL);
    g_crash_ctx = nullptr;

    printf("[fuzz:%s] done: %d iterations, %d crashes\n", name, max_iter, crashes);
    return crashes;
}

} // namespace fuzz
