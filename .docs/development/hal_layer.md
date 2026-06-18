# HAL Layer — Thiết kế & Mock Pattern

> Tài liệu học tập (tiếng Việt theo Rule §18). HAL = Hardware Abstraction Layer. Giải thích *vì sao* cần HAL, thiết kế interface ra sao, và mock để test không cần phần cứng. Đối chiếu interface trong [architecture.md](../architecture.md) §2.1.

---

## 1. Vì sao cần HAL?

Vấn đề: nếu `AudioPipeline` gọi thẳng `snd_pcm_readi()`, thì:
- Không test được trên VM (không có card ALSA / không có BBB).
- Đổi từ ALSA sang PulseAudio sau này phải sửa khắp nơi.
- Logic nghiệp vụ trộn lẫn lời gọi syscall → khó đọc.

Giải pháp: chèn một **ranh giới** — interface trừu tượng. Tầng trên chỉ thấy `IAudioHal`, không thấy ALSA. Đây là áp dụng **Dependency Inversion Principle**: module cấp cao phụ thuộc *abstraction*, không phụ thuộc *chi tiết*.

```
AudioPipeline ──depends on──► IAudioHal (abstraction)
                                 ▲           ▲
                          AlsaAudioHal   MockAudioHal
                          (thật, trên BBB) (test, trên VM)
```

---

## 2. Ba interface HAL

Tất cả là **pure virtual**, đặt trong `hal/include/`. Chỉ tầng HAL impl được `#include` header của ALSA/libgpiod/fbdev.

### 2.1 IAudioHal — ALSA
Trách nhiệm: mở/đóng capture & playback, đọc/ghi từng *period*, áp software gain (FR-6).

```cpp
struct AudioFormat { unsigned rate = 16000; unsigned channels = 1; /* S16_LE */ };

class IAudioHal {
public:
    virtual ~IAudioHal() = default;
    virtual int  openCapture(const AudioFormat&) = 0;
    virtual int  readPeriod(int16_t* dst, size_t frames) = 0;  // blocking
    virtual void closeCapture() = 0;
    virtual int  openPlayback(const AudioFormat&) = 0;
    virtual int  writePeriod(const int16_t* src, size_t frames) = 0;
    virtual void drain() = 0;
    virtual void closePlayback() = 0;
    virtual void setVolume(float gain) = 0;   // 0.0–1.0, nhân trước khi ghi
};
```

> "Period" là đơn vị buffer ALSA xử lý mỗi lần — xem [knowledge/audio_alsa.md](../knowledge/audio_alsa.md).

### 2.2 IDisplayHal — fbdev
Trách nhiệm: `mmap` `/dev/fb0`, flush một vùng RGB565. Không biết bên dưới là fbtft (xem [device_driver.md](device_driver.md)).

```cpp
class IDisplayHal {
public:
    virtual ~IDisplayHal() = default;
    virtual int  init(const char* fbdev) = 0;
    virtual void flush(int x1, int y1, int x2, int y2, const uint16_t* px) = 0;
    virtual int  width()  const = 0;   // 320
    virtual int  height() const = 0;   // 240
};
```

### 2.3 IGpioHal — libgpiod
Trách nhiệm: chờ cạnh (edge) nút bấm có debounce, điều khiển LED.

```cpp
enum class ButtonId { Ptt, VolUp, VolDown };
enum class Edge { Press, Release };
struct GpioEvent { ButtonId id; Edge edge; };

class IGpioHal {
public:
    virtual ~IGpioHal() = default;
    virtual int  init(const GpioPinMap& pins) = 0;
    virtual bool waitEvent(GpioEvent& out, int timeoutMs) = 0;  // blocking
    virtual void setLed(bool on) = 0;
};
```

---

## 3. Nguyên tắc thiết kế interface

