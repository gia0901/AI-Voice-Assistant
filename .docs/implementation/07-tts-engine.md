# 07 — TtsEngine (eSpeak-ng)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> **Phong cách mục 5:** mỗi hàm theo 3 lớp — **Bản chất** → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
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

> eSpeak-ng mặc định xuất **22050 Hz** mono. Playback ([02](02-audio-hal.md)) mở ở rate này, **hoặc** resample về 16k. Đơn giản nhất: mở một playback stream 22050 cho TTS (ALSA `plughw` lo phần còn lại). *Quyết định và lý giải.*
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

---

### 5.2 `synthesize()` (pipe + fork/exec)

**Bản chất.** Đây là **chạy một process con và bắt output của nó** — mô hình Unix kinh điển `pipe → fork → exec`. Hai điểm sống còn: (1) **deadlock pipe** — pipe có dung lượng hữu hạn; nếu cha `waitpid` *trước* khi đọc hết, con sẽ block khi ghi đầy pipe còn cha block chờ con → kẹt cả hai. Luật: **đọc tới EOF rồi mới `waitpid`**. (2) **không để zombie** — process con kết thúc vẫn chiếm slot tới khi cha `waitpid` "reap". Bonus an toàn: truyền `text` làm **argv của exec**, không qua shell ⇒ miễn nhiễm injection (đừng đổi sang `popen`/`system` nối chuỗi).

**API toolbox** (POSIX):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `pipe(fd)` | Tạo cặp fd: `fd[0]` đọc, `fd[1]` ghi | cha đóng đầu ghi, con đóng đầu đọc — đóng nhầm = treo |
| `fork()` | Nhân đôi tiến trình | trả 0 ở con, pid ở cha, <0 lỗi |
| `dup2(fd[1], STDOUT_FILENO)` | Chuyển stdout con vào pipe | làm ở **con**, trước exec |
| `execlp("espeak-ng", "espeak-ng", ...args..., nullptr)` | Thay ảnh process con bằng eSpeak | chỉ trả về **khi lỗi** → sau đó `_exit(127)` |
| `read(fd[0], buf, n)` lặp tới 0 | Đọc output tới EOF | **đọc hết trước** waitpid (chống deadlock) |
| `waitpid(pid, &st, 0)` + `WIFEXITED`/`WEXITSTATUS` | Reap con + lấy exit code | exit≠0 → Error |

**Pseudo:**
```
pipe(fd)
pid = fork()
con (pid==0):  dup2(fd[1], STDOUT); close fd[0]; close fd[1]
               execlp("espeak-ng","espeak-ng","-v",voice,"-s",speed,"--stdout",text,nullptr)
               _exit(127)                              // chỉ tới đây nếu exec fail
cha:           close(fd[1])
               vòng read(fd[0]) → nối vào raw tới EOF   // ĐỌC HẾT trước
               close(fd[0]); waitpid(pid,&st,0)
               !WIFEXITED(st) || WEXITSTATUS(st)!=0 → return Error{...}
               return stripWavHeader(raw)               // hoặc parse 'data' chunk
```
> **TODO(you):** viết vòng `read()` tới EOF và `stripWavHeader` (bỏ 44 byte hoặc tìm chunk `data`). **Thứ tự đọc-hết-rồi-wait** là chỗ Claude soi (deadlock + zombie).

---

### 5.3 Khung test
```cpp
TEST_CASE("stripWavHeader bỏ đúng 44 byte / tìm data chunk") {
    // TODO(you): cho 1 mẫu WAV nhỏ → assert số mẫu PCM đúng
}
// Integration (cần espeak-ng): synthesize("hello") → PcmBuffer không rỗng, exit 0.
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
