# Kiến thức: Concurrency & mô hình EventBus một-consumer

> Tài liệu học tập (tiếng Việt theo Rule §18). Nền tảng lý thuyết đằng sau lựa chọn ở [development/app_layer.md](../development/app_layer.md) và CLAUDE.md §4.2.

---

## 1. Vấn đề gốc: shared mutable state

Bug đa luồng khó nhất đến từ **nhiều thread cùng đọc/ghi một dữ liệu mà không đồng bộ**. Hai biểu hiện kinh điển:

- **Race condition**: kết quả phụ thuộc thứ tự thực thi ngẫu nhiên giữa các thread.
- **Data race** (UB trong C++): hai thread truy cập cùng một biến, ít nhất một là ghi, không có đồng bộ → hành vi không xác định.

Ví dụ chí mạng trong dự án: GPIO thread gọi `lv_label_set_text()` trong khi main thread đang `lv_timer_handler()` redraw. LVGL không có khóa nội bộ → cấu trúc cây widget hỏng → crash *thỉnh thoảng*, không tái hiện đều → tốn hàng giờ debug.

---

## 2. Ba cách tiếp cận & so sánh

| Cách | Ý tưởng | Ưu | Nhược |
|------|---------|-----|-------|
| **Khóa khắp nơi** (mutex quanh mọi shared data) | Mỗi tài nguyên một mutex | Linh hoạt | Dễ deadlock, dễ quên khóa, khó suy luận đúng |
| **Callback chaining** | Thread A gọi thẳng hàm xử lý của B | Ít boilerplate | Hàm chạy trên *thread của caller* → vẫn race nếu đụng state của B |
| **Message passing, single consumer** | Thread chỉ gửi message; *một* thread xử lý tuần tự | Triệt race vào state dùng chung; dễ suy luận | Thêm độ trễ hàng đợi (không đáng kể ở đây) |

Dự án chọn cách 3. Triết lý: *"Don't communicate by sharing memory; share memory by communicating."* — biến vấn đề đồng bộ thành vấn đề xếp hàng, mà hàng đợi thì dễ làm đúng.

---

## 3. Vì sao "một consumer" là chìa khóa

Nếu chỉ **một** thread chạm vào StateMachine và LVGL, thì state đó **không còn là shared** giữa nhiều thread nữa → không cần khóa quanh nó, không thể có data race trên nó. Mọi phức tạp đồng bộ co lại còn đúng một chỗ: hàng đợi.

Hàng đợi chỉ cần:
- `std::mutex` bảo vệ thao tác push/pop.
- `std::condition_variable` để consumer ngủ khi rỗng, thức khi có việc (không busy-wait tốn CPU — quan trọng trên BBB single-core).

---

## 4. Vì sao `pop` có timeout

Main loop phải làm 2 việc xen kẽ: xử lý event **và** gọi `lv_timer_handler()` định kỳ (LVGL cần nhịp để chạy animation/refresh timer). Nếu `pop` chặn vô hạn khi hàng rỗng, UI sẽ "đứng" cho tới khi có event kế tiếp.

→ `pop(10ms)`: chờ tối đa 10ms; có event thì xử lý ngay, không thì trả `nullopt` để vòng lặp tiếp tục đập nhịp UI. Đây là mẫu **"timed wait"** kết hợp event-driven + polling nhẹ.

---

## 5. Truyền dữ liệu lớn qua hàng đợi: move, đừng copy

Buffer PCM ~480KB. Nếu copy vào event rồi copy ra → tốn CPU + RAM vô ích. Dùng **move semantics**: `std::move(buffer)` chuyển quyền sở hữu, không sao chép byte. Hệ quả ràng buộc: sau khi move, owner cũ rỗng — tuyệt đối không dùng lại (xem [development/coding_guide.md](../development/coding_guide.md) §4).

---

## 6. Bất biến cần nhớ (in ra dán bàn)

1. `lv_*` chỉ trên main thread.
2. Mỗi PcmBuffer có đúng một owner; chuyển bằng `std::move`.
3. Worker thread chỉ `push`, không gọi vào FSM/UI.
4. Không busy-wait; luôn dùng condition_variable.
