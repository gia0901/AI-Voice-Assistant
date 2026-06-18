# Strategy Roadmap — Lộ trình 2 tuần cho kỹ sư Junior

> Tài liệu học tập (tiếng Việt theo Rule §18). Đây **không** phải bản kế hoạch theo ngày (cái đó ở [../timeline.md](../timeline.md)) cũng **không** phải hướng dẫn copy-paste code. Đây là **lộ trình tư duy**: học gì → giải bài toán nào → tự quyết ra sao → khi nào nhờ review.
>
> **Giao kèo làm việc:** *Bạn tự code phần lớn project. Claude là người review & cải thiện, không phải người viết hộ.* Tài liệu này được viết để ép bạn suy nghĩ trước, code sau, và biết khi nào dừng lại hỏi.

---

## 0. Cách dùng tài liệu này

Mỗi giai đoạn (Phase) có 5 phần cố định:

1. **🎯 Mục tiêu & học gì** — đọc tài liệu nào trước khi gõ dòng code đầu tiên.
2. **❓ Bài toán cần giải** — phát biểu vấn đề, *không* cho lời giải sẵn.
3. **🤔 Câu hỏi dẫn đường (có đáp án)** — tự trả lời trước, rồi mở đáp án đối chiếu.
4. **⚖️ Quyết định bạn phải tự đưa ra** — chỗ không có đáp án đúng tuyệt đối; cân nhắc đánh đổi.
5. **✅ Checkpoint review** — tiêu chí "xong", và *chính xác* nên đưa gì cho Claude review.

> Quy tắc tự kỷ luật: **không mở đáp án** ở phần ❓ cho tới khi đã tự viết câu trả lời của mình ra giấy/comment. Học nằm ở khoảng cách giữa câu trả lời của bạn và đáp án.

---

## 1. Điểm xuất phát (đã xong, không làm lại)

Phần cứng & bring-up đã hoàn tất — **bỏ qua**:
- Board boot, SSH, kernel 5.10 ✔
- Màn hình ILI9341 qua fbtft, `/dev/fb0` chạy ✔ (lý thuyết: [../development/device_driver.md](../development/device_driver.md))
- USB audio record/playback ✔
- Cross-compile crosstool-ng ✔ (lý thuyết: [cross_compile.md](cross_compile.md), thao tác: [../env_setup.md](../env_setup.md))

Bạn đang ở đây trong [../timeline.md](../timeline.md): cuối Week 2 → bắt đầu viết phần mềm. Roadmap này nén nội dung Week 2 (HAL) → Week 4 (Integration) vào **~10 ngày làm việc tập trung**.

**Trước khi bắt đầu, đọc một lượt để có bản đồ trong đầu:**
- [../architecture.md](../architecture.md) — 4 tầng, ai gọi ai, luồng dữ liệu một lượt PTT. *Đây là tấm bản đồ. Không có nó bạn sẽ lạc.*
- [../development/coding_guide.md](../development/coding_guide.md) — luật chơi C++17 áp dụng xuyên suốt.

---

## 2. Tư duy nền: hỏi đúng câu hỏi trước khi code

Trước mỗi component, tự hỏi 4 câu (lặp lại suốt project):

1. **Nó sở hữu cái gì?** (tài nguyên: fd, thread, buffer, kết nối)
2. **Ai gọi nó, nó gọi ai?** (chỉ gọi xuống tầng dưới — [../architecture.md](../architecture.md))
3. **Chạy trên thread nào?** (main? worker? capture? — [threading_eventbus.md](threading_eventbus.md))
4. **Lỗi thì sao?** (trả `Result`? đẩy event lỗi? về ERROR? — CLAUDE.md §9)

Nếu trả lời được 4 câu này, bạn đã thiết kế xong component trước khi viết dòng code đầu tiên. Nếu không → đó là dấu hiệu chưa hiểu, đừng code vội.

---

## Phase A — Common + một HAL đầu tiên (Ngày 1–3)

