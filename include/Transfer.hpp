#pragma once

#include <string>

void sendFileRandomChunkShaResume(int sock, const std::string& filepath);
bool receiveFileRandomChunkShaResume(int client_socket);