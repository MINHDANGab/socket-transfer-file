#pragma once

#include <string>
#include <vector>

std::string sha256_file(const std::string& filepath);
std::string sha256_buffer(const char* data, size_t size);
