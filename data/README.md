# TCP File Transfer with SHA256, Chunking, Resume and Out-of-order Chunk Handling

## 1. Giới thiệu

Đây là project mô phỏng hệ thống truyền file giữa **Client** và **Server** sử dụng giao thức **TCP Socket**.

Project hỗ trợ các chức năng chính:

- Truyền file từ Client sang Server.
- Chia file thành nhiều chunk nhỏ 64KB.
- Kiểm tra toàn vẹn dữ liệu bằng SHA256.
- Kiểm tra SHA256 cho từng chunk.
- Kiểm tra SHA256 cho toàn bộ file sau khi nhận xong.
- Hỗ trợ resume khi kết nối bị gián đoạn.
- Hỗ trợ nhận chunk không theo thứ tự.
- Lưu trạng thái chunk đã nhận bằng file `.state`.

---

## 2. Công nghệ sử dụng

- C++17
- TCP Socket
- OpenSSL SHA256
- CMake
- Linux / Ubuntu

---

## 3. Cấu trúc project

```text
socket-transfer-file/
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
├── CMakeLists.txt
└── README.md
```

---

## 4. Ý tưởng chính

Hệ thống truyền file theo mô hình Client-Server.

Client đọc file, tính SHA256 toàn file, chia file thành các chunk nhỏ. Mỗi chunk cũng được tính SHA256 riêng.

Server nhận từng chunk, kiểm tra SHA256 của chunk, sau đó ghi chunk vào đúng vị trí trong file đích bằng `seekp(offset)`.

Sau khi nhận đủ tất cả chunk, Server tính SHA256 toàn bộ file đã nhận và so sánh với SHA256 gốc do Client gửi.

---

## 5. Pipeline Client-Server

```text
CLIENT                                                SERVER
  |                                                     |
  | 1. Chọn file cần gửi                                |
  |                                                     |
  | 2. Tính SHA256 toàn file                            |
  |                                                     |
  | 3. Chia file thành các chunk 64KB                   |
  |                                                     |
  | 4. Kết nối TCP                                      |
  |---------------------------------------------------->|
  |                                                     |
  | 5. Gửi FileHeader                                   |
  |    filename, filesize, total_chunks, file_sha256    |
  |---------------------------------------------------->|
  |                                                     |
  |                         6. Kiểm tra file nhận dở    |
  |                            received_<filename>      |
  |                            .state bitmap            |
  |                                                     |
  |                         7. Tìm chunk còn thiếu      |
  |                                                     |
  | 8. Nhận missing chunk list                          |
  |<----------------------------------------------------|
  |                                                     |
  | 9. Gửi từng chunk còn thiếu                         |
  |    ChunkHeader + ChunkData                          |
  |    chunk_index, offset, size, chunk_sha256           |
  |---------------------------------------------------->|
  |                                                     |
  |                         10. Tính SHA256 chunk       |
  |                                                     |
  |                         11. So sánh SHA chunk       |
  |                                                     |
  |                         12. seekp(offset)           |
  |                             ghi đúng vị trí chunk   |
  |                                                     |
  |                         13. Cập nhật .state         |
  |                                                     |
  | 14. Server gửi OK / ER cho chunk                    |
  |<----------------------------------------------------|
  |                                                     |
  | 15. Lặp lại đến khi hết chunk thiếu                 |
  |---------------------------------------------------->|
  |                                                     |
  |                         16. Đủ tất cả chunk         |
  |                                                     |
  |                         17. Tính SHA256 toàn file   |
  |                                                     |
  |                         18. So sánh SHA toàn file   |
  |                                                     |
  | 19. Server gửi kết quả cuối                         |
  |<----------------------------------------------------|
  |                                                     |
  |        OK = file đúng 100%                           |
  |        ER = file lỗi                                 |
  |                                                     |
```

---

## 6. Giao thức truyền dữ liệu

### 6.1. FileHeader

