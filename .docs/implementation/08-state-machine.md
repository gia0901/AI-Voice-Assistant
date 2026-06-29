# 08 — StateMachine (FSM 7 trạng thái)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> **Phong cách mục 5:** mỗi hàm theo 3 lớp — **Bản chất** → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
>
> Nền: CLAUDE.md §8 (bảng transition) · [../development/app_layer.md](../development/app_layer.md) §3 (đây là trung tâm)

---

## 1. 🎯 Mục tiêu & vị trí

"Bộ não" điều phối: nhận Event từ bus, quyết định chuyển trạng thái, yêu cầu middleware hành động. **Không làm IO trực tiếp** → test được toàn bộ logic không cần phần cứng.

**File:** `app/StateMachine.hpp/.cpp`, `tests/app/StateMachineTest.cpp`.
**Phụ thuộc:** tất cả middleware (qua con trỏ/interface), `EventBus`, `LvglUi`. Làm **sau** khi các thành phần kia có skeleton.

---

## 2. 📜 Hợp đồng

```cpp
class StateMachine {
public:
    StateMachine(EventBus& bus, LvglUi& ui, AudioPipeline& audio,
                 NetWorkers& net, TtsEngine& tts /*, ...*/);
    void handle(const Event& ev);       // gọi từ main loop
    State state() const { return state_; }
private:
    void transitionTo(State to);        // CHỖ DUY NHẤT đổi state_
    State state_ = State::Init;
};
```
**Ràng buộc:** chạy **chỉ trên main thread**; `state_` đổi **chỉ** trong `transitionTo` (kèm validate bảng §8); không gọi ALSA/curl trực tiếp — chỉ ra lệnh cho middleware rồi chờ event.

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Dispatch event kiểu gì?</summary>

> `std::visit` trên `Event` variant, mỗi loại một overload `onEvent(const X&)`. Compiler bắt thiếu nhánh khi thêm event mới (nếu **không** để `default`). ([app_layer.md §3](../development/app_layer.md))
</details>

<details><summary><b>Q2.</b> Vì sao chỉ một chỗ đổi <code>state_</code>?</summary>

> Gom mọi kiểm tra transition hợp lệ + log + breakpoint vào `transitionTo()`. Rải `state_=...` khắp nơi → không suy luận được, dễ "lạc" trạng thái. Transition trái phép → log error, **không** đổi.
</details>

<details><summary><b>Q3.</b> Ngắt SPEAKING xử lý ở đâu (đã bỏ CANCEL)?</summary>

> Là *transition action*: `onEvent(ButtonEvent PTT Press)` khi `state_==Speaking` → `audio.stopPlayback()` rồi `transitionTo(Idle)`. Không state riêng (quyết định đã chốt khi review CLAUDE.md). ([app_layer.md §3](../development/app_layer.md))
</details>

<details><summary><b>Q4.</b> Phân biệt lỗi STT vs LLM thế nào trên UI?</summary>

> `NetworkError{svc}` mang `Service::Stt|Llm` → `ui.showError("STT unreachable")` vs `"LLM unreachable"` (CLAUDE.md §9). Đừng gộp thành "network error" chung chung.
</details>

**⚖️ Bạn tự chốt:** lưu transition table dạng gì (map/`switch`), thông điệp LCD mỗi state.

---

## 4. 🔨 Trình tự dựng (test-first, thuần logic)

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | `State` enum + `transitionTo` + bảng hợp lệ | chuyển trái phép bị chặn + log |
| 2 | `handle` = `std::visit` + các `onEvent` rỗng | compile, không thiếu nhánh |
| 3 | luồng hạnh phúc: Button→Recording→Stt→Llm→Tts→Playback | test đi đúng INIT→…→IDLE |
| 4 | nhánh lỗi: NetworkError/TtsFailed/Timeout | về ERROR/IDLE đúng |
| 5 | ngắt SPEAKING + ERROR→retry | đúng bảng §8 |

Tất cả bằng **fake bus + fake/mock middleware** trên VM.

---

## 5. 🧩 Khung code tự điền

### 5.1 `StateMachine.hpp` — *skeleton*
```cpp
#pragma once
#include "Types.hpp"
#include "EventBus.hpp"
namespace bbb {
class StateMachine {
public:
    StateMachine(/* refs: bus, ui, audio, net, tts */);
    void handle(const Event& ev) { std::visit([&](auto&& e){ onEvent(e); }, ev); }
    State state() const { return state_; }
private:
    // overloads — mỗi loại event một cái
    void onEvent(const ButtonEvent&);
    void onEvent(const RecordingComplete&);
    void onEvent(const RecordingTimeout&);
    void onEvent(const SttResult&);
    void onEvent(const LlmResult&);
    void onEvent(const NetworkError&);
    void onEvent(const PlaybackComplete&);
    void onEvent(const TtsFailed&);

    void transitionTo(State to);
    State state_ = State::Init;
    // refs tới ui/audio/net/tts
};
} // namespace bbb
```

---

### 5.2 `handle()` — dispatch bằng `std::visit`

