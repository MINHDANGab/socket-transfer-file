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
#include <numeric>
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

static void print_chunk_list_sample(const std::vector<uint64_t>& chunks, const std::string& title) {
    std::cout << title << " count=" << chunks.size() << "\n";

    if (chunks.empty()) {
        std::cout << "  none\n";
        return;
    }

    std::cout << "  sample: ";
    size_t limit = std::min<size_t>(chunks.size(), 30);
    for (size_t i = 0; i < limit; ++i) {
        std::cout << chunks[i] << " ";
    }

    if (chunks.size() > limit) {
        std::cout << "...";
    }

    std::cout << "\n";
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

    std::cout << "\n========== CLIENT FILE INFO ==========\n";
    std::cout << "[Client] File: " << filename << "\n";
    std::cout << "[Client] Size: " << file_size << " bytes\n";
    std::cout << "[Client] Chunk size: " << chunk_size << " bytes\n";
    std::cout << "[Client] Total chunks: " << total_chunks << "\n";
    std::cout << "[Client] Whole SHA256: " << file_hash << "\n";
    std::cout << "======================================\n";

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

    uint64_t already_have = total_chunks - missing_count;

    std::cout << "\n========== CLIENT RESUME INFO ==========\n";
    std::cout << "[Client] Server already has chunks: " << already_have << "/" << total_chunks << "\n";
    std::cout << "[Client] Need to send chunks: " << missing_count << "\n";
    print_chunk_list_sample(missing_chunks, "[Client] Missing chunk list before shuffle");
    std::cout << "========================================\n";

    if (missing_count == 0) {
        std::cout << "[Client] Server already has all chunks. Waiting final SHA result...\n";
    }

    // Đảo thứ tự gửi để chứng minh server xử lý được chunk đến sai thứ tự.
    std::shuffle(
        missing_chunks.begin(),
        missing_chunks.end(),
        std::mt19937{std::random_device{}()}
    );

    print_chunk_list_sample(missing_chunks, "[Client] Send order after shuffle");

    std::ifstream infile(filepath, std::ios::binary);
    if (!infile.is_open()) {
        std::cerr << "[-] Cannot open source file\n";
        return;
    }

    std::vector<char> buffer(BUFFER_SIZE);
    uint64_t sent_chunks = 0;

    for (uint64_t chunk_index : missing_chunks) {
        uint64_t offset = chunk_index * chunk_size;
        uint64_t remain = file_size - offset;
        uint32_t data_size = static_cast<uint32_t>(std::min<uint64_t>(chunk_size, remain));

        infile.seekg(offset, std::ios::beg);
        infile.read(buffer.data(), data_size);

        uint32_t actual_read = static_cast<uint32_t>(infile.gcount());
        if (actual_read != data_size) {
            std::cerr << "\n[-] Read file error at chunk " << chunk_index << "\n";
            return;
        }

        std::string chunk_hash = sha256_buffer(buffer.data(), actual_read);

        ChunkHeader chunk_header{};
        chunk_header.chunk_index = chunk_index;
        chunk_header.offset = offset;
        chunk_header.data_size = actual_read;
        std::strncpy(chunk_header.chunk_sha256, chunk_hash.c_str(), sizeof(chunk_header.chunk_sha256) - 1);

#if VERBOSE_CHUNK_LOG
        std::cout
            << "\n[CLIENT SEND]"
            << " chunk=" << chunk_index
            << " offset=" << offset
            << " size=" << actual_read
            << " sha=" << chunk_hash.substr(0, 12) << "..."
            << std::endl;
#endif

        if (!send_all(sock, &chunk_header, sizeof(chunk_header))) {
            std::cerr << "\n[-] Connection lost while sending ChunkHeader\n";
            return;
        }

        if (!send_all(sock, buffer.data(), actual_read)) {
            std::cerr << "\n[-] Connection lost while sending ChunkData\n";
            return;
        }

        char ack[3] = {0};
        if (!recv_all(sock, ack, 2)) {
            std::cerr << "\n[-] Connection lost while waiting ACK\n";
            return;
        }

        if (std::string(ack, 2) == "ER") {
            std::cerr << "\n[-] Server rejected chunk " << chunk_index << "\n";
            return;
        }

#if VERBOSE_CHUNK_LOG
        std::cout << "[CLIENT ACK] chunk=" << chunk_index << " status=OK" << std::endl;
#endif

        sent_chunks++;

        std::cout << "\r[Client] Sent chunks this run: " << sent_chunks << "/" << missing_count
                  << " | current chunk index: " << chunk_index << std::flush;

        usleep(20000);
    }

    std::cout << "\n[Client] Done sending missing chunks. Waiting final verification...\n";

    char final_response[3] = {0};
    if (!recv_all(sock, final_response, 2)) {
        std::cerr << "[-] Failed to receive final response\n";
        return;
    }

    if (std::string(final_response, 2) == "OK") {
        std::cout << "[Client] SUCCESS: whole-file SHA256 matched\n";
    } else {
        std::cout << "[Client] FAILED: whole-file SHA256 mismatch\n";
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

    std::cout << "\n========== SERVER FILE INFO ==========\n";
    std::cout << "[Server] File: " << filename << "\n";
    std::cout << "[Server] Size: " << header.file_size << " bytes\n";
    std::cout << "[Server] Chunk size: " << header.chunk_size << " bytes\n";
    std::cout << "[Server] Total chunks: " << header.total_chunks << "\n";
    std::cout << "[Server] Expected whole SHA256: " << expected_file_hash << "\n";
    std::cout << "======================================\n";

    // Nếu file nhận dở không hợp lệ về size thì reset.
    if (fs::exists(data_path) && fs::file_size(data_path) != header.file_size) {
        std::cout << "[Server] Existing file size mismatch. Reset old received file.\n";
        fs::remove(data_path);
        if (fs::exists(state_path)) fs::remove(state_path);
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
    uint64_t received_count = header.total_chunks - missing_count;

    std::cout << "\n========== SERVER RESUME INFO ==========\n";
    std::cout << "[Server] Already received chunks: "
              << received_count << "/" << header.total_chunks << "\n";
    std::cout << "[Server] Missing chunks: " << missing_count << "\n";
    print_chunk_list_sample(missing_chunks, "[Server] Missing chunk list");
    std::cout << "========================================\n";

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
            std::cerr << "\n[Server] Connection lost. Resume later.\n";
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        if (chunk_header.chunk_index >= header.total_chunks) {
            std::cerr << "\n[Server] Invalid chunk index\n";
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        if (chunk_header.data_size > BUFFER_SIZE) {
            std::cerr << "\n[Server] Invalid chunk size\n";
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        uint64_t expected_offset = chunk_header.chunk_index * header.chunk_size;
        if (chunk_header.offset != expected_offset) {
            std::cerr << "\n[Server] Invalid offset for chunk " << chunk_header.chunk_index << "\n";
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        if (!recv_all(client_socket, buffer.data(), chunk_header.data_size)) {
            std::cerr << "\n[Server] Connection lost while receiving chunk data. Resume later.\n";
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

        std::string actual_chunk_hash = sha256_buffer(buffer.data(), chunk_header.data_size);
        std::string expected_chunk_hash(chunk_header.chunk_sha256);

#if VERBOSE_CHUNK_LOG
        std::cout
            << "\n[SERVER RECV]"
            << " chunk=" << chunk_header.chunk_index
            << " offset=" << chunk_header.offset
            << " size=" << chunk_header.data_size
            << " sha_recv=" << expected_chunk_hash.substr(0, 12) << "..."
            << " sha_calc=" << actual_chunk_hash.substr(0, 12) << "..."
            << std::endl;
#endif

        if (actual_chunk_hash != expected_chunk_hash) {
            std::cerr << "[SERVER CHECK] chunk=" << chunk_header.chunk_index << " SHA=ERR\n";
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return;
        }

#if VERBOSE_CHUNK_LOG
        std::cout << "[SERVER CHECK] chunk=" << chunk_header.chunk_index << " SHA=OK" << std::endl;
#endif

        if (bitmap[chunk_header.chunk_index] == 0) {
            outfile.seekp(static_cast<std::streamoff>(chunk_header.offset), std::ios::beg);
            outfile.write(buffer.data(), chunk_header.data_size);
            outfile.flush();

            bitmap[chunk_header.chunk_index] = 1;
            save_state(state_path, bitmap);

#if VERBOSE_CHUNK_LOG
            std::cout << "[SERVER STORE] chunk=" << chunk_header.chunk_index
                      << " stored_at_offset=" << chunk_header.offset << std::endl;
#endif
        } else {
#if VERBOSE_CHUNK_LOG
            std::cout << "[SERVER SKIP] chunk=" << chunk_header.chunk_index
                      << " already received before" << std::endl;
#endif
        }

        send_all(client_socket, "OK", 2);

        uint64_t now_received = count_received_chunks(bitmap);
        std::cout << "\r[Server] Received chunks: " << now_received << "/" << header.total_chunks
                  << " | last chunk index: " << chunk_header.chunk_index << std::flush;
    }

    outfile.close();

    std::cout << "\n[Server] All chunks received. Checking whole-file SHA256...\n";

    std::string actual_file_hash = sha256_file(data_path);

    std::cout << "[Server] Actual:   " << actual_file_hash << "\n";
    std::cout << "[Server] Expected: " << expected_file_hash << "\n";

    if (actual_file_hash == expected_file_hash) {
        std::cout << "[Server] SUCCESS: file integrity OK\n";
        send_all(client_socket, "OK", 2);

        if (fs::exists(state_path)) fs::remove(state_path);
    } else {
        std::cout << "[Server] FAILED: whole-file SHA mismatch\n";
        send_all(client_socket, "ER", 2);
    }
}
