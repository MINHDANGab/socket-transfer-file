#pragma once

#include <cstddef>

int init_server_socket(int port);
int connect_to_server(const char* ip, int port);

bool send_all(int sock, const void* data, size_t size);
bool recv_all(int sock, void* data, size_t size);
