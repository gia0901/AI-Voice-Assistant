#include <gtest/gtest.h>
#include "EventBus.hpp"
#include <thread>
#include <vector>

using namespace bbb;

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
    EXPECT_EQ(std::get<SttResult>(*ev1).text, "a");
    EXPECT_EQ(std::get<SttResult>(*ev2).text, "b");
}