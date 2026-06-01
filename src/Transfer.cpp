#include "../include/Transfer.hpp"
#include "../include/Config.hpp"
#include "../include/Protocol.hpp"
#include "../include/Network.hpp"
#include "../include/SHA256Util.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <unistd.h>

namespace fs = std::filesystem;

static uint64_t get_file_size(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return static_cast<uint64_t>(-1);
    return static_cast<uint64_t>(file.tellg());
}

static std::string basename_of(const std::string& filepath) {
    return fs::path(filepath).filename().string();
}

static uint64_t calc_total_chunks(uint64_t file_size, uint64_t chunk_size) {
    if (file_size == 0) return 0;
    return (file_size + chunk_size - 1) / chunk_size;
}

void sendFileWithChunkShaAndResume(int sock, const std::string& filepath) {
    uint64_t file_size = get_file_size(filepath);
    if (file_size == static_cast<uint64_t>(-1)) {
        std::cerr << "[-] Cannot open source file: " << filepath << "\n";
        return;
    }

    std::string filename = basename_of(filepath);
    std::string file_hash = sha256_file(filepath);
    uint64_t chunk_size = BUFFER_SIZE;
    uint64_t total_chunks = calc_total_chunks(file_size, chunk_size);

    FileHeader header{};
    std::strncpy(header.filename, filename.c_str(), sizeof(header.filename) - 1);
    header.file_size = file_size;
    header.chunk_size = chunk_size;
    header.total_chunks = total_chunks;
    std::strncpy(header.file_sha256, file_hash.c_str(), sizeof(header.file_sha256) - 1);

    std::cout << "[Client] File: " << filename << "\n";
    std::cout << "[Client] Size: " << file_size << " bytes\n";
    std::cout << "[Client] SHA256 file: " << file_hash << "\n";
    std::cout << "[Client] Total chunks: " << total_chunks << "\n";

    if (!send_all(sock, &header, sizeof(header))) {
        std::cerr << "[-] Failed to send file header\n";
        return;
    }

    uint64_t resume_offset = 0;
    if (!recv_all(sock, &resume_offset, sizeof(resume_offset))) {
        std::cerr << "[-] Failed to receive resume offset\n";
        return;
    }

    if (resume_offset > file_size) {
        std::cerr << "[-] Invalid resume offset from server\n";
        return;
    }

    if (resume_offset == file_size) {
        std::cout << "[Client] Server already has full file. Waiting final result...\n";
    } else {
        std::cout << "[Client] Server already has: " << resume_offset << " bytes\n";
        std::cout << "[Client] Resume from offset: " << resume_offset << "\n";
    }

    std::ifstream infile(filepath, std::ios::binary);
    if (!infile.is_open()) {
        std::cerr << "[-] Cannot open source file\n";
        return;
    }

    infile.seekg(resume_offset, std::ios::beg);
    uint64_t current_offset = resume_offset;
    std::vector<char> buffer(BUFFER_SIZE);

    while (current_offset < file_size) {
        uint64_t remain = file_size - current_offset;
        uint32_t bytes_to_read = static_cast<uint32_t>(std::min<uint64_t>(BUFFER_SIZE, remain));

        infile.read(buffer.data(), bytes_to_read);
        uint32_t actual_read = static_cast<uint32_t>(infile.gcount());
        if (actual_read == 0) break;

        std::string chunk_hash = sha256_buffer(buffer.data(), actual_read);

        ChunkHeader chunk_header{};
        chunk_header.chunk_index = current_offset / chunk_size;
        chunk_header.offset = current_offset;
        chunk_header.data_size = actual_read;
        std::strncpy(chunk_header.chunk_sha256, chunk_hash.c_str(), sizeof(chunk_header.chunk_sha256) - 1);

        if (!send_all(sock, &chunk_header, sizeof(chunk_header))) {
            std::cerr << "\n[-] Connection lost while sending chunk header\n";
            return;
        }

        if (!send_all(sock, buffer.data(), actual_read)) {
            std::cerr << "\n[-] Connection lost while sending chunk data\n";
            return;
        }

        char ack[3] = {0};
        if (!recv_all(sock, ack, 2)) {
            std::cerr << "\n[-] Connection lost while waiting chunk ACK\n";
            return;
        }

        if (std::string(ack, 2) == "ER") {
            std::cerr << "\n[-] Server rejected chunk " << chunk_header.chunk_index << "\n";
            return;
        }

        current_offset += actual_read;
        std::cout << "\r[Client] Progress: " << current_offset << " / " << file_size << " bytes" << std::flush;

        // Delay makes Ctrl+C testing easier. Remove it if you want maximum speed.
        usleep(20000);
    }

    std::cout << "\n[Client] All required chunks sent. Waiting file verification...\n";

    char final_response[3] = {0};
    if (!recv_all(sock, final_response, 2)) {
        std::cerr << "[-] Failed to receive final result\n";
        return;
    }

    if (std::string(final_response, 2) == "OK") {
        std::cout << "[Client] SUCCESS: file integrity verified by server\n";
    } else {
        std::cout << "[Client] FAILED: server SHA256 check failed\n";
    }
}

