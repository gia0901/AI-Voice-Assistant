#include "IGpioHal.hpp"
#include "Types.hpp"
#include "Logger.hpp"
#include <iostream>
#include <string>


int main(int argc, char** argv) {
    bbb::Logger::init("debug");
    bbb::Logger::debug("Hello from {}", std::string("Beaglebone Black"));

    return 0;
}
