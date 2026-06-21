# 00 — Common (EventBus · Logger · Config · Types)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — xem [README.md](README.md). *Giàn giáo + gợi ý, không phải lời giải.*
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
> TODO(you): thêm `AudioFormat`, `GpioPinMap` (từ 01), `LlmConfig/SttConfig` khi tới các guide sau. Giữ Types.hpp là *nơi duy nhất* khai báo các kiểu dùng chung.

### 5.2 `common/EventBus.hpp` — *skeleton; điền thân `pop`*
```cpp
#pragma once
#include "Types.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

namespace bbb {
using Event = std::variant<
    ButtonEvent, RecordingComplete, RecordingTimeout,
    SttResult, LlmResult, NetworkError, PlaybackComplete, TtsFailed>;

class EventBus {
public:
    void push(Event e) {
        { std::lock_guard<std::mutex> lk(m_); q_.push(std::move(e)); }
        cv_.notify_one();
    }

    std::optional<Event> pop(int timeoutMs) {
        std::unique_lock<std::mutex> lk(m_);
        // TODO(you): cv_.wait_for(lk, timeoutMs, predicate q_ không rỗng)
        //            nếu hết timeout mà vẫn rỗng → return std::nullopt
        //            ngược lại move front ra, pop, return nó
        return std::nullopt;
    }
private:
    std::queue<Event> q_;
    std::mutex m_;
    std::condition_variable cv_;
};
} // namespace bbb
```
> Gợi ý thuật toán `pop`:
> ```
> nếu !cv_.wait_for(lk, ms, [&]{ return !q_.empty(); })  → return nullopt
> Event e = std::move(q_.front()); q_.pop(); return e;
> ```
> TODO(you): vì sao dùng predicate trong wait_for thay vì wait_for trần? (gợi ý: spurious wakeup — đây là chỗ Claude soi).

### 5.3 `common/Logger.hpp` — *skeleton*
```cpp
#pragma once
#include <spdlog/spdlog.h>
namespace bbb {
struct Logger {
    static void init(const std::string& level /*, pattern */) {
        // TODO(you): spdlog::set_level từ string; set_pattern
    }
};
} // namespace bbb
// dùng trực tiếp spdlog::info/warn/error ở nơi khác
```

### 5.4 `common/Config.hpp` — *skeleton; điền parse*
```cpp
#pragma once
#include "Types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
namespace bbb {
struct AppConfig {
    std::string llmHost; int llmPort;  std::string llmPath, llmModel;
    std::string sttHost; int sttPort;  std::string sttPath, sttModel, sttLang;
    int connectMs = 3000, totalMs = 10000;
    // TODO(you): GpioPinMap, audio device name, debounceMs, maxRecordSeconds...
};

inline Result<AppConfig> loadConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f) return Error{1, "config not found: " + path};
    // TODO(you): nlohmann::json j; f >> j; map j → AppConfig;
    //            bọc try/catch parse error → return Error
    return AppConfig{};
}
} // namespace bbb
```

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

---

## 7. ✅ Checkpoint review

**Xong khi:** test đa luồng EventBus pass (không mất event, không deadlock, chạy nhiều lần ổn định); Config đọc/`Result` lỗi đúng; Logger ra format mong muốn.

**Đưa Claude review:** `EventBus.hpp` + test đa luồng. Hỏi đích danh:
1. *"`pop` của tôi có đúng với spurious wakeup + timeout không?"*
2. *"Có chỗ nào copy Event/PcmBuffer thay vì move không?"*
3. *"`std::visit` của tôi (ở FSM sau này) có nhánh nào thiếu — nên thêm/bỏ `default:` thế nào để compiler bắt?"*
