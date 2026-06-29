# 11 — Main wiring + systemd

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.* Guide cuối — ráp tất cả.
>
> **Phong cách mục 5:** **Bản chất** → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
>
> Nền: [../development/app_layer.md](../development/app_layer.md) §6 · CLAUDE.md §11 (systemd), §14 (DoD)

---

## 1. 🎯 Mục tiêu & vị trí

`main()` ráp mọi component: load config → init logger/HAL → dựng middleware + FSM → start các thread → vòng lặp consumer → shutdown sạch. Đóng gói systemd service.

**File:** `app/main.cpp` (thay hello-world hiện tại), `scripts/bbb-voice-assistant.service`.
**Phụ thuộc:** **tất cả** guide trước.

---

## 2. 📜 Hợp đồng (cấu trúc, không phải interface)

Một vòng lặp main thread duy nhất gọi `fsm.handle` + `ui.tick`; mọi song song (GPIO/capture/playback/network) nằm ngoài, giao tiếp qua bus (architecture.md §3).

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Thứ tự init? Init fail thì sao?</summary>

> Config → Logger → HAL (audio/display/gpio) → UI → middleware → FSM. HAL/UI fail lúc INIT → vào **ERROR** state hiển thị lý do, **không** crash, không chặn boot (NFR-3, §9). "PC chưa sẵn sàng" cũng không được làm chết app (ai_server §7).
</details>

<details><summary><b>Q2.</b> Shutdown sạch gồm gì?</summary>

> Bắt SIGTERM/SIGINT → set cờ `running=false` → main loop thoát → `stop()`/join mọi thread (GPIO, capture, playback, net) → đóng HAL. systemd gửi SIGTERM khi `systemctl stop` — phải join để không bỏ thread treo.
</details>

<details><summary><b>Q3.</b> Vì sao systemd thay vì script chạy tay?</summary>

> Auto-restart khi crash, tích hợp boot (NFR-3), log journald, sống qua power cycle (§14). ([CLAUDE.md §11](../../CLAUDE.md))
</details>

<details><summary><b>Q4.</b> Health-check server lúc khởi động?</summary>

> Nên: `GET /v1/models` (LLM) + endpoint Whisper. Fail → hiển thị "PC unreachable" nhưng **vẫn vào IDLE** (thử lại ở PTT) — đừng kẹt ERROR vĩnh viễn (ai_server §7, §5 CLAUDE.md).
</details>

**⚖️ Bạn tự chốt:** user-service vs system-service, restart policy, có health-check lúc boot không.

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | load config + logger | sai config → log rõ, thoát sạch |
| 2 | init HAL + UI; fail → ERROR state | rút phần cứng → ERROR, không crash |
| 3 | dựng middleware + FSM; start GPIO/net thread | nhấn PTT chạy được luồng |
| 4 | main loop (`pop` + `handle` + `tick`) | end-to-end 1 lượt PTT chạy |
| 5 | SIGTERM handler + join | `systemctl stop` dừng sạch |
| 6 | systemd unit + enable | reboot tự chạy, crash tự restart |

---

## 5. 🧩 Khung code tự điền

### 5.1 `main()` — composition root + vòng đời

**Bản chất.** `main` là **composition root**: nơi *duy nhất* biết các lớp cụ thể (`AlsaAudioHal`, `FbDisplayHal`…) và lắp chúng vào nhau; mọi tầng khác chỉ thấy interface. Hai trách nhiệm sống còn: (1) **init là chuỗi có thể fail nửa chừng** — phần cứng/PC có thể chưa sẵn sàng, nên fail phải dẫn vào **ERROR state hiển thị được**, *không* crash/chặn boot (NFR-3); (2) **vòng đời đối xứng** — thứ tự `stop()`/join lúc tắt phải đảm bảo **không thread nào còn push lên bus sau khi loop đã thoát**, nếu không là use-after-free. Bản thân vòng lặp main rất mỏng: `pop → handle → tick`.

**API toolbox** (POSIX + C++ std):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `std::get_if<Error>(&result)` / `std::get<T>` | Rẽ nhánh `Result<T>` | kiểm Error trước khi `get<T>` |
| `sigaction(SIGTERM/SIGINT, ...)` | Đăng ký handler tắt | trong handler **chỉ** set `std::atomic<bool>` — async-signal-safe |
| `std::atomic<bool> running` | Cờ dừng main loop, set từ handler | dùng `volatile sig_atomic_t`/atomic, không làm gì nặng trong handler |
| `EventBus::pop(timeoutMs)` + `lv_timer_handler` | Nhịp main loop | timeout ngắn (~10ms) để UI mượt |

