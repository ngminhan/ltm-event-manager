# Giao thức Event Manager

## Khung tin TCP

Mỗi khung gồm 4 byte số nguyên không dấu theo thứ tự byte mạng (big-endian), theo sau là đúng số byte JSON UTF-8 ghi trong header. Độ dài chỉ tính phần JSON, từ 1 đến 65.536 byte. Mỗi phần JSON chứa một object. Không cần byte kết thúc chuỗi hay ký tự xuống dòng.

Bên nhận giữ lại dữ liệu chưa đủ khung và xử lý lần lượt tất cả khung hoàn chỉnh trong bộ đệm. Đóng kết nối khi độ dài nằm ngoài giới hạn hoặc nội dung không cung cấp được ID yêu cầu hợp lệ để trả lời.

## Loại bản tin

Client gửi `request` với `id` là số nguyên dương do client cấp, duy nhất trong số yêu cầu đang chờ trên kết nối. Server gửi `response` với cùng `id`. Phản hồi thành công luôn có object `data`, có thể rỗng. Phản hồi thất bại có `error.code` và `error.message`.

Server chủ động gửi `notification` tới tài khoản liên quan đang đăng nhập. Thông báo không có `id` và có thể đến giữa các phản hồi. Mỗi dòng dưới đây là phần JSON của một khung riêng.

```jsonl
{"type":"request","id":1,"op":"LOGIN","username":"binh","password":"demo-password"}
{"type":"response","id":1,"ok":true,"data":{"username":"binh"}}
{"type":"notification","op":"INVITE_RECEIVED","event_id":42,"from":"an"}
{"type":"request","id":2,"op":"ACCEPT_INVITE","event_id":42}
{"type":"response","id":2,"ok":true,"data":{}}
{"type":"request","id":3,"op":"ACCEPT_INVITE","event_id":99}
{"type":"response","id":3,"ok":false,"error":{"code":"NOT_FOUND","message":"Account, event, or waitlist entry not found."}}
```

## Thao tác

Các trường trong bảng được đặt cùng cấp với `type`, `id` và `op` trong yêu cầu. Cột kết quả mô tả nội dung `data` khi thành công.

| `op` | Trường yêu cầu | Kết quả |
| --- | --- | --- |
| `REGISTER` | `username`, `password` | `username` của tài khoản mới |
| `LOGIN` | `username`, `password` | `username` của phiên |
| `CREATE_EVENT` | `starts_at`, `location`, `description` | `event_id` mới |
| `LIST_EVENTS` | — | Mảng `events`, mỗi phần tử có `event_id`, `owner`, `starts_at`, `location`, `description` |
| `INVITE` | `event_id`, `username` của người nhận | Object rỗng |
| `ACCEPT_INVITE` | `event_id` | Object rỗng |
| `JOIN_REQUEST` | `event_id` | Object rỗng |
| `APPROVE_JOIN` | `event_id`, `username` của người gửi yêu cầu | Object rỗng |
| `LIST_PENDING` | — | Mảng `invites` gồm các mục `event_id`, `from` và mảng `join_requests` gồm các mục `event_id`, `username` |

`id`, `event_id` và cursor là các số nguyên dương tối đa 999.999.999.999.999 để biểu diễn chính xác trong JSON. Các trường nghiệp vụ còn lại là chuỗi. `starts_at` dùng ISO 8601 có múi giờ, ví dụ `2026-11-28T19:00:00+07:00`. `REGISTER` tạo tài khoản, còn `LOGIN` gắn tài khoản với kết nối hiện tại. Các thao tác sự kiện yêu cầu đăng nhập. Sau khi ngắt kết nối, client đăng nhập lại trên kết nối mới.

## Thông báo và lỗi

- `INVITE_RECEIVED` và `JOIN_REQUEST_RECEIVED` có `event_id` và `from`. Server gửi chúng sau khi lưu thành công lời mời hoặc yêu cầu. Người nhận có thể dùng `LIST_PENDING` để xem các mục đang chờ.
- `BAD_REQUEST` cho yêu cầu thiếu trường, sai kiểu hoặc có thao tác không hỗ trợ.
- `INVALID_CREDENTIALS` cho thông tin đăng nhập sai. `AUTH_REQUIRED` cho thao tác sự kiện khi chưa đăng nhập.
- `FORBIDDEN` khi tài khoản hiện tại thiếu quyền. `NOT_FOUND` khi tài khoản, sự kiện hoặc mục đang chờ cần xử lý không tồn tại.
- `CONFLICT` khi tên tài khoản trùng hoặc trạng thái tham gia hiện tại không cho phép thao tác. `INTERNAL_ERROR` khi server gặp lỗi trong lúc xử lý.

## Phân trang, phiên và gửi lại

- `LIST_EVENTS`, `LIST_PENDING` và `LIST_PARTICIPANTS` nhận trường tùy chọn `after` là số nguyên dương. Trang đầu bỏ qua trường này. Khi còn dữ liệu, response có `data.next_cursor`. Gửi lại cùng lệnh với `after` bằng cursor vừa nhận. Các trang đọc trạng thái hiện tại, nên dữ liệu có thể thay đổi giữa hai lần đọc.
- `LIST_PARTICIPANTS` yêu cầu `event_id`. Chỉ chủ sự kiện được xem. Mảng `participants` chứa các object có trường `username` của thành viên ở trạng thái `joined`.
- `CREATE_EVENT` nhận thêm `creation_key` tùy chọn (1–128 byte). Cùng tài khoản, cùng khóa và cùng nội dung sẽ trả về cùng `event_id`, kể cả sau khi server khởi động lại. Dùng lại khóa với nội dung khác trả về `CONFLICT`. Mỗi yêu cầu bỏ qua khóa vẫn tạo một sự kiện mới.
- Mỗi kết nối chỉ đăng nhập thành công một lần. `LOGIN` trên phiên đã đăng nhập trả về `CONFLICT`. Đổi tài khoản bằng cách mở kết nối mới.
