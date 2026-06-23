# Testing — dựng & chạy unit test (host)

> Tài liệu học tập (tiếng Việt, Rule §18). Cách dựng môi trường test trên **dev VM (x86)** và triết lý test cho dự án. Đây là chỗ các "khung test" trong [../implementation/](../implementation/README.md) và [hal_layer.md](hal_layer.md) trỏ tới.

---

## 1. Triết lý: test cái gì, ở đâu

| Loại | Chạy ở đâu | Test cái gì |
|------|-----------|-------------|
| **Unit test (mock-based)** | **VM (native x86)** | Logic thuần: EventBus, FSM, AudioPipeline (FR-8), parse JSON… — qua mock HAL, **không cần board** |
| **Integration test** | VM hoặc board | Có phụ thuộc thật: HTTP tới server, ALSA tới card |
| **Kiểm tay (manual)** | Board | Phần cứng: LCD, nút, loa/mic ([CHECK_LIST.md](../../CHECK_LIST.md)) |

Hai ý cốt lõi (đã nêu ở [hal_layer.md §5](hal_layer.md), [coding_guide.md §10](coding_guide.md)):
- **Test build *không* dùng CMake toolchain file** → biên dịch native x86, chạy thẳng trên VM. Tách hẳn với cross-build cho board.
- **Mock là chỗ cắm** — mọi phụ thuộc phần cứng nằm sau interface ảo (`IAudioHal`, `IGpioHal`…), test thay bằng mock viết tay. Không cần GMock.
- **Test-first** ở đâu làm được: viết test (đỏ) → code cho xanh.

---

## 2. Framework: GoogleTest

Quyết định: **GoogleTest** (chuẩn công nghiệp, tích hợp CTest, kéo về bằng FetchContent). Mock **viết tay** (đã thiết kế trong các guide), GMock để dành nếu sau cần.

> Đổi sang Catch2/doctest sau rất dễ — chỉ khác cú pháp `TEST`/`ASSERT`. Cấu trúc thư mục + CTest giữ nguyên.

---

## 3. Cấu trúc thư mục

```
tests/
├── CMakeLists.txt          # host build, FetchContent GoogleTest, CTest
├── common/
│   └── EventBusTest.cpp
├── hal/
│   ├── GpioHalTest.cpp     # qua MockGpioHal
│   └── AudioPipelineTest.cpp
└── app/
    └── StateMachineTest.cpp
```
Mock dùng chung (`MockGpioHal.hpp`, `MockAudioHal.hpp`) đặt cạnh test hoặc trong `tests/mocks/`.

---

## 4. Setup build (FetchContent + CTest)

`tests/CMakeLists.txt` — **build độc lập, native, KHÔNG toolchain file**:

```cmake
cmake_minimum_required(VERSION 3.16)
project(bbb_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- GoogleTest qua FetchContent (kéo về lúc configure) ---
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2)        # ghim version
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# Bật sanitizer cho test (bắt UB / use-after-free / data race)
add_compile_options(-Wall -Wextra -g -O1)
option(USE_TSAN "ThreadSanitizer cho test đa luồng" OFF)
if(USE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
else()
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# --- một executable cho mỗi nhóm test ---
add_executable(eventbus_test common/EventBusTest.cpp)
target_include_directories(eventbus_test PRIVATE ${CMAKE_SOURCE_DIR}/../common)
target_link_libraries(eventbus_test PRIVATE GTest::gtest_main)
gtest_discover_tests(eventbus_test)

# TODO(you): thêm add_executable cho hal/app khi tới các guide đó
```

