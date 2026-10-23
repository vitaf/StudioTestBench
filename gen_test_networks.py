#!/usr/bin/env python3
"""Генератор минимальных .bion v9 файлов с разным числом сегментов для тестов.

Структуры из BioNeuronCore/include/types.h (BION v9, #pragma pack(1)):
  NetworkHeader: 84 байта
  Neuron:        24 байта
  Segment:       1692 байта (MAX_MODIFIERS=32, MAX_INTRACONNECTIONS=16)
  IntraConnection: 16 байт
  Nucleus:       412 байт (GENE_COUNT=46)
"""
import struct, os

BION_MAGIC = 0x42494F4E
BION_VERSION = 9

SZ_HDR = 84
SZ_NEURON = 24
SZ_SEGMENT = 1692
SZ_INTRACONN = 16
SZ_NUCLEUS = 412

def create_minimal_segment(seg_id, neuron_id=0):
    """Создаёт минимально валидный Segment (1692 байта)."""
    buf = bytearray(SZ_SEGMENT)
    off = 0
    struct.pack_into('<I', buf, off, seg_id); off += 4      # id
    struct.pack_into('<I', buf, off, neuron_id); off += 4    # neuron_id
    struct.pack_into('<fff', buf, off, 0.0, 0.0, 0.0); off += 12  # position (Vec3)
    struct.pack_into('<fff', buf, off, 0.0, 1.0, 0.0); off += 12  # direction (Vec3)
    struct.pack_into('<f', buf, off, 50.0); off += 4        # length
    struct.pack_into('<f', buf, off, 10.0); off += 4        # radius
    struct.pack_into('<f', buf, off, -65.0); off += 4       # V_m
    off += 8*4   # intra_ions[8] (ION_MAX)
    off += 8*4   # extra_ions[8] (ION_MAX)
    off += 14*4  # intra_nt[14]
    off += 14*4  # neurotransmitter[14]
    off += 32*44 # modifiers[32] (Modifier = 44 bytes)
    off += 1     # modifier_count
    off += 1     # segment_type (DENDRITE=0)
    off += 2     # reserved1
    off += 4     # ATP_level
    off += 4     # temperature
    off += 4     # I_buffer
    off += 4     # transcription_signal
    off += 16*4  # intra_connection_ids[16]
    off += 1     # intra_connection_count
    off += 3     # reserved2
    assert off == SZ_SEGMENT, f"Segment size mismatch: {off} != {SZ_SEGMENT}"
    return bytes(buf)

def create_minimal_neuron(neuron_id=0, seg_start=0, seg_count=1, soma_index=0):
    """Создаёт минимально валидный Neuron (24 байта)."""
    buf = bytearray(SZ_NEURON)
    struct.pack_into('<I I I I f B 3s', buf, 0,
                     neuron_id, seg_start, seg_count, soma_index,
                     -50.0, 0, b'\0\0\0')
    return bytes(buf)

def write_bion(path, num_segments):
    """Создаёт .bion v9 файл с num_segments сегментов."""
    num_neurons = 1
    num_intra = 0  # без связей — сегменты изолированы, но грузятся без ошибок
    num_synapses = 0
    num_nuclei = 0  # ядра опциональны (v5+)

    with open(path, 'wb') as f:
        hdr = struct.pack('<I I B B H I I I I I I I f Q 16s I I I I',
            BION_MAGIC,           # magic
            BION_VERSION,         # version
            0,                    # mode (BIOLOGICAL)
            0,                    # diffusion_type
            0,                    # reserved
            num_neurons,          # num_neurons
            num_segments,         # num_segments
            num_intra,            # num_intra_connections
            num_synapses,         # num_synapses
            1, 1, 1,             # grid_dim
            1.0,                  # voxel_size
            0,                    # timestamp
            b'BioNeuronStudio\0\0\0\0\0\0\0\0\0',  # build_id (16 bytes)
            num_nuclei,           # num_nuclei
            1,                    # nucleus_version
            0, 0                  # _reserved_hdr[2]
        )
        assert len(hdr) == SZ_HDR, f"Header: {len(hdr)} != {SZ_HDR}"
        f.write(hdr)

        for n in range(num_neurons):
            f.write(create_minimal_neuron(n, 0, num_segments, 0))

        for s in range(num_segments):
            f.write(create_minimal_segment(s + 1, 0))

    total_bytes = SZ_HDR + num_neurons*SZ_NEURON + num_segments*SZ_SEGMENT
    print(f"  {path}: {num_segments} segs, {total_bytes/1024:.1f} KB")

# Сети с ГАРАНТИРОВАННО разным количеством сегментов
os.makedirs("/tmp", exist_ok=True)
write_bion("/tmp/test_net_a.bion", 3)
write_bion("/tmp/test_net_b.bion", 7)
print("Done.")
