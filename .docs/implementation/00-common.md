# 00 — Common (EventBus · Logger · Config · Types)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — xem [README.md](README.md). *Giàn giáo + gợi ý, không phải lời giải.*
>
> **Phong cách mục 5:** mỗi đơn vị theo 3 lớp — **Bản chất** (bài toán & hướng giải) → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
>
> Nền: [../development/app_layer.md](../development/app_layer.md) §2 · [../knowledge/threading_eventbus.md](../knowledge/threading_eventbus.md) · [../development/coding_guide.md](../development/coding_guide.md)

---

## 1. 🎯 Mục tiêu & vị trí

Dựng tầng nền mọi component khác dựa vào: kiểu dữ liệu chung, logger, đọc config, và **EventBus** (trái tim mô hình đa luồng).

**File sẽ tạo:**
```
common/Types.hpp     # enum, struct sự kiện, Result<T>, alias
common/EventBus.hpp  # hàng đợi thread-safe, 1 consumer
common/Logger.hpp    # wrapper mỏng quanh spdlog
common/Config.hpp    # đọc config.json → struct
```

**Phụ thuộc:** spdlog, nlohmann/json (header-only). Không phụ thuộc gì trong project (đây là đáy).

---

## 2. 📜 Hợp đồng

Trích [../development/app_layer.md](../development/app_layer.md) §2:

```cpp
// Tập sự kiện ĐÓNG — thêm loại mới thì std::visit báo thiếu nhánh
using Event = std::variant<
    ButtonEvent, RecordingComplete, RecordingTimeout,
    SttResult, LlmResult, NetworkError, PlaybackComplete, TtsFailed>;

class EventBus {
public:
    void push(Event e);                       // gọi từ thread bất kỳ
    std::optional<Event> pop(int timeoutMs);  // CHỈ main thread
};
```

**Ràng buộc:** `push` thread-safe; `pop` có timeout (để main loop đập nhịp LVGL — [threading_eventbus.md §4](../knowledge/threading_eventbus.md)); dữ liệu lớn (PCM) **move** vào Event, không copy.

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Vì sao Event là <code>std::variant</code> chứ không phải class cha + kế thừa?</summary>

> Tập đóng: compiler bắt lỗi khi `std::visit` thiếu nhánh sau khi thêm event mới; không cấp phát động; không `dynamic_cast`. Kế thừa thì ngược lại tất cả. ([app_layer.md §2](../development/app_layer.md))
</details>

<details><summary><b>Q2.</b> Hàng đợi có cần giới hạn kích thước (bounded) không?</summary>

> Ở dự án này **không**: event sinh ra chậm hơn nhiều so với tốc độ main loop tiêu thụ (nút bấm, kết quả mạng — vài cái/giây). Bounded queue thêm phức tạp (chặn producer? bỏ event?) mà không giải quyết vấn đề có thật → over-engineering (Rule §18). *Nhưng phải lý giải được* vì sao bỏ qua, đừng bỏ qua vô thức.
</details>

<details><summary><b>Q3.</b> <code>Result&lt;T&gt;</code> hiện thực thế nào ở C++17?</summary>

> `std::variant<T, Error>` (C++23 mới có `std::expected`). Buộc caller xử lý cả hai nhánh, không ẩn control-flow. ([coding_guide.md §5](../development/coding_guide.md))
</details>

<details><summary><b>Q4.</b> Logger: tự viết hay bọc spdlog? Async hay sync?</summary>

