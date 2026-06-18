# Coding Guide — Modern C++17

> Tài liệu học tập (tiếng Việt theo Rule §18). Mục tiêu: thống nhất phong cách code, **và** giải thích *tại sao* chọn từng quy ước — để bạn rèn tư duy nền tảng chứ không chỉ làm theo. Keyword/thuật ngữ giữ nguyên tiếng Anh khi rõ nghĩa hơn.

---

## 1. Triết lý

Ba nguyên tắc, xếp theo độ ưu tiên khi mâu thuẫn nhau:

1. **Correctness first** — code đúng và an toàn (không UB, không race, không leak) quan trọng hơn code ngắn.
2. **Readability over cleverness** — người đọc tiếp theo (chính bạn sau 3 tháng) phải hiểu trong 30 giây.
3. **Don't over-engineer** (Rule §18) — không thêm abstraction "phòng khi cần". Thêm khi *đã* có nhu cầu thật.

So sánh tư duy: với một thiết bị embedded chạy 1 process, ta **không** cần framework DI, không cần plugin system, không cần template metaprogramming hoa mỹ. Ta cần: RAII chặt, interface rõ, ownership minh bạch.

---

## 2. Quy ước đặt tên

| Thành phần | Quy ước | Ví dụ |
|-----------|---------|-------|
| Class / struct / enum | `PascalCase` | `AudioPipeline`, `ButtonId` |
| Hàm / method | `camelCase` | `readPeriod()`, `applyGain()` |
| Biến local / tham số | `camelCase` | `frameCount` |
| Member private | `camelCase_` (hậu tố `_`) | `buffer_`, `state_` |
| Hằng / `constexpr` | `kPascalCase` | `kMaxRecordSeconds` |
| Macro (hạn chế tối đa) | `UPPER_SNAKE` | `BBB_LOG_TRACE` |
| File | `PascalCase.hpp/.cpp` cho class | `EventBus.hpp` |
| Namespace | `lower_snake` | `bbb::hal` |

Lý do hậu tố `_` cho member: phân biệt tức thì member với local ngay trong thân hàm mà không cần `this->`.

---

## 3. RAII là mặc định, không phải lựa chọn

Mọi tài nguyên (file descriptor ALSA, `gpiod_line`, `CURL*`, vùng `mmap`) phải nằm trong một object tự giải phóng ở destructor. **Không** có `open()`/`close()` thủ công rải rác trong logic.

```cpp
// Đúng: tài nguyên gắn vòng đời object
class AlsaCapture {
public:
    explicit AlsaCapture(const AudioFormat& fmt);  // mở trong ctor (hoặc init())
    ~AlsaCapture();                                // snd_pcm_close trong dtor
    AlsaCapture(const AlsaCapture&) = delete;      // không copy fd
    AlsaCapture(AlsaCapture&&) noexcept;           // cho phép move
private:
    snd_pcm_t* pcm_ = nullptr;
};
```

So sánh: nếu quản lý `snd_pcm_t*` bằng tay, mỗi nhánh `return` lỗi giữa chừng đều phải nhớ `snd_pcm_close` → sớm muộn quên một nhánh → leak fd. RAII biến "nhớ dọn dẹp" thành việc của compiler.

**Smart pointer:**
- `std::unique_ptr<T>` — ownership đơn (mặc định). Dùng cho HAL impl phía sau interface: `std::unique_ptr<IAudioHal>`.
- `std::shared_ptr<T>` — chỉ khi *thực sự* có nhiều owner cùng sống. Trong dự án này hiếm; nếu định dùng, hỏi "ai sở hữu?" trước.
- Con trỏ thô `T*` — chỉ mang nghĩa "tham chiếu không sở hữu" (non-owning), không bao giờ `delete`.

---

## 4. Quản lý ownership & truyền dữ liệu qua thread

Đây là phần dễ sai nhất trong dự án (đa luồng + buffer audio). Quy tắc:

- Buffer PCM có **đúng một owner tại một thời điểm**. Chuyển owner bằng `std::move` vào trong Event, không giữ lại con trỏ.
- Hàm nhận tham số:
  - đọc-không-giữ → `const T&`
  - sẽ giữ/sở hữu → nhận `T` by value rồi `std::move` vào member (hoặc nhận `T&&`).

```cpp
// AudioPipeline trao quyền sở hữu buffer cho event, không copy 480KB
bus.push(RecordingComplete{ std::move(pcmBuffer_) });
// sau dòng này pcmBuffer_ rỗng — không được dùng lại
```

Liên hệ tư duy threading: xem [app_layer.md](app_layer.md) và [knowledge/threading_eventbus.md](../knowledge/threading_eventbus.md).