**Bản chất.** Một FSM là **(trạng thái hiện tại × event tới) → hành động + trạng thái mới**. Phần "event tới" là kiểu *tổng* (`Event` variant) → cách type-safe nhất để rẽ nhánh là `std::visit`: compiler **bắt buộc** bạn xử lý mọi biến thể, nên khi thêm event mới mà quên nhánh thì *không build được* — với điều kiện **không** đặt `default`/catch-all nuốt mất sự an toàn đó.

**API toolbox** (C++ std):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `std::visit(visitor, variant)` | Gọi đúng overload theo kiểu thực trong variant | visitor phải xử lý *mọi* kiểu, nếu không → lỗi compile (đó là tính năng) |
| Overload set `onEvent(const X&)` | Mỗi event một hàm | **đừng** thêm `onEvent(auto&&)` catch-all → mất bắt-thiếu-nhánh |

**Pseudo:** `handle(ev) → std::visit([&](auto&& e){ onEvent(e); }, ev)` — phần lambda đã có trong skeleton; việc của bạn là viết đủ các overload.

---

### 5.3 `transitionTo()` — điểm đổi state duy nhất

**Bản chất.** Tập trung **một cửa** cho mọi đổi trạng thái để có đúng một nơi: kiểm transition hợp lệ (bảng §8), log, đặt breakpoint, chạy entry-action (cập nhật LCD). Transition trái phép = **chỉ ra lệnh sai**, nên xử lý là *từ chối + log*, không phải crash.

**API toolbox:** `std::map<State, std::set<State>>` (bảng hợp lệ) — hoặc `switch`; `spdlog` để log.

**Pseudo:**
```
static const map<State,set<State>> kAllowed = { ...theo bảng CLAUDE.md §8... }
transitionTo(to):
    to ∉ kAllowed[state_] → log error("trái phép {}->{}", state_, to); return   // KHÔNG đổi
    log info("FSM {} -> {}", state_, to)
    state_ = to
    ui.showState(to)            // entry action
```

---

### 5.4 Các `onEvent()` — hành động theo (state × event)

**Bản chất.** Mỗi handler trả lời: "ở *đúng* trạng thái này, event này nghĩa là gì?". Mấu chốt phòng thủ: **event có thể tới muộn/lạc** (vd `PlaybackComplete` về sau khi đã chuyển ERROR) → mỗi handler phải **kiểm `state_` trước khi hành động**, bỏ qua nếu không hợp ngữ cảnh. FSM **ra lệnh** cho middleware rồi **chờ event tiếp**, không tự làm IO — đó là điều giữ cho nó test được thuần logic.

**Pseudo (mẫu vài cái):**
```cpp
void onEvent(const ButtonEvent& e) {
    if (state_==Idle    && e.id==Ptt && e.edge==Press) { transitionTo(Listening); audio.start(); }
    else if (state_==Speaking && e.id==Ptt && e.edge==Press) { audio.stopPlayback(); transitionTo(Idle); } // ngắt
    else if (state_==Error    && e.id==Ptt && e.edge==Press) { transitionTo(Idle); }                        // retry
    // Vol± KHÔNG đổi state → ButtonController xử lý, không tới đây
}
void onEvent(const RecordingComplete& e) {
    if (state_!=Processing) return;            // phòng event lạc
    // TODO(you): giao std::move(e.pcm) cho net worker (STT)
}
void onEvent(const NetworkError& e) {
    // TODO(you): ui.showError theo e.svc (Stt vs Llm) rồi transitionTo(Error)
}
```
> **TODO(you):** điền hết onEvent theo bảng §8; mỗi cái **kiểm `state_`** trước. Chỗ Claude soi: event lạc + nhánh thiếu.

---

### 5.5 Khung test (fake middleware)
```cpp
TEST_CASE("Luồng hạnh phúc INIT→IDLE→LISTENING→PROCESSING→SPEAKING→IDLE") {
    // dựng FSM với fake audio/net/tts ghi nhận lời gọi; bơm chuỗi event; assert state() từng bước
    // TODO(you)
}
TEST_CASE("PROCESSING + NetworkError(Stt) → ERROR, showError STT") { /* TODO(you) */ }
TEST_CASE("Transition trái phép bị chặn, state không đổi")        { /* TODO(you) */ }
```

---

## 6. ⚠️ Cạm bẫy

- **Để `default:` trong visit** → mất khả năng compiler bắt nhánh thiếu khi thêm event.
- **Không kiểm `state_` trong onEvent** → event lạc (vd PlaybackComplete tới khi đã ERROR) gây hành động sai.
- **Đổi `state_` ngoài `transitionTo`** → mất điểm validate/log duy nhất.
- **Làm IO trong FSM** (gọi curl/ALSA trực tiếp) → mất khả năng test thuần logic.
- **Gộp lỗi STT/LLM** → người dùng không biết server nào chết (§9).

---

## 7. ✅ Checkpoint review

**Xong khi:** test phủ luồng hạnh phúc + các nhánh lỗi + transition trái phép, **pass trên VM**; không nhánh visit nào thiếu.

**Đưa Claude review:** `StateMachine.cpp` + bảng transition + test. Hỏi:
1. *"Có nhánh Event nào tôi chưa xử lý (visit thiếu) không?"*
2. *"Có transition trái phép nào lọt qua `transitionTo` không?"*
3. *"onEvent nào của tôi chưa kiểm `state_` nên có thể hành động trên event lạc?"*
