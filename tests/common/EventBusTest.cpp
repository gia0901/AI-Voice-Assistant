#include <gtest/gtest.h>
#include "EventBus.hpp"
#include <thread>
#include <vector>
#include <chrono>

using namespace bbb;

// 1. Single-thread tests
TEST(EventBus, PopReturnsNullOptOnTimeoutWhenEmpty) {
    EventBus bus;
    auto ev = bus.pop(5);   // 5ms timeout
    EXPECT_FALSE(ev.has_value());
}

TEST(EventBus, SingleThreadFifoOrder) {
    EventBus bus;
    bus.push(SttResult{"a"});
    bus.push(SttResult{"b"});

    auto ev1 = bus.pop(5);
    auto ev2 = bus.pop(5);

    /* vì Event là variant, nên cần std::get
        để lấy đúng type SttResult
    1. (*ev1)      -> Event trong optional<Event>
    2. std::get    -> chuyển Event thành SttResult
    3. (*ev1).text -> truy cập biến text trong SttResult */
    ASSERT_TRUE(ev1.has_value());
    EXPECT_EQ(std::get<SttResult>(*ev1).text, "a");

    ASSERT_TRUE(ev2.has_value());
    EXPECT_EQ(std::get<SttResult>(*ev2).text, "b");
}

// 2. Multi-thread tests
TEST(EventBus, MultiThreadsEventCount) {
    EventBus bus;
    constexpr int kThreads = 10, kEvents = 1000, kTotal = kThreads * kEvents;

    // 1. Tạo 1 list các producers.
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; t++) {
        producers.emplace_back([&](){   // khi emplace, sẽ tạo ra temp thread và kích hoạt nó chạy
            for (int i = 0; i < kEvents; i++) {
                bus.push(SttResult{"event"});
            }
        });
    }

    // 2. Đợi tất cả producers đẩy hết event vào bus
    for (auto& p : producers) {
        p.join();
    }

    // 3. Pop và kiểm tra xem có đủ số lượng event không
    int eventCount = 0;
    while (bus.pop(1).has_value()) {
        eventCount++;
    }

    EXPECT_EQ(eventCount, kTotal);
}

TEST(EventBus, MultiThreadsEventCountPushPopRandom) {
    EventBus bus;
    constexpr int kThreads = 10, kEvents = 1000, kTotal = kThreads * kEvents;

    // 1. Tạo 1 list các producers.
    std::vector<std::thread> producers;
    using namespace std::chrono_literals;
    for (int t = 0; t < kThreads; t++) {
        producers.emplace_back([&](){   // khi emplace, sẽ tạo ra temp thread và kích hoạt nó chạy
            for (int i = 0; i < kEvents; i++) {
                bus.push(SttResult{"event"});
                std::this_thread::sleep_for(1us);   // sleep 10us mỗi lần push để pop có thể bị trống bất ngờ
            }
        });
    }

    // 2. Pop và kiểm tra xem có đủ số lượng event không
    int eventCount = 0;

    // Cách 1: check liên tục cho đến khi đạt kTotal
    while (eventCount < kTotal) {
        if (bus.pop(10).has_value()) {
            eventCount++;
        }
    }

    // Cách 2: nếu bị nullopt, cố gắng đợi thêm 5000ms, nếu ko có thì give up
    // while (true) {
    //     if (eventCount == kTotal) break;
    //     if (bus.pop(5000).has_value()) {
    //         eventCount++;
    //     }
    //     else {
    //         break;
    //     }
    // }

    // 3. Đợi tất cả producers đẩy hết event vào bus
    for (auto& p : producers) {
        p.join();
    }

    EXPECT_EQ(eventCount, kTotal);
}