void receiveFileWithChunkShaAndResume(int client_socket) {
    FileHeader header{};
    if (!recv_all(client_socket, &header, sizeof(header))) {
        std::cerr << "[-] Client disconnected before sending file header\n";
        return;
    }

    std::string filename(header.filename);
    std::string expected_file_hash(header.file_sha256);
    std::string save_path = "received_" + filename;

    std::cout << "\n[Server] Receiving file: " << filename << "\n";
    std::cout << "[Server] Expected size: " << header.file_size << " bytes\n";
    std::cout << "[Server] Expected SHA256: " << expected_file_hash << "\n";

    uint64_t existing_size = 0;
    if (fs::exists(save_path)) {
        existing_size = fs::file_size(save_path);
        if (existing_size > header.file_size) {
            std::cout << "[Server] Existing file larger than source. Restarting from 0.\n";
            fs::remove(save_path);
            existing_size = 0;
        }
    }

    std::cout << "[Server] Existing bytes: " << existing_size << "\n";

    if (!send_all(client_socket, &existing_size, sizeof(existing_size))) {
        std::cerr << "[-] Failed to send resume offset\n";
        return;
    }

    std::ofstream outfile(save_path, std::ios::binary | std::ios::app);
    if (!outfile.is_open()) {
        std::cerr << "[-] Cannot open output file\n";
        return;
    }

    uint64_t current_size = existing_size;
    std::vector<char> buffer(BUFFER_SIZE);

    while (current_size < header.file_size) {
        ChunkHeader chunk_header{};
        if (!recv_all(client_socket, &chunk_header, sizeof(chunk_header))) {
            std::cerr << "\n[Server] Connection lost. Resume possible later at " << current_size << " bytes\n";
            outfile.close();
            return;
        }

        if (chunk_header.data_size > BUFFER_SIZE) {
            std::cerr << "[-] Invalid chunk size\n";
            outfile.close();
            return;
        }

        if (!recv_all(client_socket, buffer.data(), chunk_header.data_size)) {
            std::cerr << "\n[Server] Connection lost while receiving chunk data\n";
            outfile.close();
            return;
        }

        std::string actual_chunk_hash = sha256_buffer(buffer.data(), chunk_header.data_size);
        std::string expected_chunk_hash(chunk_header.chunk_sha256);

        if (actual_chunk_hash != expected_chunk_hash) {
            std::cerr << "\n[Server] Chunk SHA mismatch at chunk " << chunk_header.chunk_index << "\n";
            send_all(client_socket, "ER", 2);
            outfile.close();
            return;
        }

        if (chunk_header.offset != current_size) {
            std::cerr << "\n[Server] Offset mismatch. Expected " << current_size
                      << ", got " << chunk_header.offset << "\n";
            send_all(client_socket, "ER", 2);
            outfile.close();
            return;
        }

        outfile.write(buffer.data(), chunk_header.data_size);
        current_size += chunk_header.data_size;

        send_all(client_socket, "OK", 2);

        std::cout << "\r[Server] Progress: " << current_size << " / " << header.file_size << " bytes" << std::flush;
    }

    outfile.close();

    std::cout << "\n[Server] File received. Checking whole-file SHA256...\n";
    std::string actual_file_hash = sha256_file(save_path);

    std::cout << "[Server] Actual SHA256:   " << actual_file_hash << "\n";
    std::cout << "[Server] Expected SHA256: " << expected_file_hash << "\n";

    if (actual_file_hash == expected_file_hash) {
        std::cout << "[Server] SUCCESS: file integrity OK\n";
        send_all(client_socket, "OK", 2);
    } else {
        std::cout << "[Server] FAILED: whole-file SHA mismatch\n";
        send_all(client_socket, "ER", 2);
    }
}