1. **Hẹp & theo vai trò** — interface chỉ phơi ra cái tầng trên cần. Không thêm `getRawAlsaHandle()` làm rò rỉ chi tiết.
2. **Không ném exception qua biên** — trả mã lỗi (`int`) hoặc `bool`, để tầng trên gói thành `Result`/Event (xem [coding_guide.md](coding_guide.md) §5).
3. **Blocking nhưng có timeout** — `waitEvent(timeoutMs)`, `readPeriod` chặn theo period. Việc chạy ở thread riêng là chuyện của tầng trên, HAL không tự đẻ thread.
4. **Đơn vị dữ liệu rõ ràng** — `frames` (không phải bytes), RGB565 `uint16_t` (không phải byte buffer mơ hồ). Giảm nhầm lẫn đơn vị.

---

## 4. Đóng gói thành shared library

HAL build thành các shared lib (`libbbb_hal_audio.so`, ...). Lý do:
- Tách biên dịch: sửa impl ALSA không phải build lại app.
- Có thể link mock lib khi test, link lib thật khi deploy — cùng một header.

`CMake` (phác thảo):
```cmake
add_library(bbb_hal_audio SHARED audio/AlsaAudioHal.cpp)
target_include_directories(bbb_hal_audio PUBLIC include)
target_link_libraries(bbb_hal_audio PRIVATE asound)
```

---

## 5. Mock pattern — test không cần phần cứng

Vì tầng trên chỉ giữ con trỏ `IAudioHal*`, ta thay impl thật bằng **mock** lúc test (chạy native trên VM, không cross-compile).

```cpp
// tests/hal/MockAudioHal.hpp
class MockAudioHal : public IAudioHal {
public:
    // Kịch bản nạp sẵn: capture sẽ "trả" dữ liệu này
    std::vector<int16_t> fakeCapture;
    std::vector<int16_t> playbackSink;   // ghi nhận cái app phát ra
    float lastGain = 1.0f;

    int openCapture(const AudioFormat&) override { return 0; }
    int readPeriod(int16_t* dst, size_t frames) override {
        size_t n = std::min(frames, fakeCapture.size());
        std::copy_n(fakeCapture.begin(), n, dst);
        return static_cast<int>(n);
    }
    void closeCapture() override {}
    int openPlayback(const AudioFormat&) override { return 0; }
    int writePeriod(const int16_t* src, size_t frames) override {
        playbackSink.insert(playbackSink.end(), src, src + frames);
        return static_cast<int>(frames);
    }
    void drain() override {}
    void closePlayback() override {}
    void setVolume(float g) override { lastGain = g; }
};
```

Test ví dụ (ý tưởng, dùng framework như GoogleTest/Catch2):
```cpp
TEST(AudioPipeline, AppliesGainBeforePlayback) {
    MockAudioHal hal;
    AudioPipeline pipe(&hal);
    pipe.applyGain(0.5f);
    EXPECT_FLOAT_EQ(hal.lastGain, 0.5f);
}
```

Lợi ích tư duy: ta test được **logic** (FR-8 timeout, áp gain, lắp buffer) tách rời **phần cứng**. Phần phụ thuộc HW chỉ kiểm bằng tay trên board (CHECK_LIST.md). Đây là lý do cốt lõi để có HAL ngay từ đầu, không phải "cho đẹp kiến trúc".

---

## 6. Sai lầm cần tránh

- **Rò rỉ trừu tượng**: trả `snd_pcm_t*` ra ngoài → tầng trên lại phụ thuộc ALSA. Cấm.
- **HAL ôm logic nghiệp vụ**: HAL không quyết định "khi nào dừng ghi" (đó là FR-8 của `AudioPipeline`). HAL chỉ "đọc một period".
- **HAL tự tạo thread**: dễ thành nhiều mô hình thread chồng chéo. Thread do tầng app quản (architecture.md §3).
- **Mock quá thông minh**: mock chỉ cần đủ để dựng kịch bản test, đừng tái hiện cả ALSA.
