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

static bool all_chunks_received(const std::vector<unsigned char>& bitmap) {
    return std::all_of(bitmap.begin(), bitmap.end(), [](unsigned char x) {
        return x == 1;
    });
}

static uint64_t count_received_chunks(const std::vector<unsigned char>& bitmap) {
    return static_cast<uint64_t>(
        std::count_if(bitmap.begin(), bitmap.end(), [](unsigned char x) {
            return x == 1;
        })
    );
}

static void ensure_output_file_size(const std::string& data_path, uint64_t file_size) {
    if (!fs::exists(data_path)) {
        std::ofstream create(data_path, std::ios::binary);
        create.close();
    }

    fs::resize_file(data_path, file_size);
}

static void print_progress(
    const std::string& filename,
    uint64_t done,
    uint64_t total
) {
    const int bar_width = 40;

    double percent = 1.0;

    if (total > 0) {
        percent = static_cast<double>(done) / static_cast<double>(total);
    }

    if (percent > 1.0) {
        percent = 1.0;
    }

    int filled = static_cast<int>(percent * bar_width);

    std::cout << "\r[PROGRESS] " << filename << " [";

    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) {
            std::cout << "=";
        } else if (i == filled && done < total) {
            std::cout << ">";
        } else {
            std::cout << " ";
        }
    }

    std::cout << "] "
              << done << "/" << total
              << " chunks "
              << static_cast<int>(percent * 100) << "%"
              << std::flush;
}

/*
    Đọc lại đúng vùng dữ liệu vừa ghi xuống file,
    sau đó tính SHA-256 để kiểm tra server đã ghi đúng offset chưa.
*/
static std::string sha256_file_range(
    std::fstream& file,
    uint64_t offset,
    uint32_t size
) {
    std::vector<char> verify_buffer(size);

    file.flush();
    file.clear();

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file.read(verify_buffer.data(), size);

    if (static_cast<uint32_t>(file.gcount()) != size) {
        return "";
    }

    return sha256_buffer(verify_buffer.data(), size);
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

    std::cout << filename
              << " | size: " << file_size << " bytes"
              << " | chunks: " << total_chunks
              << " | need send: " << missing_count
              << "\n";

    /*
        Gửi chunk theo thứ tự ngẫu nhiên.
        Server vẫn ghép đúng vì ghi theo offset.
    */
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

    uint64_t sent_progress = total_chunks - missing_count;
    print_progress(filename, sent_progress, total_chunks);

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
        std::strncpy(
            chunk_header.chunk_sha256,
            chunk_hash.c_str(),
            sizeof(chunk_header.chunk_sha256) - 1
        );

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

        sent_progress++;
        print_progress(filename, sent_progress, total_chunks);

        usleep(20000);
    }

    std::cout << "\n";

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

