#pragma once

#include "Types.hpp"
#include <gpiod.h>

namespace bbb {

class IGpioHal {
public:
    virtual ~IGpioHal() = default; // Impl should clean the object, not the interface
    virtual int init(const GpioPinMap& pins) = 0;

    virtual bool waitEvent(GpioEvent& out, int timeoutMs) = 0;
    virtual void setLed(bool onoff) = 0;

private:

};
} // namespace bbb