**Pseudo:**
```cpp
int main() {
    auto cfgR = loadConfig("/etc/bbb-va/config.json");
    if (auto* e = std::get_if<Error>(&cfgR)) { /* TODO: log + exit !=0 */ }
    auto cfg = std::get<AppConfig>(cfgR);
    Logger::init("info");

    EventBus bus;
    AlsaAudioHal audio(cfg.audioDevice);
    FbDisplayHal disp;
    GpioHal      gpio;
    LvglUi   ui(&disp);
    AudioPipeline pipe(&audio, bus, cfg.maxRecordSeconds);
    NetWorkers    net(cfg, bus);            // STT+LLM, 2 thread
    TtsEngine     tts;
    StateMachine  fsm(bus, ui, pipe, net, tts);
    ButtonController buttons(&gpio, bus, pipe);

    // init theo thứ tự; mỗi init fail → fsm.transitionTo(Error) + ui.showError(lý do)
    ui.init(); /* disp.init() / gpio.init(cfg.pins) ... kiểm MÃ TRẢ VỀ */
    fsm.transitionTo(State::Init);  /* ... */  fsm.transitionTo(State::Idle);

    buttons.start();                          // GPIO thread
    installSignalHandlers();                  // SIGTERM/SIGINT → running=false

    while (running) {
        if (auto ev = bus.pop(10)) fsm.handle(*ev);
        ui.tick();
    }
    buttons.stop(); pipe.stop(); net.stop();  // join TRƯỚC khi object hủy
    return 0;
}
```
> **TODO(you):** `installSignalHandlers` chỉ set `std::atomic<bool> running` (async-signal-safe — **không** log/malloc trong handler). Sắp thứ tự `stop()`/join sao cho không thread nào push sau khi loop thoát. Chỗ Claude soi.

---

### 5.2 systemd unit — `scripts/bbb-voice-assistant.service`

**Bản chất.** systemd biến binary thành **dịch vụ có vòng đời do OS quản**: tự chạy lúc boot, tự restart khi crash, dừng bằng SIGTERM (khớp §5.1), log vào journald. `After=`/`Wants=` khai báo *thứ tự phụ thuộc* (mạng, âm thanh) — nhưng "mạng có" không bằng "PC server có", nên app vẫn phải tự chịu được server chưa sẵn sàng (Q4).

```ini
[Unit]
Description=BBB Voice Assistant
After=network-online.target sound.target
Wants=network-online.target

[Service]
ExecStart=/usr/local/bin/bbb-voice-assistant
Restart=on-failure
RestartSec=2
# User=gia            # cần group gpio/audio để truy cập phần cứng

[Install]
WantedBy=multi-user.target
```
> **TODO(you):** chọn `User=`/group cho quyền GPIO+audio; đường dẫn config (`/etc/bbb-va/config.json`); `deploy.sh` copy binary + unit + `daemon-reload` + restart (CLAUDE.md §11).

---

## 6. ⚠️ Cạm bẫy (CLAUDE.md §9, §14)

- **Init fail làm crash/chặn boot** → phải vào ERROR, không panic.
- **Không join thread khi thoát** → thread treo, `systemctl stop` chậm/treo; hoặc push lên bus đã hủy → UB.
- **Làm việc nặng trong signal handler** → UB. Chỉ set atomic flag.
- **Service thiếu quyền gpio/audio** → chạy tay được, dưới systemd fail. Set User/group.
- **`config.json` không deploy kèm** → service start rồi chết. Deploy cùng binary.
- **Server chưa bật làm kẹt ERROR mãi** → health-check không được chặn vào IDLE.

---

## 7. ✅ Checkpoint review (= Definition of Done, CLAUDE.md §14)

**Xong khi:** 1 lượt PTT đầy đủ chạy trên phần cứng; rút PC → ERROR có thông báo, hồi phục ở PTT kế; `systemctl restart` + power cycle sống; RAM `free -m` < 200MB; latency < 5s.

**Đưa Claude review:** `main.cpp` + unit file + log một lượt chạy thật + số đo NFR. Hỏi:
1. *"Thứ tự init/stop của tôi có để thread nào push sau khi loop thoát không?"*
2. *"Signal handler của tôi có async-signal-safe không?"*
3. *"Init fail có path nào làm crash thay vì vào ERROR không?"*
