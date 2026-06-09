#include "../include/Config.hpp"
#include "../include/Network.hpp"
#include "../include/Transfer.hpp"
#include "../include/Protocol.hpp"
#include "../include/SHA256Util.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <unistd.h>

namespace fs = std::filesystem;

static void strip_quotes(std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }

    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        s = s.substr(1, s.size() - 2);
    }
}

static std::vector<std::string> list_files(const std::string& folder) {
    std::vector<std::string> files;

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }

    return files;
}

static std::vector<int> parse_choice(const std::string& input, int total) {
    std::vector<int> selected;

    if (input == "all" || input == "ALL") {
        for (int i = 0; i < total; ++i) {
            selected.push_back(i);
        }
        return selected;
    }

    std::stringstream ss(input);
    int index;

    while (ss >> index) {
        if (index >= 1 && index <= total) {
            selected.push_back(index - 1);
        }
    }

    return selected;
}

static uint64_t calc_total_chunks(uint64_t file_size, uint64_t chunk_size) {
    if (file_size == 0) return 0;
    return (file_size + chunk_size - 1) / chunk_size;
}

static uint64_t query_missing_chunks(const std::string& filepath) {
    int sock = connect_to_server(SERVER_IP, PORT);

    if (sock < 0) {
        return static_cast<uint64_t>(-1);
    }

    std::string filename = fs::path(filepath).filename().string();
    uint64_t file_size = fs::file_size(filepath);
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
        close(sock);
        return static_cast<uint64_t>(-1);
    }

    uint64_t missing_count = 0;

    if (!recv_all(sock, &missing_count, sizeof(missing_count))) {
        close(sock);
        return static_cast<uint64_t>(-1);
    }

    if (missing_count > 0) {
        std::vector<uint64_t> temp(missing_count);
        recv_all(sock, temp.data(), missing_count * sizeof(uint64_t));
    }

    close(sock);
    return missing_count;
}

int main() {
    std::string folder;

    std::cout << "Enter folder path: ";
    std::getline(std::cin, folder);
    strip_quotes(folder);

    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        std::cerr << "[-] Invalid folder\n";
        return 1;
    }

    std::vector<std::string> files = list_files(folder);

    if (files.empty()) {
        std::cout << "[-] No files in this folder\n";
        return 0;
    }

    std::cout << "\n========== FILE LIST ==========\n";

    for (size_t i = 0; i < files.size(); ++i) {
        std::cout << i + 1 << ". "
                  << fs::path(files[i]).filename().string()
                  << "\n";
    }

    std::cout << "===============================\n";

    std::cout << "Choose file to send, example: 1 or 1 2 or all: ";

    std::string choice;
    std::getline(std::cin, choice);

    std::vector<int> selected = parse_choice(choice, files.size());

    if (selected.empty()) {
        std::cout << "[-] No valid file selected\n";
        return 0;
    }

    std::cout << "\n========== SEND QUEUE ==========\n";

    for (size_t i = 0; i < selected.size(); ++i) {
        std::string filepath = files[selected[i]];
        std::string filename = fs::path(filepath).filename().string();

        uint64_t file_size = fs::file_size(filepath);
        uint64_t total_chunks = calc_total_chunks(file_size, BUFFER_SIZE);
        uint64_t need_send = query_missing_chunks(filepath);

        std::cout << i + 1 << ". "
                  << filename
                  << " | size: " << file_size << " bytes"
                  << " | chunks: " << total_chunks;

        if (need_send == static_cast<uint64_t>(-1)) {
            std::cout << " | need send: connect failed\n";
        } else {
            std::cout << " | need send: " << need_send << "\n";
        }
    }

    std::cout << "================================\n";

    for (int idx : selected) {
        std::string filepath = files[idx];

        int sock = connect_to_server(SERVER_IP, PORT);

        if (sock < 0) {
            std::cerr << "[-] Cannot connect to server\n";
            continue;
        }

        sendFileRandomChunkShaResume(sock, filepath);

        close(sock);
    }

    std::cout << "\n[+] Done\n";
    return 0;
}