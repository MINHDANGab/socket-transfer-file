# TCP File Transfer with SHA256, Resume and Multi-file over Single TCP Connection

## 1. Giới thiệu

Đây là project xây dựng hệ thống truyền file giữa Client và Server sử dụng giao thức TCP bằng ngôn ngữ C++.

Hệ thống hỗ trợ truyền nhiều file trong một phiên kết nối, kiểm tra tính toàn vẹn dữ liệu bằng SHA256, khôi phục truyền file khi mất kết nối (Resume) và tối ưu băng thông bằng cách chỉ gửi lại các chunk còn thiếu.

Khác với phiên bản truyền file truyền thống, Client chỉ mở một kết nối TCP duy nhất và sử dụng kết nối đó để truyền tuần tự nhiều file tới Server.

---

## 2. Tính năng chính

* Truyền nhiều file trong một phiên làm việc.
* Chỉ sử dụng một TCP connection duy nhất.
* Chia file thành nhiều chunk.
* Gửi chunk theo thứ tự ngẫu nhiên.
* Hỗ trợ Resume khi mất kết nối.
* Chỉ gửi lại các chunk còn thiếu.
* Kiểm tra SHA256 cho toàn bộ file.
* Hỗ trợ truyền file dung lượng lớn.
* Tự động lưu trạng thái truyền bằng file `.state`.
* Hỗ trợ truyền nhiều file liên tiếp trên cùng một socket.

---

## 3. Công nghệ sử dụng

* C++17
* TCP Socket Programming
* OpenSSL SHA256
* CMake
* Linux / Ubuntu

---

## 4. Cấu trúc thư mục

```text
socket_sha256_resume_random_chunks_verbose/
│
├── build.sh
├── CMakeLists.txt
├── README.md
│
├── data/
│   └── các file test
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
    ├── server
    └── data/
```

---

## 5. Kiến trúc hệ thống

```text
+------------------+
|      Client      |
+------------------+
| client.cpp       |
| Transfer.cpp     |
| Network.cpp      |
| SHA256Util.cpp   |
+------------------+
         |
         | TCP
         |
+------------------+
|      Server      |
+------------------+
| server.cpp       |
| Transfer.cpp     |
| Network.cpp      |
| SHA256Util.cpp   |
+------------------+
```

---

## 6. Mô hình kết nối

Project sử dụng:

```text
1 TCP Connection
N File
```

Client chỉ kết nối tới Server một lần.

Ví dụ:

```text
Client
|
|---- File 1 ----|
|---- File 2 ----|
|---- File 3 ----|
|
Close Connection
```

Server nhận lần lượt các file trên cùng một socket.

Ưu điểm:

* Giảm chi phí TCP Handshake.
* Tăng hiệu năng truyền.
* Không cần mở và đóng kết nối liên tục.
* Dễ mở rộng cho số lượng file lớn.

---

## 7. Cấu trúc gói tin

### FileHeader

Metadata của file.

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

Ý nghĩa:

| Trường       | Mô tả                   |
| ------------ | ----------------------- |
| filename     | Tên file                |
| file_size    | Kích thước file         |
| chunk_size   | Kích thước mỗi chunk    |
| total_chunks | Tổng số chunk           |
| file_sha256  | SHA256 của toàn bộ file |

---

### ChunkHeader

Metadata của chunk.

```cpp
struct ChunkHeader
{
    uint64_t chunk_index;
    uint64_t offset;
    uint32_t data_size;
    char chunk_sha256[65];
};
```

Ý nghĩa:

| Trường       | Mô tả                 |
| ------------ | --------------------- |
| chunk_index  | Số thứ tự chunk       |
| offset       | Vị trí ghi trong file |
| data_size    | Kích thước dữ liệu    |
| chunk_sha256 | SHA256 của chunk      |

---

## 8. Quy trình truyền file

### Bước 1

Client gửi FileHeader.

```text
Client
    |
    |---- FileHeader ---->
    |
Server
```

---

### Bước 2

Server kiểm tra trạng thái file.

Nếu đã từng nhận trước đó:

```text
received_filename
received_filename.state
```

Server xác định các chunk còn thiếu.

---

### Bước 3

Server gửi danh sách chunk còn thiếu.

