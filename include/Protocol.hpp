#pragma once

#include <cstdint>

struct FileHeader {
    char filename[256];
    uint64_t file_size;
    uint64_t chunk_size;
    uint64_t total_chunks;
    char file_sha256[65];
};

struct ChunkHeader {
    uint64_t chunk_index;
    uint64_t offset;
    uint32_t data_size;
    char chunk_sha256[65];
};