### 🎯 Mục tiêu & học gì
Dựng xương sống build + tầng `common/`, rồi làm **một** HAL trọn vẹn (đề xuất: `IGpioHal`, vì nó đơn giản và cho phản hồi vật lý ngay — nút bấm + LED).
- Đọc: [../development/hal_layer.md](../development/hal_layer.md) (mock pattern), [../development/coding_guide.md](../development/coding_guide.md) (RAII §3, ownership §4).
- Thiết lập CMake cross-build: [../env_setup.md](../env_setup.md) §5.

### ❓ Bài toán cần giải
1. EventBus phải an toàn khi nhiều thread `push`, một thread `pop`, và `pop` không được "ngủ quên" làm UI đứng.
2. `GpiodHal` phải chờ cạnh nút bấm mà **không** ngốn 100% CPU (BBB single-core).
3. Phải test được logic mà không cần cắm board.

### 🤔 Câu hỏi dẫn đường

<details><summary><b>Q1.</b> Vì sao EventBus dùng <code>std::variant</code> chứ không phải class cha + kế thừa cho Event?</summary>

> Variant = tập đóng: thêm event mới thì `std::visit` báo lỗi compile ở chỗ thiếu nhánh → không quên xử lý. Kế thừa + `dynamic_cast` thì cấp phát động, rò kiểu lúc runtime, dễ quên `case`. Chi tiết: [../development/app_layer.md](../development/app_layer.md) §2.
</details>

<details><summary><b>Q2.</b> Vì sao <code>pop()</code> cần timeout thay vì chờ vô hạn?</summary>

> Main loop phải xen kẽ xử lý event **và** gọi `lv_timer_handler()`. Chờ vô hạn → UI đứng tới khi có event. Dùng `condition_variable::wait_for` (timed wait). Lý thuyết: [threading_eventbus.md](threading_eventbus.md) §4.
</details>

<details><summary><b>Q3.</b> "Chờ cạnh không ngốn CPU" — cơ chế nào của libgpiod giúp điều đó?</summary>

