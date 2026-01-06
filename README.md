# ChatProject-IT4062

Dự án này là **khung TCP client/server** cho đồ án chat (IT4062). README này mô tả kiến trúc, protocol và checklist rubric để cả nhóm theo dõi tiến độ và phát triển tính năng tiếp theo.

## Checklist (Rubric)
- [x] Xử lý truyền dòng: 1 điểm
- [x] Cài đặt cơ chế vào/ra socket trên server: 2 điểm
- [x] Đăng ký và quản lý tài khoản: 2 điểm
- [x] Đăng nhập và quản lý phiên: 2 điểm
- [x] Gửi lời mời kết bạn: 1 điểm
- [x] Chấp nhận/Từ chối lời mời kết bạn: 1 điểm
- [x] Hủy kết bạn: 1 điểm
- [x] Lấy danh sách bạn bè và trạng thái: 1 điểm
- [x] Gửi nhận tin nhắn giữa 2 người dùng: 1 điểm
- [ ] Ngắt kết nối: 1 điểm
- [x] Tạo nhóm chat: 1 điểm
- [x] Thêm người dùng khác vào nhóm chat: 1 điểm
- [ ] Xóa người dùng ra khỏi nhóm chat: 1 điểm
- [ ] Rời nhóm chat: 1 điểm
- [ ] Gửi nhận thông điệp trong nhóm chat: 1 điểm
- [x] Gửi tin nhắn offline: 1 điểm
- [ ] Ghi log hoạt động: 1 điểm

Ghi chú: Ai làm xong mục nào thì tick `[x]` mục đó.

---

## Quickstart (Ubuntu/WSL)

### 1) Clone (khuyến nghị)
```bash
git clone https://github.com/deadnah2/IT4062-TCP-ChatApp-20251.git
cd IT4062-TCP-ChatApp-20251
```

### 2) Cài dependencies
```bash
sudo apt update
sudo apt install -y build-essential python3
```

### 3) Build
```bash
make clean
make
```

### 4) Run
**Terminal 1 (server):**
```bash
# Usage: ./build/server <port> [session_timeout_seconds]
./build/server 8888
# Ví dụ test timeout nhanh (2s):
# ./build/server 8888 2
```

**Terminal 2 (client):**
```bash
# Usage: ./build/client <server_ip> <port>
./build/client 127.0.0.1 8888
```

### 5) Test tự động (khuyến nghị)
Bộ test tích hợp cover đầy đủ 4 phần base (framing, server IO, accounts, sessions):
```bash
make clean && make
python3 tests/itest.py
# hoặc
./tests/run.sh
```

Test hiện cover:
- Framing: gửi theo từng byte, nhiều dòng trong 1 send, dòng quá dài bị ngắt
- Accounts: register hợp lệ/duplicate/invalid
- Sessions: login/logout/whoami, chặn multi-login, auto cleanup khi disconnect, timeout
- Concurrency: nhiều client PING song song

---

## Cấu trúc thư mục

- `common/`
  - `framing.c`, `framing.h`: tách TCP stream thành từng dòng kết thúc bằng `\r\n`
  - `protocol.c`, `protocol.h`: parse/gửi response dạng line-based
- `server/`
  - `server.c`: listen/accept + thread-per-connection, cleanup session khi client disconnect
  - `handlers.c`, `handlers.h`: router theo `VERB` (PING/REGISTER/LOGIN/LOGOUT/WHOAMI)
  - `accounts.c`, `accounts.h`: file DB tài khoản (`data/users.db`)
  - `sessions.c`, `sessions.h`: quản lý session token + timeout
- `client/`
  - `client.c`: menu test + chế độ `Raw send`
- `tests/`
  - `itest.py`: integration tests (khuyến nghị chạy trước khi merge)
  - `run.sh`: wrapper chạy build + test
- `data/`
  - `users.db`: được tạo tự động khi server chạy (nếu chưa có)
- `Makefile`: build ra `build/server` và `build/client`

---

## Protocol (line-based)

Tất cả message là **1 dòng** kết thúc bằng `\r\n`.

### Request
`<VERB> <REQ_ID> <k=v k=v ...>`

- `VERB`: chữ in hoa, ví dụ `REGISTER`, `LOGIN`...
- `REQ_ID`: chuỗi không có dấu cách (client tự sinh để match response)
- payload: các cặp `key=value` cách nhau bởi 1 dấu cách (value **không có** dấu cách)

### Response
- OK:  `OK <REQ_ID> <k=v k=v ...>`
- ERR: `ERR <REQ_ID> <CODE> <MESSAGE>`

### Commands đã có
- `PING <rid>` -> `OK <rid> pong=1`
- `REGISTER <rid> username=... password=... email=...`
- `LOGIN <rid> username=... password=...`
- `LOGOUT <rid> token=...`
- `WHOAMI <rid> token=...`

