#pragma once

#include "Types.hpp"

#include <gpiod.h>

enum class ButtonId {
    Ptt,    // Push-to-talk
    VolUp,
    VolDown,
};

enum class Edge {
    Press,
    Release
};

struct GpioEvent {
    ButtonId id;
    Edge edge;
};

struct GpioPinMap {
    std::string chipName; // Ex: gpiochip0
    std::string lineName; // Ex: pin_16
};

class IGpioHal {
public:
    virtual ~IGpioHal() = default; // Impl should clean the object, not the interface
    virtual int init(const GpioPinMap& pins) = 0;

    virtual bool waitEvent(GpioEvent& out, int timeoutMs) = 0;
    virtual void setLed(bool onoff) = 0;

private:

};