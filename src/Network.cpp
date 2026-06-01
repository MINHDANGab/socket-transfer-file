#include "../include/Network.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int init_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[-] Cannot create server socket\n";
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[-] Bind failed\n";
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0) {
        std::cerr << "[-] Listen failed\n";
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int connect_to_server(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

bool send_all(int sock, const void* data, size_t size) {
    const char* buffer = static_cast<const char*>(data);
    size_t total = 0;

    while (total < size) {
        ssize_t sent = send(sock, buffer + total, size - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }

    return true;
}

bool recv_all(int sock, void* data, size_t size) {
    char* buffer = static_cast<char*>(data);
    size_t total = 0;

    while (total < size) {
        ssize_t received = recv(sock, buffer + total, size - total, 0);
        if (received <= 0) return false;
        total += received;
    }

    return true;
}
