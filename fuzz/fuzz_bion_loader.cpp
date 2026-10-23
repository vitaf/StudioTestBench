// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// fuzz_bion_loader.cpp — libFuzzer harness for .bion binary parser (bion_load_host)
// Uses fmemopen to feed fuzz data directly to the fopen-based parser.

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "bion_loader.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 68 || size > 100 * 1024 * 1024) return 0;  // header is 68 bytes

    // Use fmemopen to create a FILE* from the fuzz buffer
    // NOTE: fmemopen requires glibc. Available on Linux.
    FILE* f = fmemopen((void*)data, size, "rb");
    if (!f) {
        // Fallback: write to temp file
        char tmpname[] = "/tmp/fuzz_bion_XXXXXX";
        int fd = mkstemp(tmpname);
        if (fd < 0) return 0;
        ssize_t written = write(fd, data, size);
        close(fd);
        if (written != (ssize_t)size) { unlink(tmpname); return 0; }

        BionData bd;
        int rc = bion_load_host(tmpname, &bd);
        if (rc == 0) bion_unload_host(&bd);
        unlink(tmpname);
        return 0;
    }

    // fmemopen succeeded — but bion_load_host takes a filename, not FILE*
    // We need to modify the approach: test via temp file
    fclose(f);

    // Write fuzz data to temp file and parse
    char tmpname[] = "/tmp/fuzz_bion_XXXXXX";
    int fd = mkstemp(tmpname);
    if (fd < 0) return 0;
    ssize_t written = write(fd, data, size);
    close(fd);
    if (written != (ssize_t)size) { unlink(tmpname); return 0; }

    BionData bd;
    int rc = bion_load_host(tmpname, &bd);

    // On success, exercise the loaded data
    if (rc == 0) {
        // Verify neuron/segment count consistency
        if (bd.header.num_segments > 0 && bd.header.num_segments < 1000000) {
            // Access segment data to check for OOB
            for (uint32_t i = 0; i < bd.header.num_segments && i < 1000; i++) {
                volatile float vm = bd.segments[i].V_m;
                (void)vm;
            }
        }
        if (bd.header.num_synapses > 0 && bd.header.num_synapses < 1000000) {
            for (uint32_t i = 0; i < bd.header.num_synapses && i < 1000; i++) {
                volatile float w = bd.synapses[i].weight;
                (void)w;
            }
        }
        bion_unload_host(&bd);
    }

    unlink(tmpname);
    return 0;
}
