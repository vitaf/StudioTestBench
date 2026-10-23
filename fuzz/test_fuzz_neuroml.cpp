// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// F5: Fuzz-тест для ImportNeuroML() — XML парсер.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include "fuzz_helpers.h"
#include "data_model.h"

// forward из neuroml_import.cpp
extern bool ImportNeuroML(const char* filename, NetworkModel& model, std::string& err);

static void fuzz_neuroml(const uint8_t* data, size_t size) {
    char tmpname[] = "/tmp/fuzz_neuroml_XXXXXX";
    int fd = mkstemp(tmpname);
    if (fd < 0) return;
    write(fd, data, size);
    close(fd);

    NetworkModel model;
    std::string err;
    ImportNeuroML(tmpname, model, err);
    unlink(tmpname);
}

int main(int argc, char** argv) {
    int iters = 3000;
    if (argc > 1) iters = atoi(argv[1]);
    int crashes = fuzz::fuzz_loop("neuroml", fuzz_neuroml, nullptr, 0, iters, 30);
    printf("\n=== NeuroML Fuzz: %d crashes ===\n", crashes);
    return crashes > 0 ? 1 : 0;
}