---

## 5. Error handling — `Result<T>`, không throw qua biên thread

Exception ổn trong một thread, nhưng **không** ném xuyên biên giới thread (worker network → main). Quy ước: hàm có thể lỗi trả `Result<T>`.

```cpp
template <class T>
using Result = std::variant<T, Error>;   // Error = { code, message }

Result<std::string> SttClient::transcribe(const PcmBuffer& pcm);

// caller:
auto r = stt.transcribe(buf);
if (auto* err = std::get_if<Error>(&r)) {
    bus.push(NetworkError{ Service::Stt, err->message });
    return;
}
const std::string& text = std::get<std::string>(r);
```

So sánh lựa chọn:
- **Exception**: gọn cho lỗi hiếm, nhưng khó kiểm soát khi vượt thread và làm control-flow ẩn.
- **Mã lỗi trả về (`int`/`errno`)**: cổ điển, nhưng dễ bị bỏ qua return value.
- **`Result<T>` (variant)**: buộc caller xử lý cả hai nhánh, không ẩn control-flow, hợp với mô hình "đẩy event lỗi lên bus". → **Chọn cái này.**
- (C++23 có `std::expected` — đẹp hơn, nhưng ta ở C++17 nên dùng `variant`.)

`noexcept`: đánh dấu cho move ctor/dtor và hàm chắc chắn không ném (giúp `std::vector` tối ưu, và là tài liệu cho người đọc).

---

## 6. const-correctness & immutability

- Mặc định mọi biến là `const` nếu không cần đổi. "Mutable là ngoại lệ phải biện minh."
- Method không đổi state → `const`.
- Tham chiếu chỉ đọc → `const T&`.

Lý do nền tảng: `const` thu hẹp không gian trạng thái mà người đọc phải theo dõi → ít bug logic, compiler bắt lỗi gán nhầm.

---

## 7. Header / include

- `#pragma once` ở đầu mọi header.
- Include cái mình dùng (IWYU); không dựa vào include gián tiếp.
- Thứ tự include: header của chính file → header dự án → thư viện bên thứ ba → STL → C headers. Mỗi nhóm cách nhau 1 dòng trống.
- Forward declaration thay cho include khi chỉ cần con trỏ/tham chiếu → giảm thời gian biên dịch và coupling.

---

## 8. Logging (spdlog)

Một logger, nhiều cấp. **Không** dùng `std::cout` trong code thật (chỉ tạm trong throwaway test).

| Level | Khi nào |
|-------|---------|
| `trace` | chi tiết theo từng frame/sự kiện, tắt ở release |
| `debug` | luồng thực thi khi dev |
| `info` | mốc trạng thái: chuyển state, kết nối server |
| `warn` | bất thường nhưng tự phục hồi (retry, timeout nhẹ) |
| `error` | lỗi cần đưa state machine sang ERROR |

```cpp
spdlog::info("FSM: {} -> {}", toString(from), toString(to));
spdlog::error("STT unreachable: {}", err.message);
```

Quy ước: log ở **biên** (vào/ra HAL, vào/ra network), không log tràn lan trong vòng lặp nóng (gây xrun audio — xem Risk Register).

---

## 9. Comment

- Comment trả lời **"tại sao"**, không phải "cái gì" (code đã nói "cái gì").
- Ghi rõ giả định phần cứng/giao thức (vd: "DC active-high theo dts", "Whisper trả `text` field").
- TODO có ngữ cảnh: `// TODO(gia): chuyển sang DRM khi lên kernel 6.x`.

Mật độ comment: bám theo code xung quanh. Không "comment cho có" trên mỗi dòng.

---

## 10. Định dạng & công cụ

- Chuẩn: **C++17**, bật `-Wall -Wextra -Wpedantic`, coi warning như lỗi ở CI nếu được.
- `.clang-format` (gốc: LLVM style, indent 4 space, cột ~100). Format trước khi commit.
- `clang-tidy` cho các check bug-prone (dangling, use-after-move).
- Trên host build: bật **ASan/UBSan** cho test — bắt use-after-free/UB sớm trước khi lên board.

---

## 11. Checklist review (dán vào PR)

- [ ] Tài nguyên đều RAII, không `new/delete` trần
- [ ] Không copy buffer lớn; dùng `std::move`
- [ ] Hàm lỗi trả `Result<T>`, không throw qua biên thread
- [ ] `const` ở đâu có thể
- [ ] Không có `lv_*` ngoài main thread (xem [app_layer.md](app_layer.md))
- [ ] Log đúng level, không log trong hot loop
- [ ] Có/đã cập nhật unit test (mock-based)