### Friend Commands
- `FRIEND_INVITE <rid> token=... username=...` -> Gửi lời mời kết bạn
- `FRIEND_ACCEPT <rid> token=... username=...` -> Chấp nhận lời mời
- `FRIEND_REJECT <rid> token=... username=...` -> Từ chối lời mời
- `FRIEND_PENDING <rid> token=...` -> Danh sách lời mời đang chờ
- `FRIEND_LIST <rid> token=...` -> Danh sách bạn bè + online status
- `FRIEND_DELETE <rid> token=... username=...` -> Hủy kết bạn

### Private Message Commands (Real-time Chat)
- `PM_CHAT_START <rid> token=... with=<username>` -> Vào chế độ chat với user (bật real-time push)
- `PM_CHAT_END <rid> token=...` -> Thoát chế độ chat
- `PM_SEND <rid> token=... to=<username> content=<base64>` -> Gửi tin nhắn (content phải Base64 encoded)
- `PM_HISTORY <rid> token=... with=<username> [limit=50]` -> Lấy lịch sử chat
- `PM_CONVERSATIONS <rid> token=...` -> Danh sách các cuộc trò chuyện

**Server Push** (khi user đang ở chế độ chat):
- `PUSH PM from=<username> content=<base64> msg_id=<id> ts=<timestamp>` -> Tin nhắn real-time từ server

### Error codes
- `400`: thiếu field / sai format
- `401`: sai user/pass hoặc token không hợp lệ
- `409`: username tồn tại hoặc user đã login nơi khác
- `422`: field không hợp lệ (username/email/password)
- `500`: lỗi server

---

## Truyền dòng (framing)

TCP là **stream**, vì vậy 1 lần `recv()` có thể nhận:
- chỉ *một phần* của 1 message, hoặc
- *nhiều message* dính liền nhau.

Dự án dùng delimiter `\r\n` để tách message theo dòng. Module `common/framing.*` sẽ:
- buffer dữ liệu vào bộ nhớ,
- tìm `\r\n`,
- pop ra **1 dòng hoàn chỉnh** (không gồm `\r\n`).

Giới hạn bảo vệ: nếu buffer vượt ~64KB mà chưa gặp `\r\n` thì coi là dòng quá dài và server sẽ ngắt kết nối.

Test framing đã có trong `tests/itest.py` (gửi theo từng byte, nhiều dòng trong một lần gửi, và dòng quá dài).\n---\n
## Tài khoản (accounts)

### File DB
`data/users.db`, mỗi dòng:
`id|username|salt|hash|email|active`

- `salt`: chuỗi hex ngẫu nhiên
- `hash`: hash đơn giản từ `salt:password` (mục tiêu: **không lưu plaintext**)
- `active`: 1/0

Lưu ý: hash hiện tại là hash đơn giản (không phải bcrypt/argon2). Đủ cho đồ án môn học, **không dùng cho production**.

### Validate
- `username`: 3..32 ký tự, chỉ `[a-zA-Z0-9_]`
- `password`: 6..64 ký tự, không chứa khoảng trắng
- `email`: check cơ bản có `@` và `.` sau `@`, không có khoảng trắng

Tất cả thao tác đọc/ghi file account có mutex để tránh race giữa các thread.

---

## Session (đăng nhập/phiên)

- Token dài `32` ký tự (chữ/số)
- Session lưu **in-memory** (không lưu ra file)
- Timeout mặc định: `3600s` (1 giờ) tính theo `last_activity`
- Chính sách: 1 user chỉ được login ở **một nơi** (chặn login trùng)
- Khi client disconnect, server auto xóa session theo socket

---

## Raw send là gì?

`Raw send` cho phép bạn gõ **nguyên 1 dòng request** theo protocol và gửi thẳng lên server (client tự thêm `\r\n`). Dùng để debug nhanh khi bạn vừa thêm verb mới mà chưa làm menu.

Ví dụ:
- `PING 1`
- `REGISTER 2 username=u1 password=pass1234 email=a@b.com`
- `LOGIN 3 username=u1 password=pass1234`
- `WHOAMI 4 token=<token>`

---

## Gợi ý mở rộng cho các tính năng chat

Mẫu triển khai một verb mới:
1) Thêm `VERB` trong `server/handlers.c` (parse payload `k=v`)
2) Nếu verb cần auth: bắt buộc `token=...` và gọi `sessions_validate()`
3) Tách logic theo module mới trong `server/` (ví dụ `friends.c`, `groups.c`, `messages.c`)
4) Cập nhật client menu hoặc dùng `Raw send` để test

Gợi ý chia module theo rubric:
- `friends.*`: invite/accept/deny/unfriend + list bạn + online status
- `pm.*`: nhắn tin 1-1 + offline message
- `groups.*`: create/add/remove/leave + group message + history
- `log.*`: ghi log hoạt động

---

## Giới hạn hiện tại

- Protocol `k=v` chưa hỗ trợ value có khoảng trắng (nếu cần, cân nhắc JSON hoặc length-prefix)
- Password hashing chỉ mang tính học tập
- Session chưa persist (restart server là mất session)
