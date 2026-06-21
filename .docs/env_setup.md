# Environment Setup

> How to get from a fresh Ubuntu 22.04 VM + a BeagleBone Black to a working cross-compile-and-deploy loop. English per Rule §18. Commands assume the paths in [prepare.sh](../prepare.sh).

---

## 0. Machines & roles

| Machine | Role |
|---------|------|
| Ubuntu 22.04 VM (dev host) | edit code, cross-compile, build the toolchain, run host-side unit tests |
| BeagleBone Black (target) | runs the binary; Debian 12 IoT, kernel 5.10.x |
| Windows/Linux PC | runs LM Studio (LLM) + Whisper server; reached over the LAN |

Two network links to the BBB (CLAUDE.md §5):
- **USB gadget `192.168.7.2`** — dev/deploy (SSH + rsync). This is what `prepare.sh` targets.
- **RJ45 LAN** — runtime path to the PC servers + internet/`apt`.

---

## 1. Dev host packages

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build git rsync \
    gcc g++ \
    libncurses-dev bison flex texinfo help2man gawk \
    libtool-bin unzip wget \
    pkg-config
```

(The `ncurses/bison/flex/...` set is for building crosstool-ng itself, not the app.)

---

## 2. Cross-toolchain (crosstool-ng)

Decision: a **crosstool-ng**-built toolchain, not a packaged `gcc-arm-linux-gnueabihf`, so glibc/ABI is pinned to the board (CLAUDE.md §11). Source tree lives at `.toolchain/crosstool-ng/` (gitignored).

```bash
cd .toolchain/crosstool-ng
./bootstrap
./configure --enable-local      # build/run ct-ng from this dir, no system install
make -j$(nproc)

# Configure target = ARM Cortex-A8 hardfloat, glibc matching the BBB image.
./ct-ng menuconfig              # Target: arm, cpu cortex-a8, FPU vfpv3, hardfloat
./ct-ng build                   # → installs to ~/x-tools/arm-cortex_a8-linux-gnueabihf
```

Verify:

```bash
source ./prepare.sh             # puts ~/x-tools/.../bin on PATH, sets $BBB_PATH
arm-cortex_a8-linux-gnueabihf-gcc --version
```

> Tip: `prepare.sh` must be **sourced** (`source ./prepare.sh` or `. ./prepare.sh`), not executed — `export` in a child shell would not affect your session.

---

## 3. Sysroot from the board

To avoid ABI guesswork, copy the real libraries/headers off the BBB rather than assuming versions (CLAUDE.md §11 step 2):

```bash
mkdir -p ~/bbb-sysroot
rsync -avz --rsync-path="sudo -S rsync" \
    gia@192.168.7.2:/usr/lib gia@192.168.7.2:/usr/include \
    gia@192.168.7.2:/lib \
    ~/bbb-sysroot/
```

Point CMake at it via `CMAKE_FIND_ROOT_PATH` in `toolchain/bbb-toolchain.cmake` (see §5).

---

## 4. Target (BBB) packages

On the board, over the RJ45/internet link:

```bash
# runtime + dev libs the app links against
sudo apt update
sudo apt install -y \
    libasound2 libasound2-dev \
    libgpiod2 libgpiod-dev gpiod \
    libcurl4 libcurl4-openssl-dev \
    espeak-ng \
    alsa-utils            # arecord/aplay/amixer for bring-up tests
```

LVGL and spdlog/nlohmann-json are vendored or fetched per-build (header-only / submodule), not via apt — see the top-level `CMakeLists.txt` once it exists.

---

## 5. CMake toolchain file

`toolchain/bbb-toolchain.cmake` (planned location):

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS arm-cortex_a8-linux-gnueabihf)
set(CMAKE_C_COMPILER   ${CROSS}-gcc)
set(CMAKE_CXX_COMPILER ${CROSS}-g++)

set(CMAKE_SYSROOT $ENV{HOME}/bbb-sysroot)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # use host tools
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)    # libs only from sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

Configure + build:

```bash
source ./prepare.sh
cmake -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=toolchain/bbb-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Host unit tests build *without* the toolchain file (native gcc) so the mock-based suites run on the VM.

---

## 6. Device-tree overlay (display)

The ILI9341 overlay lives at [kernel/overlays/BBB-VOICE-ASSISTANT.dts](../kernel/overlays/BBB-VOICE-ASSISTANT.dts). Build + install with the helper:

```bash
cd kernel/overlays
./copy_dtbo.sh        # dtc compile → /lib/firmware/BBB-VOICE-ASSISTANT.dtbo
```

Then enable it in `/boot/uEnv.txt` on the board and reboot:

```
uboot_overlay_addr0=/lib/firmware/BBB-VOICE-ASSISTANT.dtbo
```

Verify `/dev/fb0` appears and is 320×240 (see [troubleshooting.md](troubleshooting.md) if not).

---

## 7. Deploy loop

```bash
source ./prepare.sh
scp build/bbb-voice-assistant ${BBB_PATH}/      # or scripts/deploy.sh (planned)
ssh gia@192.168.7.2 'systemctl --user restart bbb-voice-assistant'
```

`scripts/deploy.sh` (planned) wraps this: rsync binary + `config/config.json`, then restart the systemd service.

---

## 8. Sanity checklist (mirror of CHECK_LIST.md)

- [ ] `arm-cortex_a8-linux-gnueabihf-gcc --version` works after sourcing prepare.sh
- [ ] Hello-world C++ cross-compiles and runs on the BBB
- [ ] CMake build of `app/` produces an ARM binary (`file build/... → ARM`)
- [ ] `/dev/fb0` present at 320×240
- [ ] `arecord -D plughw:CARD=...,DEV=0 -f S16_LE -r 16000 -c 1 test.wav` records cleanly
- [ ] `curl http://<PC-LAN-IP>:1234/v1/models` reachable from the BBB
- [ ] Whisper server endpoint reachable + confirmed path
