#pragma once

#include <string>

void sendFileWithChunkShaAndResume(int sock, const std::string& filepath);
void receiveFileWithChunkShaAndResume(int client_socket);