> `gpiod_line_event_wait()` là *blocking wait* trên file descriptor — kernel cho thread ngủ tới khi có cạnh, không busy-loop. Đây là lý do chọn libgpiod (chardev) thay sysfs polling (CLAUDE.md Decision #8).
</details>

<details><summary><b>Q4.</b> Debounce nên đặt ở tầng nào — HAL hay ButtonController?</summary>

> HAL (gần phần cứng nhất). HAL phát ra cạnh "đã sạch"; ButtonController chỉ diễn giải *ý nghĩa* (PTT hold, volume step). Tách biệt trách nhiệm — [../development/hal_layer.md](../development/hal_layer.md) §6 "HAL không ôm logic nghiệp vụ".
</details>

### ⚖️ Quyết định bạn phải tự đưa ra
- **Debounce bao nhiêu ms?** (gợi ý 20–30ms — đo thực tế nút của bạn, đừng đoán).
- **EventBus có cần giới hạn kích thước hàng đợi không?** (cân nhắc: event đến nhanh hơn xử lý có xảy ra ở dự án này không? Phần lớn là không → đừng over-engineer. Nhưng hãy *lý giải được* vì sao bỏ qua.)
- **`waitEvent` trả `bool` hay `Result`?** (đọc nguyên tắc HAL không throw — [hal_layer.md §3](../development/hal_layer.md)).

### ✅ Checkpoint review
- "Xong" khi: nhấn PTT thật → log in ra `ButtonEvent` đúng; LED bật/tắt được; unit test EventBus (push/pop nhiều thread) + MockGpioHal pass trên VM.
- **Đưa cho Claude review:** `common/EventBus.hpp`, `GpiodHal` impl, và test. Hỏi cụ thể: *"Review giúp race condition trong EventBus và việc quản lý vòng đời `gpiod_line`/fd của tôi."* (Câu hỏi cụ thể → review chất lượng hơn câu "xem hộ em".)

---

## Phase B — Audio HAL + AudioPipeline (Ngày 3–5)

### 🎯 Mục tiêu & học gì
`IAudioHal` (ALSA capture/playback/gain) + `AudioPipeline` (lắp buffer, FR-8 timeout, software gain).
- Đọc trước: [audio_alsa.md](audio_alsa.md) **toàn bộ** — period/buffer/xrun/`plughw` là kiến thức bắt buộc.
- Đối chiếu interface: [../architecture.md](../architecture.md) §2.1.

### ❓ Bài toán cần giải
1. Ghi âm chạy ở thread riêng, dừng khi PTT nhả **hoặc** quá 15s (FR-8) — hai điều kiện dừng từ hai nguồn khác nhau.
2. USB adapter rẻ có thể không có 16kHz native / không có mixer phần cứng.
3. Buffer ~480KB không được copy lung tung qua thread.

### 🤔 Câu hỏi dẫn đường

<details><summary><b>Q1.</b> Capture thread đang block trong <code>readPeriod()</code>, làm sao dừng nó khi PTT nhả?</summary>

> Đặt một cờ `std::atomic<bool> running_`; thread đọc theo *từng period* (không đọc một phát 15s), sau mỗi period kiểm cờ. PTT nhả → set cờ false → thread thoát vòng lặp sau period hiện tại. FR-8 timeout: so sánh tổng frame đã đọc với `15s × 16000`. Đây là lý do `readPeriod` đọc *từng period* chứ không phải cả buffer.
</details>

<details><summary><b>Q2.</b> <code>hw:</code> hay <code>plughw:</code>? Đánh đổi gì?</summary>

> `plughw` tự resample/convert → an toàn với adapter rẻ, nhưng thêm một lớp xử lý. `hw` truy cập thẳng, hiệu năng tốt nhưng lỗi nếu card không hỗ trợ format. Dự án ưu tiên *chạy được* → `plughw`. Đo thử với `arecord` trước (CLAUDE.md Risk Register). Lý thuyết: [audio_alsa.md](audio_alsa.md) §2.
</details>

<details><summary><b>Q3.</b> Software gain clamp thế nào cho khỏi méo?</summary>

> `sample = clamp(sample * gain, INT16_MIN, INT16_MAX)`. Gain >1 dễ clip → méo. [audio_alsa.md](audio_alsa.md) §5.
</details>

<details><summary><b>Q4.</b> Buffer ghi xong chuyển sang PROCESSING bằng cách nào để không copy?</summary>

> `bus.push(RecordingComplete{ std::move(buffer) })`. Sau move, owner cũ rỗng — cấm dùng lại. [coding_guide.md §4](../development/coding_guide.md), [threading_eventbus.md §5](threading_eventbus.md).
</details>

### ⚖️ Quyết định bạn phải tự đưa ra
- **Period size?** Nhỏ → độ trễ thấp, dễ xrun; lớn → ngược lại. Bắt đầu giá trị mặc định, đo xrun, chỉnh.
- **Gain step mỗi lần nhấn Vol±?** (tuyến tính hay theo dB? Tai người nghe theo log → cân nhắc bước phi tuyến).
- **Lưu PCM dạng gì để gửi STT?** (raw PCM? WAV? phụ thuộc API Whisper server bạn dùng — kiểm trước, CLAUDE.md §5).

### ✅ Checkpoint review
- "Xong" khi: giữ PTT → nói → nhả, thu được buffer; tự nhả sau 15s; gain đổi âm lượng nghe được; MockAudioHal test AudioPipeline (gain + FR-8) pass.
- **Đưa Claude review:** vòng lặp capture thread + cách dừng (atomic + timeout), và xử lý lỗi `-EPIPE` (xrun recovery). Hỏi: *"Vòng đời thread và điều kiện dừng kép của tôi có race không?"*

---

## Phase C — Network clients + TTS (Ngày 5–7)

### 🎯 Mục tiêu & học gì
`SttClient` (HTTP → Whisper), `LlmClient` (HTTP → LM Studio), `TtsEngine` (spawn eSpeak-ng → PCM). Chạy trên thread pool 2 worker.
- Đọc: [../architecture.md](../architecture.md) §2.2; CLAUDE.md §5 (topology) & §9 (error handling).
- *Chưa có tài liệu sâu về libcurl* — nếu thấy cần, đó là ứng viên viết mới `knowledge/http_libcurl.md` (xem gợi ý cuối [README.md](README.md)).

### ❓ Bài toán cần giải
1. Gọi HTTP **không bao giờ** được block main thread.
2. Server PC có thể chưa bật / mất kết nối giữa chừng (NFR-3, §9).
3. Phải phân biệt rõ "STT chết" vs "LLM chết" trên LCD.

### 🤔 Câu hỏi dẫn đường

<details><summary><b>Q1.</b> Vì sao tách <code>SttClient</code> và <code>LlmClient</code> mà không gộp "AiClient"?</summary>

> Hai server khác nhau, hai API khác nhau, hai chế độ lỗi cần báo *riêng biệt* (§9). Gộp lại làm rối khi cần biết cái nào chết. [../architecture.md §6](../architecture.md).
</details>

<details><summary><b>Q2.</b> Worker thread gọi xong thì làm gì với kết quả?</summary>

> `push` một event (`SttResult`/`LlmResult`/`NetworkError`) lên bus — **không** gọi thẳng vào StateMachine. Tuần tự hóa về main thread. [threading_eventbus.md](threading_eventbus.md), [app_layer.md](../development/app_layer.md).
</details>

<details><summary><b>Q3.</b> Đặt connect timeout bao lâu, vì sao?</summary>

> Ngắn (~3s) để phát hiện "server chưa bật" nhanh, chuyển ERROR thay vì treo người dùng (CLAUDE.md §9). Đánh đổi: mạng chậm thật có thể bị cắt oan → có thể tách connect-timeout (ngắn) khỏi total-timeout (dài hơn cho câu trả lời LLM dài).
</details>

<details><summary><b>Q4.</b> eSpeak-ng: gọi như process con hay dùng thư viện?</summary>

> Cách đơn giản nhất: spawn process, hứng PCM qua stdout/pipe (CLAUDE.md §10 "spawned per utterance"). Kiểm exit code; spawn fail → `TtsFailed` → beep lỗi, đừng treo ở SPEAKING (§9). Cân nhắc libespeak-ng nếu cần ít overhead hơn — nhưng *đừng tối ưu sớm*.
</details>

### ⚖️ Quyết định bạn phải tự đưa ra
- **Endpoint Whisper chính xác** — phải tự `curl` kiểm với server thật của bạn (CLAUDE.md §5 cảnh báo path khác nhau giữa các build). Đây là rủi ro #1 trong Risk Register.
- **Định dạng body gửi LLM** — system prompt? giới hạn token? nhiệt độ? (ảnh hưởng độ dài câu trả lời → NFR-1).
- **Thread pool 2 hay nhiều hơn?** (lý giải: một lượt PTT là tuần tự STT→LLM, song song không giúp gì → 2 là dư).

### ✅ Checkpoint review
- "Xong" khi: từ board, `curl` tới cả hai server OK; client C++ thực hiện audio→text→reply round-trip; rút mạng → nhận `NetworkError` đúng service, không crash.
- **Đưa Claude review:** xử lý timeout/lỗi libcurl và việc gói kết quả thành event. Hỏi: *"Có chỗ nào worker thread có thể chạm vào state dùng chung ngoài bus không?"*

---

## Phase D — StateMachine + UI + ráp main loop (Ngày 7–9)

### 🎯 Mục tiêu & học gì
`StateMachine` (7 trạng thái), `ButtonController`, `LvglUi`, và `main()` ráp tất cả.
- Đọc kỹ: [../development/app_layer.md](../development/app_layer.md) (toàn bộ) + CLAUDE.md §8 (bảng transition).

### ❓ Bài toán cần giải
1. FSM phải từ chối chuyển trạng thái trái phép (CLAUDE.md §8) thay vì "lạc".
2. LVGL chỉ được gọi trên main thread — bằng *thiết kế*, không bằng "nhớ đừng gọi sai".
3. Ngắt khi đang SPEAKING (nhấn PTT) — xử lý sao cho gọn (nhớ: ta đã **bỏ CANCEL state**).

### 🤔 Câu hỏi dẫn đường

<details><summary><b>Q1.</b> Làm sao đảm bảo LVGL không bị gọi từ thread khác — bằng design chứ không bằng kỷ luật?</summary>

> Các thread khác **không giữ tham chiếu** tới `LvglUi`. Chúng chỉ `push` event; main thread nhận rồi mới gọi `ui_.showState()`. Không có con đường để gọi sai. [app_layer.md §5](../development/app_layer.md).
</details>

<details><summary><b>Q2.</b> Ngắt SPEAKING xử lý thế nào sau khi đã bỏ CANCEL?</summary>

> Là một *transition action*: trong SPEAKING nhận `ButtonEvent(PTT,Press)` → `audio.stopPlayback()` → về IDLE. Không cần state riêng vì dừng playback là tức thời, không phải pha chờ. (Đây chính là quyết định ta đã chốt trong review CLAUDE.md.) [app_layer.md §3](../development/app_layer.md).
</details>

<details><summary><b>Q3.</b> Chỉ một chỗ được đổi <code>state_</code> — vì sao quan trọng?</summary>

> Gom mọi kiểm tra transition hợp lệ vào `transitionTo()` → một điểm để log, để validate, để đặt breakpoint. Rải `state_ = ...` khắp nơi → không thể suy luận. [app_layer.md §3](../development/app_layer.md).
</details>

<details><summary><b>Q4.</b> Vol± có làm đổi state không?</summary>

> Không. Âm lượng độc lập với FSM — `ButtonController` xử lý trực tiếp qua `audio.setVolume()`. Đừng nhét mọi nút vào FSM. [app_layer.md §4](../development/app_layer.md).
</details>

### ⚖️ Quyết định bạn phải tự đưa ra
- **Hiển thị câu trả lời dài trên màn 320×240 ra sao?** (cuộn? cắt? font size? — UX thật sự).
- **Trạng thái INIT khi server chưa bật:** vào ERROR ngay hay IDLE với cảnh báo? (CLAUDE.md §5 khuyên check connectivity lúc khởi động — nhưng đừng chặn boot).
- **LED pattern** cho idle/active/error (FR-7) — nhấp nháy thế nào để phân biệt được bằng mắt.

### ✅ Checkpoint review
- "Xong" khi: LCD hiển thị INIT→IDLE→LISTENING→PROCESSING→SPEAKING→IDLE đúng nhịp; transition test (bus giả + middleware giả) phủ các nhánh hợp lệ/trái phép.
- **Đưa Claude review:** `StateMachine` + bảng transition + main loop. Hỏi: *"Có nhánh sự kiện nào tôi chưa xử lý (visit thiếu case) không, và có transition trái phép nào lọt lưới không?"*

---

## Phase E — Integration, lỗi, systemd, đo NFR (Ngày 9–10+)

### 🎯 Mục tiêu & học gì
Ráp đầu-cuối trên phần cứng thật, làm cứng lỗi (CLAUDE.md §9), systemd, đo NFR-1/NFR-2.
- Đọc: CLAUDE.md §9 (bảng lỗi), §14 (Definition of Done), [../timeline.md](../timeline.md) Week 4, [../troubleshooting.md](../troubleshooting.md).

### ❓ Bài toán cần giải
1. Mỗi loại lỗi ở §9 phải cho ra ERROR state *không crash*, thông báo *cụ thể*, tự phục hồi ở PTT kế tiếp.
2. App phải sống qua `systemctl restart` và power cycle (§14).
3. NFR-1 < 5s và NFR-2 < 200MB — đo, không đoán.

### 🤔 Câu hỏi dẫn đường

<details><summary><b>Q1.</b> Đo NFR-2 (&lt;200MB) bằng gì, lúc nào?</summary>

> `free -m` *trong lúc* một lượt PTT round-trip (không phải lúc idle). Tham chiếu ngân sách RAM CLAUDE.md §10. Nghi leak → ASan/valgrind trên host build.
</details>

<details><summary><b>Q2.</b> Vì sao dùng systemd thay vì script chạy tay?</summary>

> Auto-restart khi crash, tích hợp boot (NFR-3), log qua journald. [../timeline.md](../timeline.md) Week 4 / CLAUDE.md §11.
</details>

<details><summary><b>Q3.</b> "Auto-retry ở PTT kế tiếp" nghĩa là gì về mặt FSM?</summary>

> ERROR → (PTT press) → IDLE → thử lại bình thường. Không cần restart app. CLAUDE.md §9.
</details>

### ⚖️ Quyết định bạn phải tự đưa ra
- **Nếu vượt NFR-1 5s** — model LLM nhỏ hơn? rút ngắn câu trả lời? (CLAUDE.md NFR-1: latency do PC quyết, "keep models small").
- **eSpeak fail** — beep bằng gì khi chưa có TTS? (PCM sine tự sinh? file wav nhúng?).

### ✅ Checkpoint review (= Definition of Done, CLAUDE.md §14)
- Toàn bộ tiêu chí §14 đạt. Đây là review cuối — đưa Claude *toàn bộ* luồng + log một lượt chạy thật + số đo NFR.

---

## 3. Giao thức cộng tác với Claude (đọc kỹ phần này)

Bạn code, Claude review. Để review *chất lượng*, không lãng phí:

**Nên:**
- Hỏi **câu hỏi cụ thể** ("race trong vòng lặp capture?") thay vì "xem hộ em".
- Tự nêu **bạn đã cân nhắc gì** trước khi hỏi → Claude review *suy nghĩ* của bạn, không chỉ code.
- Đưa **đủ ngữ cảnh tối thiểu**: file liên quan + triệu chứng + cái bạn đã thử.
- Review **theo từng Phase**, đừng dồn cả project cuối cùng.

**Không nên:**
- Nhờ Claude viết nguyên component khi bạn chưa thử — bạn mất phần học giá trị nhất.
- Bỏ qua phần ❓/⚖️ rồi hỏi đáp án — đó là gian lận với chính mình.

**Khi bí (debug):** mang theo *triệu chứng + giả thuyết của bạn*, không chỉ "nó lỗi". Tham khảo [../troubleshooting.md](../troubleshooting.md) trước.

---

## 4. Handoff — cho Claude ở conversation MỚI

> Dán/diễn đạt phần này khi mở hội thoại mới để Claude vào vai nhanh.

**Bối cảnh dự án:** BBB Voice Assistant (PTT → STT → LLM → TTS), embedded C++17. Kế hoạch tổng: [CLAUDE.md](../../CLAUDE.md). Phần cứng (board/màn hình/audio/cross-compile) **đã xong**; đang ở giai đoạn viết phần mềm theo lộ trình này.

**Vai trò của Claude:** *Reviewer & mentor, KHÔNG phải code-writer.* User tự code phần lớn. Claude:
1. Review code/thiết kế user đưa ra theo [coding_guide.md](../development/coding_guide.md) và các invariant trong [../architecture.md](../architecture.md) (đặc biệt: LVGL chỉ main-thread; ownership buffer; HAL không rò rỉ abstraction; FSM không làm IO).
2. Trả lời bằng **câu hỏi gợi mở + đánh đổi**, chỉ đưa code mẫu khi user thực sự bí hoặc để minh họa một điểm.
3. Dẫn chiếu tài liệu có sẵn trong `.docs/` thay vì lặp lại.

**Đang ở đâu / làm gì tiếp:** kiểm [CHECK_LIST.md](../../CHECK_LIST.md) (trạng thái sống) và xác định Phase hiện tại trong tài liệu này (A→E). Bắt đầu phiên bằng câu: *"Bạn đang ở Phase nào, đã tự cân nhắc gì, vướng ở đâu?"* trước khi review.

**Ranh giới quan trọng:** đừng tự ý generate cả component; đừng bỏ qua phần ❓/⚖️ giúp user; ưu tiên để user tự giải, Claude chỉ chỉnh hướng.

**Tài liệu bản đồ nhanh:**
- Tổng thể & quyết định → [CLAUDE.md](../../CLAUDE.md)
- Kiến trúc/interface/thread → [../architecture.md](../architecture.md)
- Luật code → [../development/coding_guide.md](../development/coding_guide.md)
- Chi tiết tầng → [../development/hal_layer.md](../development/hal_layer.md), [../development/app_layer.md](../development/app_layer.md)
- Nền tảng lý thuyết → [README.md](README.md) (concurrency, ALSA, cross-compile)
- Gỡ lỗi → [../troubleshooting.md](../troubleshooting.md)
