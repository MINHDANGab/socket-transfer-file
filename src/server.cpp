#include "../include/Config.hpp"
#include "../include/Network.hpp"
#include "../include/Transfer.hpp"

#include <iostream>
#include <unistd.h>
#include <sys/socket.h>

int main() {
    int server_fd = init_server_socket(PORT);

    if (server_fd < 0) {
        return 1;
    }

    std::cout << "[*] Server listening on port " << PORT << "...\n";

    while (true) {
        int client_socket = accept(server_fd, nullptr, nullptr);

        if (client_socket < 0) {
            continue;
        }

        std::cout << "\n[+] Client connected\n";

        while (receiveFileRandomChunkShaResume(client_socket)) {
        }

        close(client_socket);

        std::cout << "[*] Client disconnected\n";
    }

    close(server_fd);
    return 0;
}