Client gửi metadata của file trước khi gửi chunk.

```cpp
struct FileHeader {
    char filename[256];
    uint64_t file_size;
    uint64_t chunk_size;
    uint64_t total_chunks;
    char file_sha256[65];
};
```

Ý nghĩa:

| Trường | Ý nghĩa |
|---|---|
| `filename` | Tên file |
| `file_size` | Kích thước toàn file |
| `chunk_size` | Kích thước mỗi chunk |
| `total_chunks` | Tổng số chunk |
| `file_sha256` | SHA256 toàn file |

---

### 6.2. ChunkHeader

Mỗi chunk được gửi kèm header riêng.

```cpp
struct ChunkHeader {
    uint64_t chunk_index;
    uint64_t offset;
    uint32_t data_size;
    char chunk_sha256[65];
};
```

Ý nghĩa:

| Trường | Ý nghĩa |
|---|---|
| `chunk_index` | Số thứ tự chunk |
| `offset` | Vị trí ghi trong file |
| `data_size` | Kích thước dữ liệu chunk |
| `chunk_sha256` | SHA256 của chunk |

---

## 7. Cơ chế SHA256

Project sử dụng SHA256 ở 2 mức:

### 7.1. SHA256 từng chunk

Dùng để kiểm tra chunk có bị lỗi trong quá trình truyền không.

```text
Chunk Data
    |
    v
SHA256
    |
    v
chunk_sha256
```

Nếu SHA256 chunk sai, Server trả:

```text
ER
```

Nếu đúng, Server trả:

```text
OK
```

---

### 7.2. SHA256 toàn file

Sau khi Server nhận đủ tất cả chunk, Server tính SHA256 toàn bộ file đã nhận.

```text
received_file
    |
    v
SHA256
    |
    v
actual_file_sha256
```

Sau đó so sánh với SHA256 gốc do Client gửi:

```text
actual_file_sha256 == expected_file_sha256
```

Nếu trùng nhau, file nhận được đúng 100%.

---

## 8. Cơ chế Resume

Khi truyền file bị ngắt giữa chừng, Server không xóa dữ liệu đã nhận.

Server lưu trạng thái chunk đã nhận trong file:

```text
received_<filename>.state
```

File `.state` hoạt động như bitmap:

```text
0 = chunk chưa nhận
1 = chunk đã nhận
```

Ví dụ:

```text
1 1 1 0 0 1 0
```

Nghĩa là:

```text
chunk 0 đã nhận
chunk 1 đã nhận
chunk 2 đã nhận
chunk 3 chưa nhận
chunk 4 chưa nhận
chunk 5 đã nhận
chunk 6 chưa nhận
```

Khi Client kết nối lại, Server đọc `.state`, tìm các chunk còn thiếu và gửi danh sách đó cho Client.

Client chỉ gửi lại các chunk còn thiếu, không cần gửi lại toàn bộ file.

---

## 9. Cơ chế nhận chunk không theo thứ tự

Client có thể gửi chunk theo thứ tự ngẫu nhiên.

Ví dụ:

```text
chunk 148
chunk 1413
chunk 76
chunk 1076
chunk 19
```

Server vẫn lưu đúng vì mỗi chunk có `offset`.

Server ghi dữ liệu bằng:

```cpp
outfile.seekp(chunk_header.offset, std::ios::beg);
outfile.write(buffer.data(), chunk_header.data_size);
```

Vì vậy, dù chunk đến sai thứ tự, file cuối cùng vẫn đúng.

---

## 10. Log chương trình

Client in log khi gửi chunk:

```text
[CLIENT SEND] chunk=76 offset=4980736 size=65536 sha=c3298ea4ef2b...
[CLIENT ACK] chunk=76 status=OK
```

Server in log khi nhận chunk:

