#pragma once

#include <string>

void sendFileRandomChunkShaResume(int sock, const std::string& filepath);
void receiveFileRandomChunkShaResume(int client_socket);
