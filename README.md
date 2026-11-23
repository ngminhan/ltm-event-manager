# Event Manager

Event Manager là dự án Lập trình mạng viết bằng C. Nhiều người dùng đăng nhập từ terminal qua TCP để tạo sự kiện, gửi lời mời và duyệt yêu cầu tham gia. Mỗi sự kiện có mã, thời điểm, địa điểm và mô tả. 

## Ứng dụng hoạt động thế nào?

Ví dụ, An tạo sự kiện học nhóm. Bình và Chi có thể tham gia theo hai cách.

- **Qua lời mời.** An mời Bình theo tên tài khoản. Bình chấp nhận lời mời của mình.
- **Qua yêu cầu.** Chi tìm sự kiện trong danh sách và gửi yêu cầu. An duyệt với quyền người tạo.

Server ghi nhận người tham gia sau khi lời mời hoặc yêu cầu được xác nhận. Người dùng online nhận thông báo trên terminal. Nếu offline, họ có thể xem mục đang chờ khi đăng nhập lại.

## Tính năng cần phát triển

- **Đăng ký và đăng nhập.** Tạo tài khoản mới và xác thực trước khi sử dụng các chức năng sự kiện.
- **Tạo và xem sự kiện.** Lưu thời điểm, địa điểm và mô tả. Hiển thị mã sự kiện để người dùng có thể chọn sự kiện muốn tham gia.
- **Gửi lời mời.** Người tạo sự kiện gửi lời mời theo tên tài khoản đã đăng ký.
- **Chấp nhận lời mời.** Người nhận xem lời mời đang chờ và chấp nhận để trở thành người tham gia.
- **Gửi yêu cầu tham gia.** Người dùng chọn một sự kiện và gửi yêu cầu đến người tạo.
- **Duyệt yêu cầu tham gia.** Người tạo xem các yêu cầu đang chờ và duyệt từng yêu cầu.
- **Thông báo và lưu trạng thái.** Client đang kết nối nhận thông báo trực tiếp. Người dùng có thể xem lại lời mời và yêu cầu thuộc quyền xử lý của mình sau khi đăng nhập.

## Kiến trúc dự kiến

| Thành phần | Trách nhiệm |
| --- | --- |
| TCP server | Duy trì phiên theo kết nối, xử lý nghiệp vụ và kiểm tra quyền. Dùng `poll()` để theo dõi socket lắng nghe và các kết nối client. |
| TCP client | Cung cấp lệnh trên terminal. Dùng `poll()` để theo dõi bàn phím và socket, cho phép nhận thông báo trong lúc chờ lệnh. |
| Giao thức ứng dụng | Mỗi khung gồm 4 byte độ dài theo thứ tự byte mạng và nội dung JSON UTF-8. Tách yêu cầu, phản hồi và thông báo, dùng mã yêu cầu để ghép phản hồi tương ứng. |
| SQLite | Lưu tài khoản, sự kiện và trạng thái tham gia. Mỗi cặp sự kiện và người dùng có tối đa một bản ghi tham gia. |

Server dùng `fcntl()` với `O_NONBLOCK` cho socket. Mỗi kết nối có bộ đệm nhận và hàng đợi gửi. Server giữ lại dữ liệu khi khung nhận chưa đủ hoặc khi `send()` mới ghi được một phần, rồi gửi tiếp khi `poll()` báo `POLLOUT`. Áp dụng giới hạn kích thước khung và hàng đợi gửi để kiểm soát bộ nhớ.

## Giao thức và biên dịch

[Đặc tả giao thức](PROTOCOL.md) mô tả cấu trúc khung TCP, các thao tác, thông báo, mã lỗi và bản tin mẫu dùng chung giữa client và server.

### Thư viện JSON

Cài cJSON và `pkg-config` trước khi chạy `make`.

- **macOS (Homebrew):** `brew install cjson pkgconf`
- **Windows (Ubuntu trên WSL 2):** `sudo apt update && sudo apt install build-essential libcjson-dev pkg-config`

`Makefile` dùng `pkg-config` để lấy đường dẫn header và cờ liên kết cho `libcjson`. Có thể kiểm tra bằng `pkg-config --cflags --libs libcjson`. Trong mã C, dùng `#include <cJSON.h>` để đọc hoặc tạo bản tin JSON.

### Biên dịch và chạy

Mở terminal macOS hoặc Ubuntu trên WSL 2, chuyển vào thư mục chứa `Makefile` rồi chạy:

```sh
make
./server
./client
```

Hai chương trình khung hiện in tên rồi thoát. Kết nối TCP sẽ được triển khai ở các task tiếp theo. Dùng `make clean` để xóa các tệp sinh ra khi biên dịch.

