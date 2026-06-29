#include "GpioHal.hpp"
#include <cstdlib>

namespace bbb {
    
GpioHal::~GpioHal() {

}

int GpioHal::init(const GpioPinMap& pins) {
    // 1. open gpiochip
    chip_ = gpiod_chip_open_by_name(std::string("gpiochip" + std::to_string(pins.ptt.chip)).c_str());
    if (!chip_) {
        return -EINVAL;
    }

    led_ = gpiod_chip_open_by_name(std::string("gpiochip" + std::to_string(pins.led.chip)).c_str());
    if (!led_) {
        return -EINVAL;
    }
    
    // 2. get gpio line

    return 0;
}

bool GpioHal::waitEvent(GpioEvent& out, int timeoutMs)  {
    return true;
}

void GpioHal::setLed(bool onoff)  {
    
}

} // namespace bbb