// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// fuzz_swc_parser.cpp — libFuzzer harness for SWC ASCII parser

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include "swc_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 10 || size > 10 * 1024 * 1024) return 0;

    char tmpname[] = "/tmp/fuzz_swc_XXXXXX";
    int fd = mkstemp(tmpname);
    if (fd < 0) return 0;
    ssize_t written = write(fd, data, size);
    ::close(fd);
    if (written != (ssize_t)size) { ::unlink(tmpname); return 0; }

    SwcMorphology m = swc_parse(tmpname);
    (void)m;

    ::unlink(tmpname);
    return 0;
}
