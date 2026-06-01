#include "../include/Config.hpp"
#include "../include/Network.hpp"
#include "../include/Transfer.hpp"

#include <iostream>
#include <string>
#include <unistd.h>

static void strip_quotes(std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        s = s.substr(1, s.size() - 2);
    }
}

int main() {
    int sock = connect_to_server(SERVER_IP, PORT);
    if (sock < 0) {
        std::cerr << "[-] Cannot connect to server. Is server running?\n";
        return 1;
    }

    std::cout << "[+] Connected to server " << SERVER_IP << ":" << PORT << "\n";

    std::string filepath;
    std::cout << "Enter file path to send: ";
    std::getline(std::cin, filepath);
    strip_quotes(filepath);

    sendFileWithChunkShaAndResume(sock, filepath);

    close(sock);
    return 0;
}
