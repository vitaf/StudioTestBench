// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// F3: Fuzz-тест для swc_parse() — ASCII SWC парсер.
#include "swc_parser.h"
#include "fuzz_helpers.h"
#include <cstdio>
#include <cstdlib>

static void fuzz_swc(const uint8_t* data, size_t size) {
    char tmpname[] = "/tmp/fuzz_swc_XXXXXX";
    int fd = mkstemp(tmpname);
    if (fd < 0) return;
    write(fd, data, size);
    close(fd);
    auto result = swc_parse(tmpname);
    (void)result; // проверяем только что не краш
    unlink(tmpname);
}

int main(int argc, char** argv) {
    int iters = 5000;
    if (argc > 1) iters = atoi(argv[1]);
    int crashes = fuzz::fuzz_loop("swc", fuzz_swc, nullptr, 0, iters, 30);
    printf("\n=== SWC Fuzz: %d crashes ===\n", crashes);
    return crashes > 0 ? 1 : 0;
}
