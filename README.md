# ChatProject-IT4062

Ứng dụng **TCP Chat Client/Server** hoàn chỉnh cho đồ án môn học IT4062. Project hỗ trợ đăng ký/đăng nhập, kết bạn, chat 1-1 (PM) và chat nhóm (GM) với real-time push notification.

## Checklist (Rubric) — ✅ Hoàn thành 17/17 (20 điểm)

| # | Tính năng | Điểm | Module |
|---|-----------|------|--------|
| 1 | Xử lý truyền dòng (framing) | 1 | `common/framing.*` |
| 2 | Cài đặt cơ chế vào/ra socket trên server | 2 | `server/server.c`, `common/protocol.*` |
| 3 | Đăng ký và quản lý tài khoản | 2 | `server/accounts.*` |
| 4 | Đăng nhập và quản lý phiên | 2 | `server/sessions.*` |
| 5 | Gửi lời mời kết bạn | 1 | `server/friends.*` |
| 6 | Chấp nhận/Từ chối lời mời kết bạn | 1 | `server/friends.*` |
| 7 | Hủy kết bạn | 1 | `server/friends.*` |
| 8 | Lấy danh sách bạn bè và trạng thái | 1 | `server/friends.*` |
| 9 | Gửi nhận tin nhắn giữa 2 người dùng | 1 | `server/messages.*` |
| 10 | Ngắt kết nối | 1 | `server/handlers.c` |
| 11 | Tạo nhóm chat | 1 | `server/groups.*` |
| 12 | Thêm người dùng khác vào nhóm chat | 1 | `server/groups.*` |
| 13 | Xóa người dùng ra khỏi nhóm chat | 1 | `server/groups.*` |
| 14 | Rời nhóm chat | 1 | `server/groups.*` |
| 15 | Gửi nhận thông điệp trong nhóm chat | 1 | `server/group_messages.*` |
| 16 | Gửi tin nhắn offline | 1 | `server/messages.*`, `server/group_messages.*` |
| 17 | Ghi log hoạt động | 1 | `server/logger.*` |

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
Bộ test tích hợp cover đầy đủ tất cả tính năng với **30 test cases**:
```bash
make clean && make
python3 tests/itest.py
# hoặc
./tests/run.sh
```

**Test coverage (30 tests):**
- **Base (6 tests)**: framing, accounts, sessions, concurrency
- **Friend (8 tests)**: invite/accept/reject/pending/list/delete
- **Private Message (5 tests)**: PM_SEND, PM_HISTORY, PM_CONVERSATIONS, offline message, real-time push
- **Group (5 tests)**: GROUP_CREATE, GROUP_ADD, GROUP_REMOVE, GROUP_LEAVE, GROUP_LIST
- **Group Message (6 tests)**: GM_SEND, GM_HISTORY, real-time push, join/leave notifications, kick handling

---

## Cấu trúc thư mục

```
ChatProject-IT4062/
├── Makefile                    # Build server và client
├── README.md
├── build/                      # Binary output
│   ├── server
│   └── client
├── common/                     # Shared modules
│   ├── framing.c/h             # TCP stream → line-based messages
│   └── protocol.c/h            # Parse/send response dạng OK/ERR
├── server/
│   ├── server.c                # Listen/accept + thread-per-connection
│   ├── handlers.c/h            # Router theo VERB
│   ├── accounts.c/h            # User DB (data/users.db)
│   ├── sessions.c/h            # Session token + timeout + online tracking
│   ├── friends.c/h             # Invite/accept/reject/list/unfriend
│   ├── groups.c/h              # Group CRUD + membership
│   ├── messages.c/h            # Private Message (PM) chat 1-1
│   ├── group_messages.c/h      # Group Message (GM) chat nhóm
│   └── logger.c/h              # Ghi log hoạt động (data/server.log)
├── client/
│   └── client.c                # Menu-based client + Raw send mode
├── data/                       # Database files (auto-created)
│   ├── users.db                # Tài khoản: id|username|salt|hash|email|active
│   ├── friends.db              # Quan hệ bạn bè
│   ├── groups.db               # Danh sách nhóm
│   ├── group_members.db        # Thành viên nhóm
│   ├── pm/                     # Tin nhắn 1-1: {user1_id}_{user2_id}.txt
│   ├── gm/                     # Tin nhắn nhóm: {group_id}.txt
│   └── server.log              # Log hoạt động
└── tests/                      # Integration tests (Python)
    ├── itest.py                # Base tests (framing, accounts, sessions)
    ├── itest_addfriend.py      # Friend invite tests
    ├── itest_listpending_acc_rej.py  # Pending/accept/reject tests
    ├── itest_friendlist_status.py    # Friend list + online status
    ├── itest_deletefriend.py   # Unfriend tests
    ├── itest_disconnect.py     # Disconnect tests
    ├── itest_groups_members_add.py   # Group + membership tests
    ├── itest_private_message.py      # PM tests
    ├── itest_group_message.py        # GM tests
    └── run.sh                  # Wrapper chạy build + all tests
```

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
- `LOGOUT <rid> token=...` -> Hủy session, giữ TCP connection
- `WHOAMI <rid> token=...`
- `DISCONNECT <rid> [token=...]` -> Hủy session (nếu có) và đóng TCP connection