## Lộ trình phát triển

| Giai đoạn | Công việc | Tiêu chí hoàn thành |
| --- | --- | --- |
| 1. Nền tảng TCP | Triển khai kết nối TCP và xử lý khung tin từ hai chương trình khung. Tích hợp `poll()` và socket non-blocking ở server. | Hai client trao đổi độc lập. Server đọc đúng khi một khung bị chia qua nhiều lần nhận hoặc khi nhiều khung đến cùng lúc. |
| 2. Tài khoản và sự kiện | Tích hợp SQLite. Làm đăng ký, đăng nhập, tạo sự kiện và xem danh sách. | Client chưa đăng nhập bị từ chối khi tạo sự kiện. Tài khoản và sự kiện còn sau khi khởi động lại server. |
| 3. Lời mời và yêu cầu | Hoàn thiện luồng gửi và chấp nhận lời mời, gửi và duyệt yêu cầu. Thêm danh sách đang chờ. | Cả hai luồng tạo được trạng thái đã tham gia. Thao tác sai quyền hoặc lặp lại không tạo bản ghi trùng. |
| 4. Thông báo và kiểm thử | Thêm thông báo trực tiếp, cách phục hồi khi client ngắt giữa chừng và hướng dẫn biên dịch, chạy thử. | Client online nhận thông báo ngay. Client offline xem lại mục đang chờ khi kết nối lại. Một client đọc chậm không làm gián đoạn các client khác. |

## Bảng công việc cho 3 người

Các task có thể làm song song khi những task trong cột **Cần xong trước** đã hoàn thành.

| Mã | Công việc | Phụ trách | Cần xong trước | Kết quả cần đạt |
| --- | --- | --- | --- | --- |
| T01 | Server TCP xử lý bản tin cho nhiều client | Người 1 | — | Dùng `poll()` để quản lý kết nối, tách đúng khung nhận từng phần hoặc liền nhau và gửi tiếp qua `POLLOUT` khi ghi chưa đủ. Hai client hoạt động độc lập, kể cả khi một client đọc chậm. |
| T02 | Khởi tạo SQLite, tạo và liệt kê sự kiện | Người 2 | — | Lần chạy đầu tạo đủ bảng tài khoản, sự kiện và trạng thái tham gia. Sự kiện được cấp mã, lưu và xuất hiện trong danh sách. |
| T03 | Kết nối server và xử lý bản tin trên client | Người 3 | — | Client gửi yêu cầu, đọc phản hồi và phân biệt thông báo với phản hồi. |
| T04 | Đăng ký, đăng nhập và quản lý phiên TCP | Người 1 | T01, T02 | Client đăng ký và đăng nhập qua TCP. Server từ chối tên trùng, mật khẩu sai và thao tác sự kiện khi chưa đăng nhập. |
| T05 | Thêm lệnh tài khoản và sự kiện vào client | Người 3 | T02, T03, T04 | Chạy được luồng đăng ký, đăng nhập, tạo và xem sự kiện qua TCP. |
| T06 | Xử lý gửi và chấp nhận lời mời | Người 2 | T02, T04 | Người tạo mời được tài khoản đã đăng ký. Chỉ người nhận có thể chấp nhận. |
| T07 | Xử lý gửi và duyệt yêu cầu tham gia | Người 2 | T02, T04 | Người dùng gửi được yêu cầu. Chỉ người tạo sự kiện có thể duyệt. |
| T08 | Truy vấn lời mời và yêu cầu đang chờ | Người 2 | T06, T07 | Mỗi tài khoản xem được các mục thuộc quyền xử lý của mình. |
| T09 | Gửi thông báo đến client đang online | Người 1 | T04, T06, T07 | Server đưa thông báo vào hàng đợi gửi của đúng tài khoản sau khi lưu thành công. |
| T10 | Hoàn thiện lệnh tham gia và hiển thị thông báo trên client | Người 3 | T05, T08, T09 | Client thực hiện được cả hai luồng tham gia và nhận thông báo trong lúc chờ nhập lệnh. |
| T11 | Kiểm thử đầu cuối và viết hướng dẫn chạy demo | Người 3 | T10 | Chạy được demo với ba client. Kiểm tra người dùng offline, thao tác sai quyền, thao tác trùng và client đọc chậm. |

## Kịch bản demo

1. An, Bình và Chi đăng ký rồi đăng nhập từ ba terminal.
2. An tạo sự kiện và mời Bình. Bình nhận thông báo và chấp nhận lời mời.
3. Chi gửi yêu cầu tham gia. An nhận thông báo và duyệt yêu cầu.
4. Kiểm tra Bình và Chi đã tham gia. Ngắt một client và kiểm tra hai client còn lại vẫn thao tác được.
