#pragma once

#include <cstdint>
#include <string>

struct FileHeader {
    char filename[256];        // file name only, not full path
    uint64_t file_size;        // total file size
    uint64_t chunk_size;       // usually 65536
    uint64_t total_chunks;     // number of chunks
    char file_sha256[65];      // 64 hex chars + '\0'
};

struct ChunkHeader {
    uint64_t chunk_index;
    uint64_t offset;
    uint32_t data_size;
    char chunk_sha256[65];     // 64 hex chars + '\0'
};
