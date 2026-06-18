# App Layer — StateMachine · EventBus · LVGL UI · ButtonController

> Tài liệu học tập (tiếng Việt theo Rule §18). Đây là "bộ não" điều phối. Giải thích mô hình thread một-consumer, máy trạng thái 7 trạng thái, và vì sao LVGL chỉ chạy trên main thread. Đối chiếu CLAUDE.md §4.2, §8 và [architecture.md](../architecture.md).

---

## 1. Mô hình tổng quát: một consumer, nhiều producer

Toàn hệ thống là **1 process, nhiều thread**, giao tiếp qua **một hàng đợi thread-safe duy nhất** (`EventBus`). Main thread là *consumer duy nhất*; mọi thread khác chỉ *push* event.

```
GPIO thread ─┐
Capture ─────┤  push()
Playback ────┼────────►  EventBus (queue + mutex + condition_variable)
Net ×2 ──────┘                       │ pop(timeout)
                                     ▼
                        Main thread (consumer):
                        while(running):
                          ev = bus.pop(10ms)
                          if ev: stateMachine.handle(ev)
                          lv_timer_handler()      // vẽ UI
```

Vì sao **không** dùng callback chaining (thread A gọi thẳng hàm của thread B)? Vì đó là cái bẫy race condition: ví dụ GPIO thread gọi thẳng `lv_label_set_text()` → LVGL không thread-safe → crash ngẫu nhiên, cực khó debug (CLAUDE.md Risk Register). Hàng đợi một-consumer **tuần tự hóa** mọi sự kiện về một thread → không còn race vào UI/FSM.

Chi tiết lý thuyết: [knowledge/threading_eventbus.md](../knowledge/threading_eventbus.md).

---

## 2. EventBus

Tập event là **đóng** (closed set) qua `std::variant` — thêm event mới thì compiler nhắc mọi chỗ `visit` còn thiếu nhánh.

```cpp
// common/EventBus.hpp
struct ButtonEvent      { ButtonId id; Edge edge; };
struct RecordingComplete{ PcmBuffer pcm; };
struct RecordingTimeout {};                       // FR-8
struct SttResult        { std::string text; };
struct LlmResult        { std::string reply; };
struct NetworkError     { Service svc; std::string msg; };
struct PlaybackComplete {};
struct TtsFailed        { std::string msg; };

using Event = std::variant<
    ButtonEvent, RecordingComplete, RecordingTimeout,
    SttResult, LlmResult, NetworkError, PlaybackComplete, TtsFailed>;

class EventBus {
public:
    void push(Event e) {                       // gọi từ thread bất kỳ
        { std::lock_guard lk(m_); q_.push(std::move(e)); }
        cv_.notify_one();
    }
    std::optional<Event> pop(int timeoutMs) {  // chỉ main thread
        std::unique_lock lk(m_);
        if (!cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                          [&]{ return !q_.empty(); }))
            return std::nullopt;
        Event e = std::move(q_.front()); q_.pop();
        return e;
    }
private:
    std::queue<Event> q_;
    std::mutex m_;
    std::condition_variable cv_;
};
```

`pop` có timeout để main loop vẫn quay đều gọi `lv_timer_handler()` kể cả khi không có event (LVGL cần được "đập nhịp" định kỳ).

---

## 3. StateMachine — 7 trạng thái

Theo CLAUDE.md §8 (đã bỏ CANCEL — ngắt là transition action, không phải state riêng):

```
INIT → IDLE → LISTENING → PROCESSING → SPEAKING → IDLE
                  │ (timeout FR-8)         │ (lỗi)
                  └──────────► IDLE/ERROR ◄┘
```

| State | Vào khi | Hành động khi vào | Rời khi |
|-------|---------|-------------------|---------|
| INIT | bật nguồn | init HAL, kết nối thử server | hw xong → IDLE |
| IDLE | nghỉ | LED idle, LCD "Ready" | PTT press → LISTENING |
| LISTENING | giữ PTT | bật capture thread | PTT release → PROCESSING; 15s → IDLE |
| PROCESSING | thả PTT | đẩy STT→LLM qua net worker | có reply → SPEAKING; lỗi → ERROR |
| SPEAKING | có reply | TTS synth + playback | xong → IDLE; PTT (ngắt) → stop+IDLE; lỗi → ERROR |
| ERROR | lỗi bất kỳ | LED error, LCD thông báo cụ thể | PTT press → IDLE (thử lại) |

