# Kiến thức: ALSA & audio số cơ bản

> Tài liệu học tập (tiếng Việt theo Rule §18). Nền tảng cho [development/hal_layer.md](../development/hal_layer.md) (IAudioHal) và FR-1/FR-4/NFR-4.

---

## 1. Audio số là gì (PCM)

Âm thanh analog liên tục được số hóa bằng cách **lấy mẫu** (sampling) theo chu kỳ và **lượng tử hóa** (quantize) biên độ. Kết quả là PCM (Pulse-Code Modulation) — chuỗi số nguyên.

Ba thông số định nghĩa một stream PCM (NFR-4 chốt: 16kHz / 16-bit / mono):

| Thông số | Giá trị dự án | Ý nghĩa |
|----------|---------------|---------|
| Sample rate | 16000 Hz | số mẫu/giây. Whisper huấn luyện ở 16kHz → khớp, khỏi resample |
| Bit depth | 16-bit (`S16_LE`) | mỗi mẫu là `int16_t`, signed, little-endian |
| Channels | 1 (mono) | một kênh; tiếng nói không cần stereo |

Tính kích thước: `16000 mẫu/s × 2 byte × 1 kênh = 32 KB/s`. Ghi tối đa 15s (FR-8) → ~480KB, đúng con số trong CLAUDE.md §10.

> Vì sao 16kHz đủ? Định lý Nyquist: tái tạo được tần số tới ~8kHz, phủ trọn dải tiếng nói (~300Hz–3.4kHz của điện thoại, tới ~8kHz là dư cho STT).

---

## 2. ALSA là gì

ALSA (Advanced Linux Sound Architecture) = lớp audio trong kernel + thư viện userspace `libasound`. Ta dùng `libasound` (FR: ALSA trực tiếp, không PulseAudio — embedded standard, CLAUDE.md Decision #2).

Khái niệm cốt lõi:

- **PCM device**: điểm vào/ra audio. Tên dạng `hw:CARD=Device,DEV=0` hoặc `plughw:...`.
- **`hw` vs `plughw`**:
  - `hw:` — truy cập thẳng phần cứng, *không* tự chuyển đổi. Nếu card không hỗ trợ 16kHz native → mở lỗi.
  - `plughw:` — chèn lớp "plug" tự resample/convert format. An toàn cho adapter USB rẻ (CLAUDE.md Risk Register).
- **Frame**: một mẫu trên *tất cả* kênh. Mono → 1 frame = 1 `int16_t`. Stereo → 1 frame = 2 `int16_t`. (HAL đếm theo *frames*, không phải bytes — tránh nhầm.)
- **Period**: số frame ALSA xử lý/ngắt mỗi lần. Đọc/ghi diễn ra theo từng period.
- **Buffer**: gồm nhiều period; là vùng đệm vòng giữa app và phần cứng.

---

## 3. Vòng đời capture & playback

```
open  → set hw params (rate, format, channels, period, buffer) → prepare
      → vòng lặp readi()/writei() theo period → drain (playback) → close
```

Capture (FR-1):
```cpp
snd_pcm_open(&pcm, dev, SND_PCM_STREAM_CAPTURE, 0);
// set params: S16_LE, 16000, 1 ch, period size...
snd_pcm_readi(pcm, buf, frames);   // blocking, trả số frame đọc được
```

Playback (FR-4): tương tự với `SND_PCM_STREAM_PLAYBACK` + `snd_pcm_writei`, kết thúc bằng `snd_pcm_drain()` để phát hết buffer trước khi đóng.

---

## 4. Xrun — kẻ thù của audio thời gian thực

**Xrun** = buffer under/overrun:
- **Underrun** (playback): app ghi quá chậm, phần cứng "đói" dữ liệu → tiếng giật/click.
- **Overrun** (capture): app đọc quá chậm, buffer đầy → mất mẫu.

Nguyên nhân trên BBB single-core: CPU bị giành bởi LVGL redraw / network (CLAUDE.md Risk Register). Cách giảm:
- Buffer/period đủ lớn (đánh đổi độ trễ).
- Không log trong vòng lặp audio nóng.
- Audio chạy thread riêng (architecture.md §3), không bị main loop chặn.

Khi xrun xảy ra, `readi/writei` trả lỗi `-EPIPE` → cần `snd_pcm_prepare()` để phục hồi.

---

## 5. Âm lượng — vì sao software gain (FR-6)

Nhiều USB audio adapter rẻ **không có** mixer control phần cứng (kiểm bằng `amixer -c <n> scontrols` → rỗng/không có "PCM"/"Master"). Khi đó Vol+/Vol- không thể chỉnh qua `amixer`.

Giải pháp: **software PCM gain** — nhân biên độ mẫu trước khi `writePeriod`:
```cpp
sample = clamp(int16, sample * gain);   // gain ∈ [0.0, 1.0]
```
Lưu ý clamp để tránh tràn `int16_t` (gain >1 gây clip méo tiếng). `IAudioHal::setVolume(gain)` chính là chỗ này.

---

## 6. Lệnh bring-up tay (trước khi viết C++)

```bash
arecord -l                                   # liệt kê card capture
arecord -D plughw:CARD=Device,DEV=0 \
        -f S16_LE -r 16000 -c 1 -d 5 t.wav   # ghi 5s
aplay t.wav                                  # phát lại
amixer -c <n> scontrols                      # có mixer control không?
speaker-test -D plughw:... -c 1 -t sine      # test loa
```

Chạy được các lệnh này = phần cứng/format OK, mới nên viết HAL (CHECK_LIST.md Week 1).