### Friend Commands
- `FRIEND_INVITE <rid> token=... username=...` -> Gửi lời mời kết bạn
- `FRIEND_ACCEPT <rid> token=... username=...` -> Chấp nhận lời mời
- `FRIEND_REJECT <rid> token=... username=...` -> Từ chối lời mời
- `FRIEND_PENDING <rid> token=...` -> Danh sách lời mời đang chờ
- `FRIEND_LIST <rid> token=...` -> Danh sách bạn bè + online status
- `FRIEND_DELETE <rid> token=... username=...` -> Hủy kết bạn

### Group Commands
- `GROUP_CREATE <rid> token=... name=<group_name>` -> Tạo nhóm (owner là người tạo)
- `GROUP_ADD <rid> token=... group_id=... username=...` -> Thêm thành viên (chỉ owner)
- `GROUP_REMOVE <rid> token=... group_id=... username=...` -> Xóa thành viên (chỉ owner)
- `GROUP_LEAVE <rid> token=... group_id=...` -> Rời nhóm
- `GROUP_LIST <rid> token=...` -> Danh sách nhóm của user

### Private Message Commands (Real-time Chat 1-1)
- `PM_CHAT_START <rid> token=... with=<username>` -> Vào chế độ chat với user (bật real-time push)
- `PM_CHAT_END <rid> token=...` -> Thoát chế độ chat
- `PM_SEND <rid> token=... to=<username> content=<base64>` -> Gửi tin nhắn (content phải Base64 encoded)
- `PM_HISTORY <rid> token=... with=<username> [limit=50]` -> Lấy lịch sử chat
- `PM_CONVERSATIONS <rid> token=...` -> Danh sách các cuộc trò chuyện

**Server Push (PM):**
- `PUSH PM from=<username> content=<base64> msg_id=<id> ts=<timestamp>` -> Tin nhắn real-time từ server

### Group Message Commands (Real-time Chat nhóm)
- `GM_CHAT_START <rid> token=... group_id=...` -> Vào chế độ chat nhóm (bật real-time push)
- `GM_CHAT_END <rid> token=...` -> Thoát chế độ chat nhóm
- `GM_SEND <rid> token=... group_id=... content=<base64>` -> Gửi tin nhắn vào nhóm
- `GM_HISTORY <rid> token=... group_id=... [limit=50]` -> Lấy lịch sử chat nhóm

**Server Push (GM):**
- `PUSH GM from=<username> group_id=<id> content=<base64> msg_id=<id> ts=<ts>` -> Tin nhắn nhóm real-time
- `PUSH GM_JOIN user=<username> group_id=<id>` -> Thông báo ai đó vào chế độ chat nhóm
- `PUSH GM_LEAVE user=<username> group_id=<id>` -> Thông báo ai đó rời chế độ chat nhóm
- `PUSH GM_KICKED group_id=<id>` -> Thông báo bạn đã bị kick khỏi nhóm

