#include "../include/Network.hpp"

#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int init_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[-] Cannot create socket\n";
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
    const char* p = static_cast<const char*>(data);
    size_t sent_total = 0;

    while (sent_total < size) {
        ssize_t sent = send(sock, p + sent_total, size - sent_total, 0);
        if (sent <= 0) return false;
        sent_total += sent;
    }

    return true;
}

bool recv_all(int sock, void* data, size_t size) {
    char* p = static_cast<char*>(data);
    size_t recv_total = 0;

    while (recv_total < size) {
        ssize_t n = recv(sock, p + recv_total, size - recv_total, 0);
        if (n <= 0) return false;
        recv_total += n;
    }

    return true;
}