```text
MissingCount
MissingChunkList
```

---

### Bước 4

Client chỉ gửi các chunk còn thiếu.

```text
ChunkHeader
ChunkData
```

---

### Bước 5

Server ghi dữ liệu vào đúng vị trí.

```cpp
outfile.seekp(offset);
outfile.write(...);
```

---

### Bước 6

Sau khi nhận đủ chunk, Server tính SHA256 toàn file.

```text
SHA256(received_file)
==
SHA256(original_file)
```

Nếu đúng:

```text
OK
```

Nếu sai:

```text
ER
```

---

## 9. Cơ chế Resume

Mỗi file sẽ có:

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
1 = Đã nhận
0 = Chưa nhận
```

Khi Client gửi lại cùng file:

```text
Client gửi FileHeader
        ↓
Server đọc file .state
        ↓
Server xác định chunk còn thiếu
        ↓
Server gửi MissingChunkList
        ↓
Client chỉ gửi lại các chunk còn thiếu
```

Nhờ đó không cần truyền lại toàn bộ file.

---

## 10. Vai trò của SHA256

TCP đã đảm bảo dữ liệu được truyền chính xác trên đường mạng.

Tuy nhiên hệ thống vẫn sử dụng SHA256 để kiểm tra tính toàn vẹn dữ liệu ở tầng ứng dụng.

SHA256 giúp:

* Kiểm tra file sau khi Resume.
* Phát hiện lỗi khi ghép lại các chunk.
* Phát hiện lỗi ghi file.
* Đảm bảo file cuối cùng giống hoàn toàn file gốc.

Ví dụ:

```text
SHA256(received_file)
==
SHA256(original_file)
```

Nếu khác nhau:

```text
File lỗi
```

Nếu giống nhau:

```text
File hợp lệ
```

---

## 11. Build Project

### Cài đặt thư viện

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev
```

### Cấp quyền thực thi

```bash
chmod +x build.sh
```

### Build

```bash
./build.sh
```

Script sẽ thực hiện:

```bash
rm -rf build
mkdir -p build
cd build

cmake ..
cmake --build .

cp -r ../data .
```

Sau khi build:

```text
build/
├── client
├── server
└── data/
```

---

## 12. Chạy chương trình

### Chạy Server

```bash
cd build
./server
```

Kết quả:

```text
[*] Server listening on port 54321...
```

---

### Chạy Client

```bash
cd build
./client
```

Nhập thư mục:

```text
Enter folder path:
/home/user/data
```

Ví dụ chọn:

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

## 13. Ví dụ đầu ra

### Client

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

[+] Done
```

### Server

```text
[*] Server listening on port 54321...

[+] Client connected

[OK] Received successfully: CMakeLists.txt

[OK] Received successfully: test_100mb.bin

[OK] Received successfully: test_1gb.bin

[*] Client disconnected
```

---

## 14. Tạo file test

Tạo file 100MB:

```bash
dd if=/dev/zero of=test_100mb.bin bs=1M count=100 status=progress
```

Tạo file 1GB:

```bash
dd if=/dev/zero of=test_1gb.bin bs=1M count=1024 status=progress
```

Kiểm tra:

```bash
ls -lh
```

---

## 15. Ưu điểm

* Một TCP connection cho nhiều file.
* Resume khi mất kết nối.
* Chỉ gửi phần dữ liệu còn thiếu.
* Tiết kiệm băng thông.
* Hỗ trợ file dung lượng lớn.
* Kiểm tra toàn vẹn dữ liệu bằng SHA256.
* Dễ mở rộng và bảo trì.

---

## 16. Hướng phát triển

* Retry khi ACK = ER.
* Mã hóa AES.
* Multi-thread Server.
* Nén dữ liệu trước khi truyền.
* Thanh tiến trình truyền file.
* GUI Client.
* Truyền thư mục đệ quy.
* Xác thực Client/Server.

---

## 17. Kết luận

Project hiện thực hệ thống truyền file dựa trên:

```text
TCP + Resume + SHA256 + Multi-file + Single Connection
```

Hệ thống cho phép truyền nhiều file trong một phiên kết nối, hỗ trợ Resume khi mất kết nối và đảm bảo tính toàn vẹn dữ liệu thông qua SHA256.