> Bọc mỏng spdlog (Decision #11) — chỉ thêm hàm `init()` set pattern/level từ Config. Async logger của spdlog tốt nhưng *đừng bật vội*; sync đủ, và tránh log trong hot loop quan trọng hơn (xem Cạm bẫy). Wrapper mỏng để sau đổi backend không phải sửa khắp nơi.
</details>

**⚖️ Bạn tự chốt:** schema `config.json` (những trường nào), log level mặc định (gợi ý `info`).

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | `Types.hpp`: enum + struct event + `Result<T>` | compile sạch |
| 2 | `EventBus.hpp`: push/pop với mutex+cv | test đơn luồng pass |
| 3 | test đa luồng: N producer push, 1 consumer pop hết | không mất event, không deadlock |
| 4 | `Logger.hpp`: `init()` + macro/log helper | log ra stdout đúng format |
| 5 | `Config.hpp`: parse `config.json` → struct, lỗi → `Result` | thiếu file/sai JSON không crash |

---

## 5. 🧩 Khung code tự điền

### 5.1 `common/Types.hpp` — *phần lớn là contract, có thể đủ*
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <variant>

namespace bbb {

using PcmBuffer = std::vector<int16_t>;   // S16_LE mono

enum class State { Init, Idle, Listening, Processing, Speaking, Error };
enum class Service { Stt, Llm };

struct Error { int code; std::string message; };
template <class T> using Result = std::variant<T, Error>;

// ---- GPIO (đã dùng ở 01) ----
enum class ButtonId { Ptt, VolUp, VolDown };
enum class Edge { Press, Release };

// ---- Sự kiện trên bus ----
struct ButtonEvent       { ButtonId id; Edge edge; };
struct RecordingComplete { PcmBuffer pcm; };
struct RecordingTimeout  {};
struct SttResult         { std::string text; };
struct LlmResult         { std::string reply; };
struct NetworkError      { Service svc; std::string msg; };
struct PlaybackComplete  {};
struct TtsFailed         { std::string msg; };

} // namespace bbb
```
> TODO(you): thêm `AudioFormat`, `GpioLineSpec`/`GpioPinMap` (từ 01), `LlmConfig/SttConfig` khi tới các guide sau. Giữ Types.hpp là *nơi duy nhất* khai báo các kiểu dùng chung.

---

### 5.2 `common/EventBus.hpp`

**Bản chất.** Đây là **producer–consumer một chiều**: nhiều thread *sản xuất* event, đúng một thread (main) *tiêu thụ*. Hai yêu cầu cốt lõi: (1) **đồng bộ** — bảo vệ hàng đợi bằng mutex để không hỏng dữ liệu; (2) **chờ có điều kiện** — consumer không được *busy-poll* hàng đợi (ngốn CPU), mà ngủ trên một condition variable tới khi có event *hoặc* hết timeout. Timeout là bắt buộc vì main loop còn phải nhịp `lv_timer_handler()`. Cạm bẫy kinh điển: **spurious wakeup** — cv có thể đánh thức vô cớ, nên *luôn* chờ kèm predicate.

**API toolbox** (C++ std):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `std::mutex` + `std::lock_guard`/`std::unique_lock` | Khoá vùng truy cập hàng đợi | `wait_for` cần `unique_lock`, không phải `lock_guard` |
| `std::condition_variable::wait_for(lk, dur, pred)` | Ngủ tới khi `pred()` true hoặc hết `dur` | **luôn dùng `pred`** để chống spurious wakeup; trả `false` nếu hết giờ mà pred vẫn false |
| `std::condition_variable::notify_one()` | Đánh thức 1 consumer | nên gọi *sau* khi nhả lock để tránh đánh thức rồi lại chặn |
| `std::queue<Event>` + `std::optional<Event>` | Hàng đợi + "có/không có" giá trị trả về | `front()` rồi `pop()`; nhớ `std::move(front)` |

**Pseudo:**
```cpp
void push(Event e) {
    { std::lock_guard lk(m_); q_.push(std::move(e)); }   // move, đừng copy PCM
    cv_.notify_one();
}
std::optional<Event> pop(int timeoutMs) {
    std::unique_lock lk(m_);
    if (!cv_.wait_for(lk, ms(timeoutMs), [&]{ return !q_.empty(); })) return std::nullopt;
    Event e = std::move(q_.front()); q_.pop(); return e;
}
```
> **TODO(you):** vì sao predicate trong `wait_for` thay vì `wait_for` trần? (spurious wakeup). Đây là chỗ Claude soi.

---

### 5.3 `common/Logger.hpp`

**Bản chất.** **Bọc mỏng để cô lập phụ thuộc** — phần còn lại của app chỉ gọi `spdlog::info/...` (hoặc macro của bạn), còn việc *cấu hình* (level, pattern, sau này async/sink) gom vào một `init()`. Đổi backend log về sau = sửa một chỗ. Không thêm logic, chỉ thêm điểm cấu hình.

**API toolbox** (spdlog):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `spdlog::level::from_str("info")` | Đổi chuỗi config → enum level | chuỗi sai → trả `off`, nên validate |
| `spdlog::set_level(lvl)` | Đặt ngưỡng log toàn cục | dưới ngưỡng bị bỏ |
| `spdlog::set_pattern("...")` | Định dạng dòng log | đặt 1 lần ở `init` |
| `spdlog::info/warn/error(...)` | Ghi log (dùng trực tiếp khắp app) | tránh trong hot loop (xem §6) |

**Pseudo:**
```cpp
struct Logger {
    static void init(const std::string& level) {
        spdlog::set_level(spdlog::level::from_str(level));   // TODO(you): validate chuỗi
        spdlog::set_pattern(/* TODO(you): pattern có timestamp + level */);
    }
};
```

---

### 5.4 `common/Config.hpp`

**Bản chất.** Biến **văn bản không tin cậy** (file JSON người dùng sửa) thành **struct đã kiểm chứng**. Hai nguyên tắc: (1) lỗi (thiếu file, JSON hỏng, thiếu trường) trả `Result<Error>` — **không crash, không throw qua biên**; (2) phân biệt trường *bắt buộc* (thiếu = lỗi) và *tuỳ chọn* (thiếu = giá trị mặc định). Đây là một biên hệ thống — mọi giá trị vào đây phải coi như có thể sai.

**API toolbox** (nlohmann/json):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `nlohmann::json j; f >> j;` | Parse stream → cây JSON | ném `json::parse_error` khi hỏng → bọc try/catch |
| `j.at("key").get<T>()` | Lấy trường **bắt buộc** | ném nếu thiếu key → đúng cho trường bắt buộc |
| `j.value("key", default)` | Lấy trường **tuỳ chọn** kèm mặc định | không ném; dùng cho field optional |

**Pseudo:**
```cpp
inline Result<AppConfig> loadConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f) return Error{1, "config not found: " + path};
    try {
        nlohmann::json j; f >> j;
        AppConfig c;
        // c.llmHost = j.at("llm").at("host").get<std::string>();   // bắt buộc
        // c.connectMs = j.value("connect_ms", 3000);               // tuỳ chọn
        return c;
    } catch (const std::exception& e) {
        return Error{2, std::string("config parse: ") + e.what()};
    }
}
```
> **TODO(you):** chốt schema `config.json` (trường nào bắt buộc/tuỳ chọn): host/port/path cho LLM+STT, `GpioPinMap`, audio device, `debounceMs`, `maxRecordSeconds`. Trường nào thiếu thì **fail rõ ràng** thay vì chạy với giá trị rác.

---

### 5.5 Khung test EventBus
```cpp
TEST_CASE("EventBus: nhiều producer, một consumer, không mất event") {
    EventBus bus;
    constexpr int N = 1000;
    // TODO(you): spawn vài thread cùng push N/k event;
    //            main pop tới khi đủ N; assert đếm đúng, không deadlock.
}
```

---

## 6. ⚠️ Cạm bẫy

- **`pop` không có timeout** → main loop kẹt, LVGL đứng. Luôn timed-wait.
- **wait_for không dùng predicate** → spurious wakeup trả nhầm khi hàng rỗng.
- **Copy PcmBuffer vào Event** → tốn 480KB/lần. Phải `std::move`.
- **Log trong hot loop** (audio/redraw) → gây xrun/giật. Log ở biên thôi ([coding_guide.md §8](../development/coding_guide.md)).
- **Thêm event mới mà quên nhánh visit** → để compiler bắt bằng cách *không* có `default:` trong visit.
- **Config throw qua biên** → app chết vì JSON hỏng. Bọc try/catch → `Result`.

---

## 7. ✅ Checkpoint review

**Xong khi:** test đa luồng EventBus pass (không mất event, không deadlock, chạy nhiều lần ổn định); Config đọc/`Result` lỗi đúng; Logger ra format mong muốn.

**Đưa Claude review:** `EventBus.hpp` + test đa luồng. Hỏi đích danh:
1. *"`pop` của tôi có đúng với spurious wakeup + timeout không?"*
2. *"Có chỗ nào copy Event/PcmBuffer thay vì move không?"*
3. *"`std::visit` của tôi (ở FSM sau này) có nhánh nào thiếu — nên thêm/bỏ `default:` thế nào để compiler bắt?"*
