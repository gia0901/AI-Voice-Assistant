# Toolchain file for Beaglebone Black
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS arm-cortex_a8-linux-gnueabihf)
set(CMAKE_C_COMPILER    ${CROSS}-gcc)
set(CMAKE_CXX_COMPILER  ${CROSS}-g++)

set(CMAKE_SYSROOT $ENV{HOME}/bbb-sysroot)       # BBB sysroot clone in Host machine
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})      # Direct root path to BBB sysroot clone
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)    # use host tools
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)     # libs only from sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)