bool receiveFileRandomChunkShaResume(int client_socket) {
    FileHeader header{};

    if (!recv_all(client_socket, &header, sizeof(header))) {
        return false;
    }

    std::string filename(header.filename);
    std::string expected_file_hash(header.file_sha256);

    std::string data_path = data_path_of(filename);
    std::string state_path = state_path_of(filename);

    /*
        Nếu file cũ tồn tại nhưng kích thước khác file mới,
        server xóa file cũ và state cũ để nhận lại từ đầu.
    */
    if (fs::exists(data_path) && fs::file_size(data_path) != header.file_size) {
        fs::remove(data_path);

        if (fs::exists(state_path)) {
            fs::remove(state_path);
        }
    }

    /*
        Tạo file nhận và resize đúng bằng kích thước file gốc.
        Nhờ vậy server có thể ghi chunk vào đúng vị trí bằng offset.
    */
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
        return false;
    }

    if (missing_count > 0) {
        if (!send_all(client_socket, missing_chunks.data(), missing_count * sizeof(uint64_t))) {
            return false;
        }
    }

    std::fstream outfile(data_path, std::ios::binary | std::ios::in | std::ios::out);

    if (!outfile.is_open()) {
        return false;
    }

    std::vector<char> buffer(BUFFER_SIZE);

    uint64_t received_progress = count_received_chunks(bitmap);
    print_progress(filename, received_progress, header.total_chunks);

    while (!all_chunks_received(bitmap)) {
        ChunkHeader chunk_header{};

        if (!recv_all(client_socket, &chunk_header, sizeof(chunk_header))) {
            std::cout << "\n";
            save_state(state_path, bitmap);
            outfile.close();
            return false;
        }

        if (chunk_header.chunk_index >= header.total_chunks) {
            std::cout << "\n";
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return false;
        }

        if (chunk_header.data_size > BUFFER_SIZE) {
            std::cout << "\n";
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return false;
        }

        uint64_t expected_offset = chunk_header.chunk_index * header.chunk_size;

        if (chunk_header.offset != expected_offset) {
            std::cout << "\n";
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return false;
        }

        if (!recv_all(client_socket, buffer.data(), chunk_header.data_size)) {
            std::cout << "\n";
            save_state(state_path, bitmap);
            outfile.close();
            return false;
        }

        /*
            Check hash từng chunk nhưng không in ra màn hình.
            Client gửi hash chunk trong ChunkHeader.
            Server tính lại hash từ dữ liệu vừa nhận.
        */
        std::string client_chunk_hash(chunk_header.chunk_sha256);

        std::string server_received_hash =
            sha256_buffer(buffer.data(), chunk_header.data_size);

        if (server_received_hash != client_chunk_hash) {
            std::cout << "\n";
            send_all(client_socket, "ER", 2);
            save_state(state_path, bitmap);
            outfile.close();
            return false;
        }

        if (bitmap[chunk_header.chunk_index] == 0) {
            /*
                Xử lý out-of-order:

                Chunk có thể đến không theo thứ tự.
                Server không ghi nối tiếp cuối file.
                Server ghi theo offset:

                offset = chunk_index * chunk_size
            */
            outfile.seekp(static_cast<std::streamoff>(chunk_header.offset), std::ios::beg);
            outfile.write(buffer.data(), chunk_header.data_size);
            outfile.flush();

            /*
                Sau khi ghi, server đọc lại vùng vừa ghi và tính SHA-256.
                Nếu hash sau ghi giống hash client gửi,
                chứng tỏ ghi đúng vị trí và đúng dữ liệu.
                Phần này cũng không in ra màn hình.
            */
            std::string hash_after_write =
                sha256_file_range(outfile, chunk_header.offset, chunk_header.data_size);

            if (hash_after_write != client_chunk_hash) {
                std::cout << "\n";
                send_all(client_socket, "ER", 2);
                save_state(state_path, bitmap);
                outfile.close();
                return false;
            }

            bitmap[chunk_header.chunk_index] = 1;
            save_state(state_path, bitmap);

            received_progress++;
            print_progress(filename, received_progress, header.total_chunks);
        }

        send_all(client_socket, "OK", 2);
    }

    std::cout << "\n";

    outfile.close();

    /*
        Sau khi nhận đủ toàn bộ chunk,
        server tính SHA-256 của file đã ghép hoàn chỉnh.
        Sau đó so sánh với SHA-256 file gốc client gửi trong FileHeader.
    */
    std::string actual_file_hash = sha256_file(data_path);

    std::cout << "\n[FINAL FILE HASH CHECK]\n";
    std::cout << "  filename             : " << filename << "\n";
    std::cout << "  client original hash : " << expected_file_hash << "\n";
    std::cout << "  server merged hash   : " << actual_file_hash << "\n";

    if (actual_file_hash == expected_file_hash) {
        std::cout << "  result               : OK - merged file is correct\n";

        send_all(client_socket, "OK", 2);

        if (fs::exists(state_path)) {
            fs::remove(state_path);
        }

        std::cout
            << "[OK] Received successfully: "
            << filename
            << " ("
            << header.file_size
            << " bytes, "
            << header.total_chunks
            << " chunks)"
            << std::endl;

        return true;
    }

    std::cout << "  result               : ER - merged file hash mismatch\n";

    send_all(client_socket, "ER", 2);

    std::cout
        << "[FAILED] SHA256 mismatch: "
        << filename
        << std::endl;

    return false;
}