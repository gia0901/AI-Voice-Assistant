#pragma once
#include "IGpioHal.hpp"
#include <gpiod.h>
#include <chrono>
#include <array>

namespace bbb {

class GpioHal : public IGpioHal {
public:
    GpioHal() = default;
    ~GpioHal() override;                // đóng line và chip
    
    GpioHal(const GpioHal&) = delete;   // không copy fd
    GpioHal& operator=(const GpioHal&) = delete;
    
    int init(const GpioPinMap& pins) override;
    bool waitEvent(GpioEvent& out, int timeoutMs) override;
    void setLed(bool onoff) override;

private:
    static constexpr int kTotalButtons_ = 3;
    gpiod_chip* chip_ = nullptr;    // 3 nút dùng chung gpiochip
    gpiod_chip* led_ = nullptr;

    GpioPinMap pins_{};
    std::array<gpiod_line*, kTotalButtons_> btn_{};
    // debounce: mốc thời gian cạnh gần nhất mỗi nút
    std::array<std::chrono::steady_clock::time_point, kTotalButtons_> lastEdge_{};
};

} // namespace bbb