FSM **không làm IO trực tiếp**: nó *yêu cầu* middleware hành động rồi *chờ event*. Nhờ vậy test được toàn bộ logic với EventBus giả + middleware giả (không cần BBB).

```cpp
void StateMachine::handle(const Event& ev) {
    std::visit([&](auto&& e){ onEvent(e); }, ev);   // dispatch theo kiểu
}

void StateMachine::onEvent(const ButtonEvent& e) {
    if (state_ == State::Idle && e.id == ButtonId::Ptt && e.edge == Edge::Press) {
        transitionTo(State::Listening);
        audio_.startCapture();
    } else if (state_ == State::Speaking && e.id == ButtonId::Ptt
               && e.edge == Edge::Press) {
        audio_.stopPlayback();          // exit-action của ngắt (thay cho CANCEL state)
        transitionTo(State::Idle);
    } else if (e.id == ButtonId::VolUp || e.id == ButtonId::VolDown) {
        buttons_.handleVolume(e);       // không đổi state
    }
}
```

`transitionTo` là chỗ **duy nhất** đổi `state_`, và nó kiểm tra bảng transition hợp lệ (CLAUDE.md §8) — chuyển trái phép thì log error, không làm gì. Đây là bất biến quan trọng để FSM không "lạc" trạng thái.

---

## 4. ButtonController

Chuyển `GpioEvent` thô (đã debounce ở HAL) thành *ý định* ngữ nghĩa:
- PTT press/release → điều khiển vòng record (qua FSM).
- Vol+/Vol- → gọi `audio.setVolume(gain ± step)`, **không** đổi state (âm lượng độc lập với FSM).

Tách `ButtonController` khỏi FSM để FSM không phải biết "nút nào pin nào" — FSM chỉ quan tâm ý định.

---

## 5. LVGL UI — luật vàng: chỉ main thread

**Bất biến số 1** (architecture.md §3): mọi lời gọi `lv_*` chỉ xảy ra trên main thread. LVGL không thread-safe; gọi từ thread khác = crash ngẫu nhiên.

Cách giữ bất biến này *by design*: thread khác không có tham chiếu tới `LvglUi`. Chúng chỉ `push` event; main thread nhận event rồi tự gọi `ui_.showState(...)`.

```cpp
class LvglUi {
public:
    void showState(State s);              // đổi nhãn trạng thái
    void showText(const std::string&);    // hiển thị câu trả lời
    void showError(const std::string&);   // thông báo lỗi cụ thể
};
```

Hiệu năng (CLAUDE.md Risk Register): refresh theo **sự kiện đổi state**, không animation liên tục — BBB single-core, để CPU cho audio/network.

---

## 6. Main loop hoàn chỉnh

```cpp
int main() {
    Config cfg = Config::load("config/config.json");
    Logger::init(cfg);
    EventBus bus;

    AlsaAudioHal audio; FbDisplayHal disp; GpiodHal gpio;
    LvglUi ui(&disp);
    AudioPipeline pipeline(&audio, bus);
    NetWorkers net(cfg, bus);              // SttClient + LlmClient, 2 thread
    TtsEngine tts;
    StateMachine fsm(bus, ui, pipeline, net, tts, audio);

    gpio.startThread(bus);                 // GPIO edge-wait → push ButtonEvent
    fsm.transitionTo(State::Init);
    /* init HAL... */ fsm.transitionTo(State::Idle);

    while (running) {
        if (auto ev = bus.pop(10)) fsm.handle(*ev);
        lv_timer_handler();                // luôn đập nhịp UI
    }
}
```

Để ý: chỉ duy nhất vòng lặp này gọi `fsm.handle` và `lv_timer_handler` — tuần tự, một thread. Mọi sự song song (GPIO, capture, playback, network) nằm ngoài và chỉ giao tiếp qua `bus`.

---

## 7. Vì sao thiết kế này (tóm tắt tư duy)

| Quyết định | Lựa chọn khác | Vì sao chọn |
|-----------|----------------|-------------|
| Queue một-consumer | Callback chaining qua thread | Tuần tự hóa → triệt race vào LVGL/FSM |
| `std::variant` cho Event | base class + `dynamic_cast` | Tập đóng, compiler bắt thiếu nhánh, không cấp phát động |
| FSM không làm IO | FSM gọi thẳng ALSA/curl | Test được toàn bộ logic không cần phần cứng |
| Ngắt = transition action | CANCEL là state riêng | Ngắt là tức thời, không phải pha chờ → tránh over-engineering (Rule §18) |
