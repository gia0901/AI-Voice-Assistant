# CMake toolchain file for the BeagleBone Black (ARM Cortex-A8, hardfloat).
# See .docs/env_setup.md §5 for the full rationale.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS arm-cortex_a8-linux-gnueabihf)
set(CMAKE_C_COMPILER   ${CROSS}-gcc)
set(CMAKE_CXX_COMPILER ${CROSS}-g++)

set(CMAKE_SYSROOT $ENV{HOME}/bbb-sysroot)       # board libs/headers, mirrored on the host
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)    # host tools
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)     # libs only from sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# --- Debian multiarch ----------------------------------------------------
# Debian keeps arch-specific headers, startup objects, and libs under a
# multiarch subdir (arm-linux-gnueabihf/). The crosstool-ng compiler does not
# search there by default, so a bare build fails with one of:
#   "bits/wordsize.h: No such file"  → headers   → -isystem
#   "cannot find crt1.o"             → startup    → -B
#   "cannot find -lm"                → libraries  → -L
# -rpath-link lets ld resolve indirect .so dependencies at link time.
# Use *_FLAGS_INIT (not add_link_options): the latter isn't applied during
# CMake's compiler-test step, so the test would still fail.
set(MULTIARCH arm-linux-gnueabihf)
set(_bbb_flags
    "-isystem ${CMAKE_SYSROOT}/usr/include/${MULTIARCH} \
     -B${CMAKE_SYSROOT}/usr/lib/${MULTIARCH} \
     -L${CMAKE_SYSROOT}/usr/lib/${MULTIARCH} \
     -L${CMAKE_SYSROOT}/lib/${MULTIARCH} \
     -Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/${MULTIARCH} \
     -Wl,-rpath-link,${CMAKE_SYSROOT}/lib/${MULTIARCH}")
set(CMAKE_C_FLAGS_INIT   "${_bbb_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_bbb_flags}")
