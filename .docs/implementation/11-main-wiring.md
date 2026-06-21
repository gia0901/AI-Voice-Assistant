# 11 — Main wiring + systemd

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.* Guide cuối — ráp tất cả.
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

### 5.1 `main.cpp` — *skeleton (theo app_layer §6)*
```cpp
int main() {
    auto cfgR = bbb::loadConfig("/etc/bbb-va/config.json");
    if (auto* e = std::get_if<bbb::Error>(&cfgR)) { /* TODO: log + exit */ }
    auto cfg = std::get<bbb::AppConfig>(cfgR);
    bbb::Logger::init("info");

    bbb::EventBus bus;
    bbb::hal::AlsaAudioHal audio(cfg.audioDevice);
    bbb::hal::FbDisplayHal disp;
    bbb::hal::GpiodHal     gpio;
    bbb::LvglUi   ui(&disp);
    bbb::AudioPipeline pipe(&audio, bus, cfg.maxRecordSeconds);
    bbb::NetWorkers    net(cfg, bus);          // STT+LLM, 2 thread
    bbb::TtsEngine     tts;
    bbb::StateMachine  fsm(bus, ui, pipe, net, tts /*, audio*/);
    bbb::ButtonController buttons(&gpio, bus, pipe);

    // TODO(you): init theo thứ tự; fail nào → fsm.transitionTo(Error) + showError
    ui.init(); /* disp.init / gpio.init(cfg.pins) ... kiểm mã trả về */
    fsm.transitionTo(bbb::State::Init);
    /* ... */ fsm.transitionTo(bbb::State::Idle);

    buttons.start();                            // GPIO thread
    installSignalHandlers();                    // SIGTERM/SIGINT → running=false

    while (running) {
        if (auto ev = bus.pop(10)) fsm.handle(*ev);
        ui.tick();                              // lv_timer_handler
    }

    buttons.stop(); pipe.stop(); net.stop();    // join sạch
    return 0;
}
```
> TODO(you): `installSignalHandlers` chỉ set một `std::atomic<bool> running` (async-signal-safe, **không** làm gì nặng trong handler). Thứ tự stop/join để không thread nào còn push sau khi loop thoát. Đây là chỗ Claude soi.

### 5.2 systemd unit — `scripts/bbb-voice-assistant.service`
```ini
[Unit]
Description=BBB Voice Assistant
After=network-online.target sound.target
Wants=network-online.target

[Service]
ExecStart=/usr/local/bin/bbb-voice-assistant
Restart=on-failure
RestartSec=2
# User=gia            # nếu cần quyền gpio/audio: thêm group phù hợp

[Install]
WantedBy=multi-user.target
```
> TODO(you): chọn `User=`/group cho quyền GPIO+audio; đường dẫn config (`/etc/bbb-va/config.json`); `deploy.sh` copy binary + unit + `daemon-reload` + restart (CLAUDE.md §11).

---

## 6. ⚠️ Cạm bẫy (CLAUDE.md §9, §14)

- **Init fail làm crash/chặn boot** → phải vào ERROR, không panic.
- **Không join thread khi thoát** → thread treo, `systemctl stop` chậm/treo.
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
