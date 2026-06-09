#include "../include/Transfer.hpp"
#include "../include/Config.hpp"
#include "../include/Protocol.hpp"
#include "../include/Network.hpp"
#include "../include/SHA256Util.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string basename_of(const std::string& filepath) {
    return fs::path(filepath).filename().string();
}

static uint64_t get_file_size(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return static_cast<uint64_t>(-1);
    return static_cast<uint64_t>(file.tellg());
}

static uint64_t calc_total_chunks(uint64_t file_size, uint64_t chunk_size) {
    if (file_size == 0) return 0;
    return (file_size + chunk_size - 1) / chunk_size;
}

static std::string data_path_of(const std::string& filename) {
    return "received_" + filename;
}

static std::string state_path_of(const std::string& filename) {
    return "received_" + filename + ".state";
}

static std::vector<unsigned char> load_state(const std::string& state_path, uint64_t total_chunks) {
    std::vector<unsigned char> bitmap(total_chunks, 0);

    std::ifstream in(state_path, std::ios::binary);
    if (!in.is_open()) return bitmap;

    in.read(reinterpret_cast<char*>(bitmap.data()), static_cast<std::streamsize>(bitmap.size()));
    return bitmap;
}

static void save_state(const std::string& state_path, const std::vector<unsigned char>& bitmap) {
    std::ofstream out(state_path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bitmap.data()), static_cast<std::streamsize>(bitmap.size()));
}

static uint64_t count_received_chunks(const std::vector<unsigned char>& bitmap) {
    return static_cast<uint64_t>(std::count(bitmap.begin(), bitmap.end(), 1));
}

static bool all_chunks_received(const std::vector<unsigned char>& bitmap) {
    return std::all_of(bitmap.begin(), bitmap.end(), [](unsigned char x) {
        return x == 1;
    });
}

static void ensure_output_file_size(const std::string& data_path, uint64_t file_size) {
    if (!fs::exists(data_path)) {
        std::ofstream create(data_path, std::ios::binary);
        create.close();
    }

    fs::resize_file(data_path, file_size);
}

void sendFileRandomChunkShaResume(int sock, const std::string& filepath) {
    uint64_t file_size = get_file_size(filepath);

    if (file_size == static_cast<uint64_t>(-1)) {
        std::cerr << "[-] Cannot open file: " << filepath << "\n";
        return;
    }

    std::string filename = basename_of(filepath);
    uint64_t chunk_size = BUFFER_SIZE;
    uint64_t total_chunks = calc_total_chunks(file_size, chunk_size);
    std::string file_hash = sha256_file(filepath);

    FileHeader header{};
    std::strncpy(header.filename, filename.c_str(), sizeof(header.filename) - 1);
    header.file_size = file_size;
    header.chunk_size = chunk_size;
    header.total_chunks = total_chunks;
    std::strncpy(header.file_sha256, file_hash.c_str(), sizeof(header.file_sha256) - 1);

    if (!send_all(sock, &header, sizeof(header))) {
        std::cerr << "[-] Failed to send FileHeader\n";
        return;
    }

    uint64_t missing_count = 0;

    if (!recv_all(sock, &missing_count, sizeof(missing_count))) {
        std::cerr << "[-] Failed to receive missing chunk count\n";
        return;
    }

    std::vector<uint64_t> missing_chunks(missing_count);

    if (missing_count > 0) {
        if (!recv_all(sock, missing_chunks.data(), missing_count * sizeof(uint64_t))) {
            std::cerr << "[-] Failed to receive missing chunk list\n";
            return;
        }
    }

    std::shuffle(
        missing_chunks.begin(),
        missing_chunks.end(),
        std::mt19937{std::random_device{}()}
    );

    std::ifstream infile(filepath, std::ios::binary);

    if (!infile.is_open()) {
        std::cerr << "[-] Cannot open source file\n";
        return;
    }

    std::vector<char> buffer(BUFFER_SIZE);

    for (uint64_t chunk_index : missing_chunks) {
        uint64_t offset = chunk_index * chunk_size;
        uint64_t remain = file_size - offset;
        uint32_t data_size = static_cast<uint32_t>(std::min<uint64_t>(chunk_size, remain));

        infile.seekg(offset, std::ios::beg);
        infile.read(buffer.data(), data_size);

        uint32_t actual_read = static_cast<uint32_t>(infile.gcount());

        if (actual_read != data_size) {
            std::cerr << "[-] Read file error at chunk " << chunk_index << "\n";
            return;
        }

        std::string chunk_hash = sha256_buffer(buffer.data(), actual_read);

        ChunkHeader chunk_header{};
        chunk_header.chunk_index = chunk_index;
        chunk_header.offset = offset;
        chunk_header.data_size = actual_read;
        std::strncpy(chunk_header.chunk_sha256, chunk_hash.c_str(), sizeof(chunk_header.chunk_sha256) - 1);

        if (!send_all(sock, &chunk_header, sizeof(chunk_header))) {
            std::cerr << "[-] Connection lost while sending ChunkHeader\n";
            return;
        }

        if (!send_all(sock, buffer.data(), actual_read)) {
            std::cerr << "[-] Connection lost while sending ChunkData\n";
            return;
        }

        char ack[3] = {0};

        if (!recv_all(sock, ack, 2)) {
            std::cerr << "[-] Connection lost while waiting ACK\n";
            return;
        }

        if (std::string(ack, 2) == "ER") {
            std::cerr << "[-] Server rejected chunk " << chunk_index << "\n";
            return;
        }

        usleep(20000);
    }

    char final_response[3] = {0};

    if (!recv_all(sock, final_response, 2)) {
        std::cerr << "[-] Failed to receive final response\n";
        return;
    }

    if (std::string(final_response, 2) == "OK") {
        std::cout << "[OK] " << filename << "\n";
    } else {
        std::cout << "[FAILED] " << filename << "\n";
    }
}