Lệnh chạy (trên VM, **không** source prepare.sh — đây là native build):
```bash
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

> Vì sao tách `build-host/` khỏi `build/` (cross): hai build dùng compiler khác nhau (x86 vs ARM). Trộn thư mục build = cache CMake xung đột. Giữ riêng.

> Đường dẫn include `../common`: common hiện header-only nên chỉ cần include path, không cần link gì. Khi có file `.cpp` thật (HAL/middleware) thì thêm chúng vào `add_executable` hoặc build thành lib rồi link.

---

## 5. Sanitizer — vũ khí chính cho test

- **ASan + UBSan** (mặc định ở trên): bắt use-after-free, buffer overflow, UB — đặc biệt hữu ích cho code có con trỏ/buffer (HAL, PCM).
- **TSan** (`-DUSE_TSAN=ON`): bắt **data race** — *bắt buộc* cho test EventBus/đa luồng. Bật riêng vì TSan và ASan không chạy chung được.

```bash
cmake -S tests -B build-tsan -DUSE_TSAN=ON
cmake --build build-tsan && ctest --test-dir build-tsan --output-on-failure
```

> Mẹo: test đa luồng có thể "may rủi" pass. Chạy **dưới TSan** + **lặp nhiều lần** (`ctest --repeat until-fail:50`) mới đủ tin.

---

## 6. Ví dụ: test EventBus (khung — bạn điền)

```cpp
#include <gtest/gtest.h>
#include "EventBus.hpp"
#include <thread>
#include <vector>
using namespace bbb;

// 1) Đơn luồng: FIFO + nullopt khi rỗng
TEST(EventBus, PopReturnsNulloptOnTimeoutWhenEmpty) {
    EventBus bus;
    auto ev = bus.pop(5);                 // queue rỗng
    EXPECT_FALSE(ev.has_value());         // TODO(you): đúng kỳ vọng?
}

TEST(EventBus, SingleThreadFifoOrder) {
    EventBus bus;
    bus.push(SttResult{"a"});
    bus.push(SttResult{"b"});
    // TODO(you): pop 2 lần, assert đúng thứ tự "a" rồi "b"
    //            (gợi ý: std::get<SttResult>(*ev).text)
}

// 2) Nhiều producer, một consumer: KHÔNG mất event
TEST(EventBus, MultiProducerNoLostEvents) {
    EventBus bus;
    constexpr int kThreads = 4, kPer = 1000, kTotal = kThreads * kPer;

    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t)
        producers.emplace_back([&]{
            for (int i = 0; i < kPer; ++i) bus.push(RecordingTimeout{});
        });

    int got = 0;
    // TODO(you): consumer pop tới khi đủ kTotal.
    //   Cẩn thận điều kiện dừng: pop(timeout) trả nullopt khi tạm hết —
    //   đừng thoát sớm khi producer chưa push xong. (gợi ý: join producer
    //   trước, hoặc đếm tới đúng kTotal rồi mới dừng.)
    for (auto& p : producers) p.join();

    EXPECT_EQ(got, kTotal);               // không mất, không thừa
}
```
> TODO(you): hoàn thiện 3 TODO. Đặc biệt **điều kiện dừng của consumer** trong test 2 là chỗ dễ sai (thoát sớm khi `nullopt` tạm thời) — chính là kiến thức "miss notify khi queue có thể còn item" bạn vừa nắm. Chạy nó dưới **TSan** + lặp.

---

## 7. Checklist

- [ ] `cmake -S tests -B build-host` configure được (FetchContent kéo GoogleTest)
- [ ] `ctest --test-dir build-host` chạy, test đỏ trước khi code (test-first)
- [ ] EventBus: test đơn luồng + đa luồng **pass dưới ASan**
- [ ] Test đa luồng **pass dưới TSan**, lặp `--repeat until-fail:50` không vỡ
- [ ] Mỗi component mới → thêm `add_executable` + test tương ứng

---

## 8. Liên quan
- Mock pattern: [hal_layer.md §5](hal_layer.md)
- Luật code/sanitizer: [coding_guide.md §10](coding_guide.md)
- Khung test từng component: mục 5 trong mỗi [../implementation/](../implementation/README.md) guide