```text
[SERVER RECV] chunk=76 offset=4980736 size=65536 sha_recv=c3298ea4ef2b... sha_calc=c3298ea4ef2b...
[SERVER CHECK] chunk=76 SHA=OK
[SERVER STORE] chunk=76 stored_at_offset=4980736
```

Khi hoàn tất:

```text
[Server] Actual:   d938a08ef749d0e269ebb540ec46ebc36140a59c70734c0441dc6558c0690277
[Server] Expected: d938a08ef749d0e269ebb540ec46ebc36140a59c70734c0441dc6558c0690277
[Server] SUCCESS: file integrity OK
```

---

## 11. Cài đặt thư viện cần thiết

Trên Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev
```

---

## 12. Build project

Từ thư mục gốc project:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Sau khi build thành công sẽ có:

```text
server
client
```

---

## 13. Chạy chương trình

### Terminal 1: Chạy Server

```bash
cd build
./server
```

Kết quả:

```text
[*] Server listening on port 54321...
```

### Terminal 2: Chạy Client

```bash
cd build
./client
```

Nhập đường dẫn file cần gửi:

```text
Enter file path to send: /home/user/test_100mb.bin
```

---

## 14. Tạo file test

### File 100MB

```bash
dd if=/dev/zero of=test_100mb.bin bs=1M count=100 status=progress
```

### File 1GB

```bash
dd if=/dev/zero of=test_1gb.bin bs=1M count=1024 status=progress
```

Kiểm tra file:

```bash
ls -lh test_100mb.bin
```

---

## 15. Test Resume

Bước 1: Chạy server.

```bash
./server
```

Bước 2: Chạy client và gửi file lớn.

```bash
./client
```

Bước 3: Khi đang gửi, nhấn:

```text
Ctrl + C
```

để giả lập mất kết nối.

Bước 4: Chạy lại client và gửi đúng file đó.

Server sẽ đọc file `.state`, xác định chunk còn thiếu và yêu cầu Client gửi tiếp.

---

## 16. File Server tạo ra

Sau khi nhận file, Server tạo:

```text
received_<filename>
```

Trong lúc truyền dở, Server tạo thêm:

```text
received_<filename>.state
```

Ví dụ:

```text
received_test_100mb.bin
received_test_100mb.bin.state
```

---

## 17. Ưu điểm

- Không cần gửi lại toàn bộ file khi mất kết nối.
- Phát hiện lỗi ở từng chunk.
- Đảm bảo file cuối cùng đúng bằng SHA256 toàn file.
- Hỗ trợ chunk đến không theo thứ tự.
- Có log rõ ràng để quan sát quá trình truyền.
- Phù hợp để demo bài toán Computer Networking.

---

## 18. Hạn chế

- Chưa hỗ trợ nhiều client đồng thời bằng multi-thread.
- Chưa mã hóa dữ liệu truyền qua mạng.
- Chưa có giao diện GUI.
- Chưa giới hạn tốc độ truyền.
- Chưa xác thực Client/Server.
- Chưa hỗ trợ truyền cả thư mục.

---

## 19. Hướng phát triển

- Hỗ trợ multi-thread cho nhiều client.
- Thêm mã hóa AES.
- Thêm thanh tiến trình.
- Truyền nhiều file hoặc thư mục.
- Thêm cơ chế retry từng chunk khi SHA sai.
- Thêm xác thực bằng username/password hoặc token.

---

## 20. Kết luận

Project xây dựng hệ thống truyền file Client-Server qua TCP có cơ chế chia chunk, kiểm tra SHA256 và resume.

Nhờ sử dụng SHA256 từng chunk, hệ thống có thể phát hiện chính xác chunk bị lỗi. Nhờ SHA256 toàn file, Server xác nhận file sau khi nhận xong giống hoàn toàn file gốc.

Cơ chế `.state` giúp Server lưu lại trạng thái chunk đã nhận, từ đó hỗ trợ resume khi kết nối bị gián đoạn.

```text
Chunking + SHA256 + Resume + Out-of-order Handling
```