### Error codes
- `400`: thiếu field / sai format
- `401`: sai user/pass hoặc token không hợp lệ
- `409`: username tồn tại hoặc user đã login nơi khác
- `422`: field không hợp lệ (username/email/password)
- `500`: lỗi server

---

## Kiến trúc hệ thống

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                                  CLIENT                                      │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │  Menu UI  │  Chat Mode (PM/GM)  │  Receive Thread (PUSH handler)       ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                              │ TCP Socket │                                  │
└──────────────────────────────┴────────────┴──────────────────────────────────┘
                                     │
                               ┌─────┴─────┐
                               │  Network  │
                               └─────┬─────┘
                                     │
┌──────────────────────────────┬─────┴─────┬──────────────────────────────────┐
│                              │ TCP Socket│                                   │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         SERVER (Multi-threaded)                      │   │
│  │  ┌─────────┐  ┌──────────────────────────────────────────────────┐   │   │
│  │  │ Accept  │→ │  Thread per Connection (handle_client)           │   │   │
│  │  │ Loop    │  │  ┌─────────────────────────────────────────────┐ │   │   │
│  │  └─────────┘  │  │ Framing → Parse VERB → Router → Handler     │ │   │   │
│  │               │  └─────────────────────────────────────────────┘ │   │   │
│  │               └──────────────────────────────────────────────────┘   │   │
│  │                                                                      │   │
│  │  ┌─────────────────────────────────────────────────────────────────┐ │   │
│  │  │ In-Memory State                                                 │ │   │
│  │  │  • Sessions (token, user_id, socket_fd, chat_partner,           │ │   │
│  │  │              chat_group_id, last_activity)                      │ │   │
│  │  └─────────────────────────────────────────────────────────────────┘ │   │
│  │                                                                      │   │
│  │  ┌─────────────────────────────────────────────────────────────────┐ │   │
│  │  │ File-based Database (data/)                                     │ │   │
│  │  │  • users.db, friends.db, groups.db, group_members.db            │ │   │
│  │  │  • pm/{id1}_{id2}.txt, gm/{group_id}.txt                        │ │   │
│  │  │  • server.log                                                   │ │   │
│  │  └─────────────────────────────────────────────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                  SERVER                                      │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Luồng xử lý tin nhắn Real-time

**Private Message (PM):**
```
A (chat với B)          Server                    B (chat với A)
     │                    │                            │
     │ PM_SEND to=B       │                            │
     ├───────────────────>│ Save to pm/{A_B}.txt       │
     │                    │───────────────────────────>│ PUSH PM from=A
     │<───────────────────│ OK                         │
     │                    │                            │
```

**Group Message (GM):**
```
A (chat nhóm G)         Server                    B,C (cũng chat nhóm G)
     │                    │                            │
     │ GM_SEND group=G    │                            │
     ├───────────────────>│ Save to gm/{G}.txt         │
     │                    │───────────────────────────>│ PUSH GM (to B)
     │                    │───────────────────────────>│ PUSH GM (to C)
     │<───────────────────│ OK                         │
     │                    │                            │
```

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

## Logging

Server ghi log tất cả hoạt động vào `data/server.log`:
- Thời gian, user, action, kết quả
- Ví dụ: `[2025-01-15 10:30:45] user=alice action=LOGIN result=OK`
- Module: `server/logger.*`

---

## Raw send là gì?

`Raw send` cho phép bạn gõ **nguyên 1 dòng request** theo protocol và gửi thẳng lên server (client tự thêm `\r\n`). Dùng để debug nhanh khi bạn vừa thêm verb mới mà chưa làm menu.

Ví dụ:
- `PING 1`
- `REGISTER 2 username=u1 password=pass1234 email=a@b.com`
- `LOGIN 3 username=u1 password=pass1234`
- `WHOAMI 4 token=<token>`

---

## Giới hạn hiện tại (cho production)

- Protocol `k=v` chưa hỗ trợ value có khoảng trắng → dùng Base64 cho message content
- Password hashing chỉ mang tính học tập (không phải bcrypt/argon2)
- Session chưa persist (restart server là mất session)
- File DB dùng mutex, chưa tối ưu cho concurrent write cao

---

## Tác giả

Nhóm 3 - IT4062 - HUST
