# 08 — StateMachine (FSM 7 trạng thái)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
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

### 5.2 Thuật toán `transitionTo` (validate)
```
static const map<State, set<State>> kAllowed = { ...theo bảng CLAUDE.md §8... }
transitionTo(to):
    nếu to ∉ kAllowed[state_]:
        log error("transition trái phép {}->{}", state_, to); return
    log info("FSM {} -> {}", state_, to)
    state_ = to
    ui.showState(to)            // exit/entry action gắn ở đây nếu cần
```

### 5.3 Mẫu vài `onEvent`
```cpp
void onEvent(const ButtonEvent& e) {
    if (state_==State::Idle && e.id==ButtonId::Ptt && e.edge==Edge::Press) {
        transitionTo(State::Listening); audio.start();
    } else if (state_==State::Speaking && e.id==ButtonId::Ptt && e.edge==Edge::Press) {
        audio.stopPlayback(); transitionTo(State::Idle);     // ngắt
    } else if (state_==State::Error && e.id==ButtonId::Ptt && e.edge==Edge::Press) {
        transitionTo(State::Idle);                            // retry
    }
    // Vol± KHÔNG đổi state → ButtonController xử lý, không tới đây
}
void onEvent(const RecordingComplete& e) {
    if (state_!=State::Processing) return;       // phòng event lạc
    // TODO(you): giao e.pcm cho net worker (STT). Chú ý move buffer.
}
void onEvent(const NetworkError& e) {
    // TODO(you): showError theo e.svc (Stt vs Llm) rồi transitionTo(Error)
}
```
> TODO(you): điền hết các onEvent theo bảng §8. Mỗi onEvent **kiểm `state_` hiện tại** trước khi hành động (event có thể tới khi đã đổi state). Đây là chỗ Claude soi: event lạc + nhánh thiếu.

### 5.4 Khung test (fake middleware)
```cpp
TEST_CASE("Luồng hạnh phúc INIT→IDLE→LISTENING→PROCESSING→SPEAKING→IDLE") {
    // dựng FSM với fake audio/net/tts ghi nhận lời gọi
    // bơm chuỗi event, assert state() sau mỗi bước
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
