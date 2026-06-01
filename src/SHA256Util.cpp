#include "../include/SHA256Util.hpp"

#include <openssl/evp.h>
#include <fstream>
#include <sstream>
#include <iomanip>

static std::string to_hex(const unsigned char* hash, unsigned int length) {
    std::ostringstream oss;
    for (unsigned int i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return oss.str();
}

std::string sha256_buffer(const char* data, size_t size) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data, size);
    EVP_DigestFinal_ex(ctx, hash, &length);

    EVP_MD_CTX_free(ctx);
    return to_hex(hash, length);
}

std::string sha256_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "";

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    char buffer[65536];

    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        std::streamsize n = file.gcount();
        if (n > 0) {
            EVP_DigestUpdate(ctx, buffer, static_cast<size_t>(n));
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;

    EVP_DigestFinal_ex(ctx, hash, &length);
    EVP_MD_CTX_free(ctx);

    return to_hex(hash, length);
}