void receiveFileRandomChunkShaResume(int client_socket) {
    FileHeader header{};

    if (!recv_all(client_socket, &header, sizeof(header))) {
        std::cerr << "[-] Client disconnected before FileHeader\n";
        return;
    }

    std::string filename(header.filename);
    std::string expected_file_hash(header.file_sha256);
    std::string data_path = data_path_of(filename);
    std::string state_path = state_path_of(filename);

    if (fs::exists(data_path) && fs::file_size(data_path) != header.file_size) {
        fs::remove(data_path);

        if (fs::exists(state_path)) {
            fs::remove(state_path);
        }
    }

    ensure_output_file_size(data_path, header.file_size);

    std::vector<unsigned char> bitmap = load_state(state_path, header.total_chunks);

    if (bitmap.size() != header.total_chunks) {
        bitmap.assign(header.total_chunks, 0);
    }

    std::vector<uint64_t> missing_chunks;
    missing_chunks.reserve(header.total_chunks);

    for (uint64_t i = 0; i < header.total_chunks; ++i) {
        if (bitmap[i] == 0) {
            missing_chunks.push_back(i);
        }
    }

    uint64_t missing_count = static_cast<uint64_t>(missing_chunks.size());

    if (!send_all(client_socket, &missing_count, sizeof(missing_count))) {
        std::cerr << "[-] Failed to send missing chunk count\n";
        return;
    }

    if (missing_count > 0) {
        if (!send_all(client_socket, missing_chunks.data(), missing_count * sizeof(uint64_t))) {
            std::cerr << "[-] Failed to send missing chunk list\n";
            return;
        }
    }

    std::fstream outfile(data_path, std::ios::binary | std::ios::in | std::ios::out);

    if (!outfile.is_open()) {
        std::cerr << "[-] Cannot open output file\n";
        return;
    }

    std::vector<char> buffer(BUFFER_SIZE);

    while (!all_chunks_received(bitmap)) {
        ChunkHeader chunk_header{};

        if (!recv_all(client_socket, &chunk_header, sizeof(chunk_header))) {
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        if (chunk_header.chunk_index >= header.total_chunks) {
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        if (chunk_header.data_size > BUFFER_SIZE) {
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        uint64_t expected_offset = chunk_header.chunk_index * header.chunk_size;

        if (chunk_header.offset != expected_offset) {
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        if (!recv_all(client_socket, buffer.data(), chunk_header.data_size)) {
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        std::string actual_chunk_hash =
            sha256_buffer(buffer.data(), chunk_header.data_size);

        std::string expected_chunk_hash(chunk_header.chunk_sha256);

        if (actual_chunk_hash != expected_chunk_hash) {
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        if (bitmap[chunk_header.chunk_index] == 0) {
            outfile.seekp(static_cast<std::streamoff>(chunk_header.offset), std::ios::beg);
            outfile.write(buffer.data(), chunk_header.data_size);
            outfile.flush();

            bitmap[chunk_header.chunk_index] = 1;
            save_state(state_path, bitmap);
        }

        send_all(client_socket, "OK", 2);
    }

    outfile.close();

    std::string actual_file_hash = sha256_file(data_path);

    if (actual_file_hash == expected_file_hash) {
        send_all(client_socket, "OK", 2);

        if (fs::exists(state_path)) {
            fs::remove(state_path);
        }
    } else {
        send_all(client_socket, "ER", 2);
    }
}