// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// F4: Fuzz-тест для bion_load_host() — бинарный .bion парсер.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "fuzz_helpers.h"
#include "bion_loader.h"

static void fuzz_bion(const uint8_t* data, size_t size) {
    char tmpname[] = "/tmp/fuzz_bion_XXXXXX";
    int fd = mkstemp(tmpname);
    if (fd < 0) return;
    write(fd, data, size);
    close(fd);

    BionData bd = {};
    int ret = bion_load_host(tmpname, &bd);
    if (ret == 0) bion_unload_host(&bd);
    unlink(tmpname);
}

int main(int argc, char** argv) {
    int iters = 5000;
    if (argc > 1) iters = atoi(argv[1]);
    int crashes = fuzz::fuzz_loop("bion", fuzz_bion, nullptr, 0, iters, 30);
    printf("\n=== BION Fuzz: %d crashes ===\n", crashes);
    return crashes > 0 ? 1 : 0;
}
