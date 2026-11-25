# Event Manager

Ứng dụng quản lý sự kiện viết bằng C cho môn Lập trình mạng. Mỗi người dùng kết nối tới server TCP từ terminal để tạo sự kiện, mời người khác hoặc xin tham gia. Người đang online nhận thông báo ngay trên terminal. SQLite lưu tài khoản, sự kiện và các mục đang chờ qua những lần khởi động server.

## Cài đặt và biên dịch

Chạy các lệnh trong thư mục chứa `Makefile`. Dự án cần trình biên dịch C11, Make, pkg-config, cJSON, SQLite và OpenSSL. Bộ kiểm thử tích hợp dùng Python 3.

**macOS với Homebrew**

```sh
brew install cjson pkgconf sqlite openssl@3
make
```

**Ubuntu hoặc Windows dùng Ubuntu trên WSL 2**

```sh
sudo apt update
sudo apt install build-essential pkg-config libcjson-dev libsqlite3-dev libssl-dev python3
make
```

Sau khi build, thư mục hiện tại có hai chương trình `./server` và `./client`.

## Chạy demo với bốn terminal

Kịch bản gồm một server và ba client. An tạo buổi học nhóm, mời Bình và duyệt yêu cầu của Chi. Cuối buổi demo, An xem được cả hai người trong danh sách thành viên.

### 1. Khởi động server ở terminal 1

```sh
./server 5050 demo.db
```

Khi thấy `Event Manager TCP port 5050`, giữ terminal này mở. Server tạo `demo.db` trong thư mục hiện tại nếu tệp chưa tồn tại. Mặc định server chỉ lắng nghe tại `127.0.0.1`. Để demo qua LAN, chỉ định địa chỉ IPv4 của máy chạy server, ví dụ `./server 5050 demo.db 192.168.1.10`, rồi dùng địa chỉ đó khi chạy client. Dùng mật khẩu riêng cho demo vì kết nối TCP truyền mật khẩu dạng rõ.

### 2. Mở ba client và đăng nhập

Mở terminal 2, 3 và 4 tại cùng thư mục. Chạy lệnh sau trong **mỗi terminal** để kết nối tới server.

```sh
./client 127.0.0.1 5050
```

Sau khi client hiện danh sách lệnh, nhập từng dòng vào đúng terminal. Nếu đã tạo các tài khoản này trong `demo.db`, chỉ cần chạy dòng `LOGIN` tương ứng.

**Terminal 2 · An**

```text
REGISTER an | demo-password
LOGIN an | demo-password
```

**Terminal 3 · Bình**

```text
REGISTER binh | demo-password
LOGIN binh | demo-password
```

**Terminal 4 · Chi**

```text
REGISTER chi | demo-password
LOGIN chi | demo-password
```

### 3. An tạo sự kiện

Trong terminal 2, nhập:

```text
CREATE_EVENT 2026-11-28T19:00:00+07:00 | Thư viện | Học nhóm
```

Đọc trường `data.event_id` trong phản hồi. Các bước dưới đây giả sử mã sự kiện là `1`. Nếu server trả mã khác, thay `1` bằng mã đó trong mọi lệnh.

### 4. Bình tham gia qua lời mời

1. **An** nhập `INVITE 1 | binh`. Terminal của Bình tự hiện thông báo `INVITE_RECEIVED`.
2. **Bình** nhập `LIST_PENDING`. Mảng `invites` có lời mời từ `an` cho sự kiện vừa tạo.
3. **Bình** nhập `ACCEPT_INVITE 1`. Phản hồi có `"ok":true`.

### 5. Chi gửi yêu cầu và An kiểm tra kết quả

1. **Chi** nhập `LIST_EVENTS` để xem sự kiện, rồi nhập `JOIN_REQUEST 1`.
2. **An** nhận thông báo `JOIN_REQUEST_RECEIVED`. Nhập `LIST_PENDING` để thấy `chi` trong mảng `join_requests`.
3. **An** nhập `APPROVE_JOIN 1 | chi`, rồi nhập `LIST_PARTICIPANTS 1`.

Kết quả mong đợi trong trường `data` của phản hồi cuối:

```json
{"participants":[{"username":"binh"},{"username":"chi"}]}
```

Gõ `QUIT` trong từng client và nhấn `Ctrl+C` ở terminal server để kết thúc. Để kiểm tra dữ liệu đã lưu, chạy lại server với cùng tệp `demo.db`, mở client mới, đăng nhập bằng tài khoản An rồi nhập lại `LIST_PARTICIPANTS 1` với mã sự kiện thực tế.

## Chạy kiểm thử

```sh
make test
```

Lệnh này build chương trình, kiểm tra cách đọc khung TCP bị chia nhỏ hoặc ghép liền nhau, rồi chạy các test tích hợp. Bộ test tự mở server trên cổng riêng và dùng database tạm. Các ca tích hợp kiểm tra cả thông báo khi terminal đang chờ nhập, quyền duyệt thành viên, phân trang và gửi lại yêu cầu tạo sự kiện sau khi server khởi động lại.

## Giao thức và cách dùng CLI

Server dùng socket non-blocking và `poll()` để phục vụ nhiều kết nối. Mỗi khung TCP gồm 4 byte độ dài theo thứ tự byte mạng và tối đa 65.536 byte JSON UTF-8. Mỗi phản hồi mang ID của yêu cầu tương ứng. Thông báo có thể đến bất cứ lúc nào trong phiên.

- Gõ `HELP` trong client để xem cú pháp các lệnh. Với giá trị chứa dấu `|` hoặc khoảng trắng cần giữ nguyên, dùng mảng JSON, ví dụ `LOGIN --json ["binh"," pass | word "]`.
- Khi danh sách có `data.next_cursor`, dùng giá trị đó cho trang kế tiếp. Ví dụ, nếu cursor là `42`, nhập `LIST_EVENTS 42`, `LIST_PENDING 42` hoặc `LIST_PARTICIPANTS 1 | 42` cho đúng danh sách đang xem.
- Khi cần gửi lại lệnh tạo sau mất kết nối, thêm khóa vào cuối lệnh ngay từ lần gửi đầu, ví dụ `CREATE_EVENT 2026-12-01T09:00:00Z | Thư viện | Ôn tập | on-tap-01`. Cùng tài khoản, khóa và nội dung sẽ nhận lại cùng mã sự kiện.

Xem cấu trúc bản tin, trường dữ liệu và mã lỗi trong [đặc tả giao thức](PROTOCOL.md).
