# TCP File Transfer with SHA256, Random Chunks, Resume and Multi-file over One TCP Connection

## Giới thiệu

Project xây dựng hệ thống truyền file giữa Client và Server sử dụng TCP Socket bằng C++.

### Tính năng

* Gửi nhiều file trong một lần chạy Client
* Chỉ sử dụng 1 TCP connection duy nhất
* Truyền file theo cơ chế chunk
* Chunk được gửi theo thứ tự ngẫu nhiên
* Kiểm tra SHA256 cho từng chunk
* Kiểm tra SHA256 cho toàn bộ file
* Resume khi mất kết nối
* Chỉ gửi lại các chunk còn thiếu
* Lưu trạng thái nhận bằng file `.state`

---

## Công nghệ sử dụng

* C++17
* TCP Socket
* OpenSSL SHA256
* CMake
* Linux / Ubuntu

---

## Kiến trúc hệ thống

```text
Client
│
├── client.cpp
├── Network.cpp
├── Transfer.cpp
└── SHA256Util.cpp

        TCP

Server
│
├── server.cpp
├── Network.cpp
├── Transfer.cpp
└── SHA256Util.cpp
```

---

## Cấu trúc project

```text
socket_sha256_resume_random_chunks_verbose/
│
├── build.sh
├── CMakeLists.txt
├── README.md
│
├── include/
│   ├── Config.hpp
│   ├── Network.hpp
│   ├── Protocol.hpp
│   ├── SHA256Util.hpp
│   └── Transfer.hpp
│
├── src/
│   ├── client.cpp
│   ├── server.cpp
│   ├── Network.cpp
│   ├── SHA256Util.cpp
│   └── Transfer.cpp
│
└── build/
    ├── client
    └── server
```

---

## Mô hình truyền dữ liệu

Project hiện tại sử dụng:

```text
1 TCP Connection
N File
```

Client chỉ kết nối một lần.

```text
Client                                   Server
  |                                         |
  |------ TCP Connect --------------------->|
  |                                         |
  |------ FileHeader(file1) --------------->|
  |<----- MissingInfo(file1) ---------------|
  |------ ChunkHeader + ChunkData --------->|
  |<----- ACK OK / ER ----------------------|
  |------ ChunkHeader + ChunkData --------->|
  |<----- ACK OK / ER ----------------------|
  |<----- FinalResult(file1) ---------------|
  |                                         |
  |------ FileHeader(file2) --------------->|
  |<----- MissingInfo(file2) ---------------|
  |------ ChunkHeader + ChunkData --------->|
  |<----- ACK OK / ER ----------------------|
  |<----- FinalResult(file2) ---------------|
  |                                         |
  |------ FileHeader(fileN) --------------->|
  |<----- MissingInfo(fileN) ---------------|
  |------ ChunkHeader + ChunkData --------->|
  |<----- ACK OK / ER ----------------------|
  |<----- FinalResult(fileN) ---------------|
  |                                         |
  |------ Close Connection ---------------->|
```

---

## FileHeader

Metadata của file được gửi trước khi truyền dữ liệu.

```cpp
struct FileHeader
{
    char filename[256];
    uint64_t file_size;
    uint64_t chunk_size;
    uint64_t total_chunks;
    char file_sha256[65];
};
```

---

## ChunkHeader

Metadata của từng chunk.

```cpp
struct ChunkHeader
{
    uint64_t chunk_index;
    uint64_t offset;
    uint32_t data_size;
    char chunk_sha256[65];
};
```

---

## Quy trình Resume

### Lần gửi đầu

```text
Client gửi FileHeader
        ↓
Server tạo file nhận
        ↓
Server gửi missing chunk list
        ↓
Client gửi chunk
        ↓
Server lưu bitmap
        ↓
Server kiểm tra SHA256
```

### Mất kết nối

Server lưu:

```text
received_filename
received_filename.state
```

Ví dụ:

```text
1 1 1 0 1 0 0
```

Trong đó:

```text
1 = đã nhận
0 = chưa nhận
```

---

## Resume lần tiếp theo

```text
Client gửi FileHeader
        ↓
Server đọc file .state
        ↓
Server xác định chunk còn thiếu
        ↓
Server gửi missing chunk list
        ↓
Client chỉ gửi các chunk còn thiếu
```

---

## Kiểm tra lỗi

### SHA256 từng chunk

Client:

```text
ChunkData
↓
SHA256(chunk)
↓
ChunkHeader
```

Server:

```text
Nhận Chunk
↓
Tính SHA256
↓
So sánh
```

Nếu đúng:

```text
ACK = OK
```

Nếu sai:

```text
ACK = ER
```

---

### SHA256 toàn file

Sau khi nhận đủ chunk:

```text
SHA256(received_file)
==
SHA256(original_file)
```

Nếu đúng:

```text
FinalResult = OK
```

Nếu sai:

```text
FinalResult = ER
```

---

## Build

Cài đặt thư viện:

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev
```

Build:

```bash
./build.sh
```

hoặc:

```bash
mkdir build
cd build

cmake ..
cmake --build . -j$(nproc)
```

---

## Chạy chương trình

### Server

```bash
cd build
./server
```

Output:

```text
[*] Server listening on port 54321...
```

### Client

```bash
cd build
./client
```

Nhập thư mục:

```text
Enter folder path:
/home/user/test_files
```

Chọn file:

```text
1
```

hoặc:

```text
1 2
```

hoặc:

```text
all
```

---

## Ví dụ output

```text
========== SEND QUEUE ==========
1. CMakeLists.txt | size: 540 bytes | chunks: 1
2. test_100mb.bin | size: 104857600 bytes | chunks: 1600
3. test_1gb.bin | size: 1073741824 bytes | chunks: 16384
================================

CMakeLists.txt | size: 540 bytes | chunks: 1 | need send: 1
[OK] CMakeLists.txt

test_100mb.bin | size: 104857600 bytes | chunks: 1600 | need send: 1600
[OK] test_100mb.bin

test_1gb.bin | size: 1073741824 bytes | chunks: 16384 | need send: 8977
[OK] test_1gb.bin
```

---

## Ưu điểm

* Chỉ dùng 1 TCP connection
* Hỗ trợ Resume
* Hỗ trợ truyền file lớn
* Kiểm tra toàn vẹn dữ liệu
* Chỉ gửi phần còn thiếu
* Tiết kiệm băng thông khi truyền lại
* Hỗ trợ nhiều file trong một phiên truyền

---

## Hướng phát triển

* Retry khi ACK = ER
* AES Encryption
* Multi-thread Server
* Nén dữ liệu trước khi gửi
* GUI Client
* Thanh tiến trình truyền file
* Hỗ trợ truyền thư mục đệ quy

---

## Kết luận

Project hiện thực cơ chế truyền file tin cậy dựa trên:

```text
TCP + SHA256 + Resume + Random Chunks + Multi-file + Single Connection
```

Client chỉ mở một kết nối TCP duy nhất, truyền nhiều file tuần tự, hỗ trợ resume và kiểm tra toàn vẹn dữ liệu ở cả mức chunk và mức file.
