# Implementation Guides

> Tài liệu học tập (tiếng Việt theo Rule §18). Hướng dẫn **dựng từng component** từ file rỗng → chạy được, theo đúng tinh thần: *bạn tự code, Claude review*. Đây là tầng giữa nối [../architecture.md](../architecture.md) (contract — *cái gì*) với code thật (*làm thế nào, từng bước*).

---

## 1. Nguyên tắc cốt lõi — đọc trước khi dùng

**Guide cho bạn GIÀN GIÁO, không cho LỜI GIẢI.** Cụ thể:
- ✅ Có: interface/chữ ký hàm đầy đủ, thứ tự dựng tăng dần, gợi ý thuật toán (pseudo-code) cho hàm khó, khung unit test, tiêu chí "xong".
- ❌ Không có: thân hàm production hoàn chỉnh để copy-paste. Các chỗ logic chính để **`// TODO(you)`** kèm gợi ý.

Vì sao? Giá trị học nằm ở chỗ bạn tự viết phần thân. Nếu guide viết hộ hết, nó phản lại mục tiêu project. Claude là người **review** cái bạn viết, không phải người viết thay (xem [../knowledge/strategy_roadmap.md](../knowledge/strategy_roadmap.md) §3 Giao thức cộng tác).

---

## 2. Cách dùng một guide — template 7 mục

Mỗi guide có cấu trúc cố định:

| Mục | Nội dung | Bạn làm gì |
|-----|----------|-----------|
| 1. **Mục tiêu & vị trí** | component gì, file nào, phụ thuộc cái gì có trước | xác định đã đủ tiền đề chưa |
| 2. **Hợp đồng** | interface chính xác, thread, mô hình lỗi (trích architecture.md) | nắm contract, không đổi tùy tiện |
| 3. **Quyết định trước khi code** | các lựa chọn + đánh đổi (Socratic, đáp án ẩn) | tự trả lời trước khi mở đáp án |
| 4. **Trình tự dựng** | từng bước, mỗi bước có "done-when" | làm tuần tự, không nhảy cóc |
| 5. **Khung code tự điền** | skeleton + chữ ký + `TODO(you)` + pseudo-code + khung test | điền phần thân, làm test pass |
| 6. **Cạm bẫy** | lỗi đặc thù component (trích troubleshooting.md) | tránh trước khi vấp |
| 7. **Checkpoint review** | tiêu chí xong + đưa gì cho Claude + hỏi câu gì | tự kiểm, rồi nhờ review |

> Kỷ luật: ở mục 3, **tự viết câu trả lời ra trước** rồi mới mở `<details>` đối chiếu. Học nằm ở khoảng cách giữa hai câu trả lời.

---

## 3. Thứ tự dựng (build order)

Đánh số = thứ tự khuyến nghị, khớp các Phase trong [../knowledge/strategy_roadmap.md](../knowledge/strategy_roadmap.md). Mỗi component chỉ nên bắt đầu khi phụ thuộc của nó đã "xong".

| # | Guide | Component | Phase | Phụ thuộc |
|---|-------|-----------|-------|-----------|
| 00 | [00-common.md](00-common.md) | EventBus, Logger, Config, Types | A | — |
| 01 | [01-gpio-hal.md](01-gpio-hal.md) | IGpioHal + GpiodHal + Mock | A | Types |
| 02 | [02-audio-hal.md](02-audio-hal.md) | IAudioHal + AlsaAudioHal + Mock | B | Types |
| 03 | [03-display-hal.md](03-display-hal.md) | IDisplayHal + FbDisplayHal | B | Types |
| 04 | [04-audio-pipeline.md](04-audio-pipeline.md) | AudioPipeline (FR-8, gain) | B | IAudioHal, EventBus |
| 05 | [05-stt-client.md](05-stt-client.md) | SttClient (libcurl → Whisper) | C | EventBus, Config |
| 06 | [06-llm-client.md](06-llm-client.md) | LlmClient (libcurl → LM Studio) | C | EventBus, Config |
| 07 | [07-tts-engine.md](07-tts-engine.md) | TtsEngine (spawn eSpeak-ng) | C | — |
| 08 | [08-state-machine.md](08-state-machine.md) | StateMachine (7 trạng thái) | D | tất cả middleware, EventBus |
| 09 | [09-button-controller.md](09-button-controller.md) | ButtonController | D | IGpioHal, EventBus |
| 10 | [10-lvgl-ui.md](10-lvgl-ui.md) | LvglUi | D | IDisplayHal |
| 11 | [11-main-wiring.md](11-main-wiring.md) | main() + systemd | E | tất cả |

> Tất cả guide bám [../architecture.md](../architecture.md) (contract) + [../knowledge/strategy_roadmap.md](../knowledge/strategy_roadmap.md) (Phase). Code trong guide là **scaffold + pseudo-code**, không phải bản chạy được — bạn điền phần thân, Claude review.

---

## 4. Bản đồ tài liệu liên quan

- **Contract/interface** → [../architecture.md](../architecture.md) §2
- **Luật code** (RAII, Result<T>, ownership) → [../development/coding_guide.md](../development/coding_guide.md)
- **Thiết kế tầng** → [../development/hal_layer.md](../development/hal_layer.md), [../development/app_layer.md](../development/app_layer.md)
- **Tư duy mức Phase** → [../knowledge/strategy_roadmap.md](../knowledge/strategy_roadmap.md)
- **Nền tảng lý thuyết** → [../knowledge/](../knowledge/README.md)
- **Gỡ lỗi** → [../troubleshooting.md](../troubleshooting.md)
