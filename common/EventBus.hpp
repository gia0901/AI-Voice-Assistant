#pragma once
#include "Types.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>
#include <variant>

namespace bbb {

using Event = std::variant<
    ButtonEvent, RecordingComplete, RecordingTimeout,
    SttResult, LlmResult, NetworkError, PlaybackComplete, TtsFailed>;

class EventBus {
public:
    // push: only for workers
    void push(Event e) {
        std::lock_guard<std::mutex> lck(mutex_); { queue_.push(std::move(e)); }
        cv_.notify_one();   // notify main app
        
    }

    /* pop: only for main app (consumer)
    1. (Lock đã giữ sẵn bởi unique_lock) wait_for kiểm predicate trước.
    2. Predicate thỏa → return ngay, lấy event, lock nhả khi pop() kết thúc. ✅
    3. Predicate không thỏa → nhả lock + ngủ, chờ tới deadline 10ms.
        3.1. Chạm 10ms mà vẫn rỗng → return nullopt. ✅
        3.2. Bị đánh thức trước 10ms → kiểm predicate:
            thỏa → return event ✅
            không thỏa → NGỦ LẠI, chờ tiếp tới hết deadline gốc. Đây là lúc spurious wakeup bị nuốt.
    */
    std::optional<Event> pop(int timeoutMs) {
        std::unique_lock<std::mutex> lck(mutex_);

        bool ok = cv_.wait_for(lck, std::chrono::milliseconds(timeoutMs), [&]() {
            return !queue_.empty();
        });

        if (ok) {
            Event e = std::move(queue_.front());
            queue_.pop();
            return e;
        }
        else {
            return std::nullopt; // No event available to pop
        }
    }

private:
    std::mutex mutex_;
    std::queue<Event> queue_;
    std::condition_variable cv_;
};

} // namespace bbb