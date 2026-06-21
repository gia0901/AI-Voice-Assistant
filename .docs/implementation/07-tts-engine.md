# 07 — TtsEngine (eSpeak-ng)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> Nền: CLAUDE.md §6 (Decision #5), §9 (eSpeak fail), §10 (spawn per utterance)

---

## 1. 🎯 Mục tiêu & vị trí

Biến text → PCM bằng **eSpeak-ng**, để playback phát. Spawn process eSpeak-ng mỗi câu (short-lived).

**File:** `middleware/tts/TtsEngine.hpp/.cpp`.
**Phụ thuộc:** eSpeak-ng cài trên board; `Types.hpp` (`PcmBuffer`). Gọi từ main thread (PROCESSING→SPEAKING).

---

## 2. 📜 Hợp đồng

```cpp
class TtsEngine {
public:
    Result<PcmBuffer> synthesize(const std::string& text);   // text → PCM
};
```
**Ràng buộc:** spawn fail / exit ≠ 0 → `Error` (FSM phát beep lỗi thay vì kẹt SPEAKING — §9); không để zombie process.

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Spawn process hay link libespeak-ng?</summary>

> Cách đơn giản nhất: **spawn** `espeak-ng` với `--stdout` → đọc WAV từ stdout (CLAUDE.md §10). Ít phụ thuộc build, dễ thay. libespeak-ng nhanh hơn (không fork) nhưng phức tạp hơn — *đừng tối ưu sớm*. Bắt đầu spawn.
</details>

<details><summary><b>Q2.</b> eSpeak xuất sample rate nào? Có khớp playback 16k không?</summary>

> eSpeak-ng mặc định xuất **22050 Hz** mono. Playback (guide 02) mở ở rate này, **hoặc** resample về 16k. Đơn giản nhất: mở một playback stream 22050 cho TTS (ALSA `plughw` lo phần còn lại). *Quyết định và lý giải.*
</details>

<details><summary><b>Q3.</b> Lấy PCM qua pipe stdout hay file tạm?</summary>

> Pipe (`--stdout`) gọn, không rác file. Nhưng phải đọc hết pipe **trước** khi `waitpid` để tránh deadlock (pipe đầy → eSpeak block ghi, ta block chờ exit → kẹt). File tạm dễ hơn nhưng có I/O đĩa. Gợi ý: pipe + đọc-hết-rồi-wait.
</details>

<details><summary><b>Q4.</b> Tự sinh WAV-strip hay parse header?</summary>

> `--stdout` cho ra WAV (44-byte header + PCM). Bỏ 44 byte đầu để lấy PCM thô, **hoặc** parse header cho chắc (rate/bits). Parse an toàn hơn nếu eSpeak đổi format.
</details>

**⚖️ Bạn tự chốt:** giọng/voice (`-v en`), tốc độ (`-s`), rate playback cho TTS.

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | gọi tay `espeak-ng --stdout "hi" > out.wav` | nghe được bằng aplay |
| 2 | spawn + đọc stdout vào buffer (pipe) | buffer ≈ kích thước out.wav |
| 3 | strip/parse WAV → PcmBuffer | aplay PcmBuffer (qua HAL) nghe đúng |
| 4 | waitpid + kiểm exit code | exit≠0 → `Error` |
| 5 | spawn fail (đổi tên binary) → `Error` | không crash, không zombie |

---

## 5. 🧩 Khung code tự điền

### 5.1 `TtsEngine.hpp` — *skeleton*
```cpp
#pragma once
#include "Types.hpp"
#include <string>
namespace bbb {
struct TtsConfig { std::string voice="en"; int speed=160; };
class TtsEngine {
public:
    explicit TtsEngine(TtsConfig c={}) : cfg_(std::move(c)) {}
    Result<PcmBuffer> synthesize(const std::string& text);
private:
    TtsConfig cfg_;
};
} // namespace bbb
```

### 5.2 Thuật toán `synthesize` (pipe + fork/exec)
```
pipe(fd)                                   // fd[0] read, fd[1] write
pid = fork()
nếu pid == 0 (con):
    dup2(fd[1], STDOUT); close fd[0],fd[1]
    execlp("espeak-ng", "espeak-ng", "-v", voice, "-s", speed,
           "--stdout", text, nullptr)
    _exit(127)                             // exec fail
cha:
    close(fd[1])
    đọc HẾT fd[0] vào std::vector<uint8_t> raw   // QUAN TRỌNG: đọc hết trước waitpid
    close(fd[0])
    waitpid(pid, &status)
    nếu !WIFEXITED || WEXITSTATUS != 0 → return Error{...}
    pcm = stripWavHeader(raw)              // hoặc parse header
    return pcm
```
> TODO(you): viết `stripWavHeader` (hoặc parse 'data' chunk). Và xử lý đọc pipe theo vòng lặp `read()` tới EOF. **Thứ tự đọc-hết-rồi-wait** là chỗ Claude soi (deadlock pipe).

> Lưu ý an toàn: `text` đi thẳng vào exec — *không* qua shell (`system()`), nên không có injection. Đừng đổi sang `popen("espeak ... " + text)` (shell injection + khó kiểm lỗi).

### 5.3 Khung test
```cpp
// Integration (cần espeak-ng): synthesize("hello") → PcmBuffer không rỗng, exit 0.
// Unit: stripWavHeader(mẫu WAV) → đúng số mẫu PCM.
TEST_CASE("stripWavHeader bỏ đúng 44 byte / tìm data chunk") {
    // TODO(you)
}
```

---

## 6. ⚠️ Cạm bẫy (CLAUDE.md §9)

- **`waitpid` trước khi đọc hết pipe** → deadlock (pipe đầy). Đọc hết → rồi wait.
- **Zombie process** → quên `waitpid`. Luôn reap.
- **Dùng `system()`/`popen` với text nối chuỗi** → shell injection + khó bắt exit. Dùng `execlp` truyền arg.
- **Giả định 16k** → eSpeak ra 22050. Khớp rate playback hoặc resample.
- **exec fail im lặng** → kẹt SPEAKING. Kiểm exit code, trả Error → beep (§9).

---

## 7. ✅ Checkpoint review

**Xong khi:** text → PCM phát nghe rõ; exit≠0 và spawn-fail đều ra `Error` (không crash/zombie); `stripWavHeader` test pass.

**Đưa Claude review:** `TtsEngine.cpp`. Hỏi:
1. *"Thứ tự đọc pipe / waitpid của tôi có nguy cơ deadlock không?"*
2. *"Tôi có để lại zombie khi lỗi giữa chừng không?"*
3. *"strip/parse WAV của tôi đúng với output eSpeak (rate, bits) chưa?"*
