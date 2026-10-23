// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// fuzz_neuroml_xml.cpp — libFuzzer harness for NeuroML XML import

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <unistd.h>
#include "neuroml_import.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 10 || size > 256 * 1024) return 0;

    char tmpname[] = "/tmp/fuzz_neuroml_XXXXXX";
    int fd = mkstemp(tmpname);
    if (fd < 0) return 0;
    FILE* f = fdopen(fd, "wb");
    if (!f) { ::close(fd); return 0; }
    fwrite(data, 1, size, f);
    fclose(f);

    NetworkModel model;
    std::string err;
    (void)ImportNeuroML(tmpname, model, err);

    ::unlink(tmpname);
    return 0;
}
