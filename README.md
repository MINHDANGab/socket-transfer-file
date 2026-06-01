# TCP File Transfer with Chunk SHA256 + Whole File SHA256 + Resume

## Ý tưởng

Project này gộp 3 ý tưởng:

1. Truyền file Client-Server bằng TCP.
2. Chia file thành chunk 64KB.
3. Kiểm tra toàn vẹn:
   - SHA256 từng chunk: biết lỗi ở chunk nào.
   - SHA256 toàn file: xác nhận file sau khi nhận xong giống file gốc.
4. Resume:
   - Nếu truyền bị đứt, server giữ lại `received_<filename>`.
   - Lần sau client gửi lại cùng file, server trả về số byte đã có.
   - Client `seekg(offset)` và gửi tiếp từ vị trí đó.

## Pipeline

```text
CLIENT                                      SERVER
  |                                            |
  |--- connect TCP -------------------------->|
  |                                            |
  |--- FileHeader --------------------------->|
  |    filename, filesize,                    |
  |    chunk_size, total_chunks,              |
  |    whole_file_sha256                      |
  |                                            |
  |                  Check received_<filename> |
  |                  existing_size = offset    |
  |                                            |
  |<--- offset -------------------------------|
  |                                            |
  | seekg(offset)                              |
  |                                            |
  |--- ChunkHeader + ChunkData -------------->|
  |    chunk_index, offset,                    |
  |    data_size, chunk_sha256, data           |
  |                                            |
  |                  Check SHA256 chunk        |
  |                  Append if OK             |
  |                                            |
  |<--- OK / ER ------------------------------|
  |                                            |
  | repeat until full file                     |
  |                                            |
  |                  Check whole file SHA256   |
  |                                            |
  |<--- OK / ER ------------------------------|
```

## Build bằng CMake trên Linux / MSYS2 UCRT64

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Chạy

Terminal 1:

```bash
./server
```

Terminal 2:

```bash
./client
```

Nhập đường dẫn file cần gửi.

## Test resume

1. Chạy server.
2. Chạy client và gửi file lớn.
3. Khi đang gửi, nhấn `Ctrl+C` ở client để giả lập đứt mạng.
4. Chạy lại client và gửi đúng file đó.
5. Server sẽ trả offset, client gửi tiếp phần còn lại.

## Output

Server lưu file nhận được dưới dạng:

```text
received_<filename>
```
