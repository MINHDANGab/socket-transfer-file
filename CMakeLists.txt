cmake_minimum_required(VERSION 3.15)
project(socket_sha256_resume_random_chunks_verbose)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

find_package(OpenSSL REQUIRED)

add_executable(server
    src/server.cpp
    src/Network.cpp
    src/Transfer.cpp
    src/SHA256Util.cpp
)

add_executable(client
    src/client.cpp
    src/Network.cpp
    src/Transfer.cpp
    src/SHA256Util.cpp
)

target_link_libraries(server PRIVATE OpenSSL::Crypto)
target_link_libraries(client PRIVATE OpenSSL::Crypto)
