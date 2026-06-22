# Environment Setup

> From a fresh Ubuntu 22.04 VM + a BeagleBone Black to a working cross-compile-and-deploy loop. English per Rule §18. Paths follow [prepare.sh](../prepare.sh).

The whole chain is one idea: **the dev VM (x86) builds binaries that run on the BBB (ARM).** That needs two things, and most setup pain comes from wiring them together correctly:

1. **A cross-compiler** that emits ARM code — built once with crosstool-ng (§2).
2. **A sysroot** — a mirror of the board's own libraries + headers, so we link against the *exact* versions that exist on the BBB (§3).

---

## 0. Machines & roles

| Machine | Role |
|---------|------|
| Ubuntu 22.04 VM (dev host) | edit code, build the toolchain, cross-compile, run host-side unit tests |
| BeagleBone Black (target) | runs the binary; Debian 12 IoT, kernel 5.10.x |
| Windows/Linux PC | runs LM Studio (LLM) + Whisper server; reached over the LAN |

Two network links to the BBB (CLAUDE.md §5):
- **USB gadget `192.168.7.2`** — dev/deploy (SSH + rsync). This is what `prepare.sh` targets.
- **RJ45 LAN** — runtime path to the PC servers, plus internet/`apt` on the board.

---

## 1. Dev host packages

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build git rsync \
    gcc g++ pkg-config \
    libncurses-dev bison flex texinfo help2man gawk \
    libtool-bin unzip wget
```

The `ncurses/bison/flex/texinfo/help2man/gawk` set is only for **building crosstool-ng itself** (§2), not the app.

---

## 2. Cross-toolchain (crosstool-ng)

Decision: a **crosstool-ng**-built toolchain rather than a packaged `gcc-arm-linux-gnueabihf`, so the toolchain's glibc/ABI is pinned to the board (CLAUDE.md §11). The source tree lives at `.toolchain/crosstool-ng/` (gitignored).

```bash
cd .toolchain/crosstool-ng
./bootstrap
./configure --enable-local      # build/run ct-ng from this dir, no system install
make -j"$(nproc)"

./ct-ng menuconfig              # Target: arm, cpu cortex-a8, FPU vfpv3, hardfloat
./ct-ng build                   # → installs to ~/x-tools/arm-cortex_a8-linux-gnueabihf
```

Verify:

```bash
source ./prepare.sh             # puts ~/x-tools/.../bin on PATH, sets $BBB_PATH
arm-cortex_a8-linux-gnueabihf-gcc --version
```

> `prepare.sh` must be **sourced** (`source ./prepare.sh`), not executed — `export` in a child shell would not reach your session.

---

## 3. Sysroot from the board

The sysroot is a copy of the board's libs + headers under `~/bbb-sysroot`, so we never guess library versions (CLAUDE.md §11 step 2). It has **two halves**:

- *runtime* — the `.so.6` shared objects. The IoT image ships these.
- *development* — headers (`features.h`, `stdio.h`, …) and the `.so` dev symlinks/scripts. These come from `*-dev` packages; install them on the board **before** pulling (§4 lists them).

### 3.1 Pull it

```bash
mkdir -p ~/bbb-sysroot
rsync -avzR \
    gia@192.168.7.2:/usr/lib \
    gia@192.168.7.2:/usr/include \
    gia@192.168.7.2:/lib \
    ~/bbb-sysroot/
```

Two flags matter, and getting either wrong wasted real time during bring-up:

- **No `sudo`.** Do *not* use `--rsync-path="sudo rsync"`. rsync's SSH session has no TTY, so `sudo` can't prompt for a password and aborts (`connection unexpectedly closed (code 12)`). A sysroot only needs *readable* libs/headers — all world-readable — so plain rsync is enough.
- **`-R` (`--relative`) is mandatory.** Without it, rsync keeps only each source's *basename*: `/usr/include` → `bbb-sysroot/include` (**not** `usr/include`), and `/usr/lib` + `/lib` collapse into one `bbb-sysroot/lib`. The compiler then can't find `features.h` (it expects `<sysroot>/usr/include`). `-R` recreates the full path → the proper FHS layout `usr/lib`, `usr/include`, `lib` that `--sysroot` expects.

> **`code 23` is expected and usually harmless.** Without root, rsync can't read a few files (`cockpit`, `polkit`, …) or set some perms, and exits 23. Only worry if a file *you link against* failed — check with `grep -iE 'denied|failed' <log>`. If a real `.so`/header was denied, re-pull just that with passwordless sudo (add `gia ALL=(ALL) NOPASSWD: /usr/bin/rsync` to `/etc/sudoers.d/rsync` on the board, then `--rsync-path="sudo rsync"`).

### 3.2 Fix absolute paths in linker scripts

Debian's `libc.so` is **not** a library — it's a GNU ld script with **absolute** paths:

```
GROUP ( /lib/arm-linux-gnueabihf/libc.so.6 /usr/lib/arm-linux-gnueabihf/libc_nonshared.a AS_NEEDED ( /lib/ld-linux-armhf.so.3 ) )
```

The crosstool-ng `ld` does not prepend the sysroot to these, so it looks for `/usr/lib/.../libc_nonshared.a` on the **host** and fails (`cannot find .../libc_nonshared.a`). Rewrite the absolute paths to **basenames** so `ld` resolves them via the `-L` paths from §5 instead:

```bash
for f in ~/bbb-sysroot/usr/lib/arm-linux-gnueabihf/libc.so \
         ~/bbb-sysroot/lib/arm-linux-gnueabihf/libc.so; do
    [ -f "$f" ] || continue
    cp "$f" "$f.bak"
    sed -i 's#/usr/lib/arm-linux-gnueabihf/##g; s#/lib/arm-linux-gnueabihf/##g; s#/lib/##g' "$f"
done
# result: GROUP ( libc.so.6 libc_nonshared.a AS_NEEDED ( ld-linux-armhf.so.3 ) )
```

Only `libc.so` needs this on the current image (`libgcc_s.so` already uses basenames). To catch any others after a future pull:

```bash
find ~/bbb-sysroot -name '*.so' -type f -exec sh -c \
  'head -c80 "$1" | grep -q "GNU ld script" && grep -l "GROUP.*/lib/" "$1"' _ {} \;
```

> This edits files **inside** the sysroot, so it does **not** survive a re-pull (§3.1). Re-run it whenever you rebuild the sysroot.

### 3.3 Verify

```bash
ls ~/bbb-sysroot/usr/include/features.h                        # headers landed (proves -R worked)
ls -d ~/bbb-sysroot/usr/include/arm-linux-gnueabihf            # arch-specific headers (bits/, gnu/)
find ~/bbb-sysroot -name 'crt1.o'                              # startup object
ls ~/bbb-sysroot/usr/include/curl/curl.h ~/bbb-sysroot/usr/include/alsa/asoundlib.h
```

If these exist, you're ready to point CMake at the sysroot (§5).

---

## 4. Target (BBB) packages

Install on the board over the RJ45/internet link, **before** the §3.1 pull, so the sysroot gets the dev half:

```bash
sudo apt update
sudo apt install -y \
    libc6-dev linux-libc-dev \
    libasound2-dev \
    libgpiod-dev gpiod \
    libcurl4-openssl-dev \
    espeak-ng \
    alsa-utils                  # arecord/aplay/amixer for bring-up tests
```

- `libc6-dev` + `linux-libc-dev` → `features.h` and the core system headers. Skip these and the cross-build fails at compile time even though the runtime `.so.6` exist.
- the `*-dev` libs → headers + `.so` symlinks for ALSA / GPIO / curl, which the app links against.

LVGL and spdlog/nlohmann-json are vendored or fetched per build (header-only / submodule), not via apt — see the top-level `CMakeLists.txt`.

---

## 5. CMake toolchain file

[`toolchain/bbb-toolchain.cmake`](../toolchain/bbb-toolchain.cmake):

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS arm-cortex_a8-linux-gnueabihf)
set(CMAKE_C_COMPILER   ${CROSS}-gcc)
set(CMAKE_CXX_COMPILER ${CROSS}-g++)

set(CMAKE_SYSROOT $ENV{HOME}/bbb-sysroot)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # host tools
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)    # libs only from sysroot
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
```

---

## 6. Build

```bash
source ./prepare.sh
cmake -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=toolchain/bbb-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build

file build/app/app          # expect: ELF 32-bit LSB executable, ARM, EABI5
```

> **After editing the toolchain file, always `rm -rf build` first.** CMake reads the toolchain file and `*_FLAGS_INIT` only at the *first* configure and bakes the result into `build/`; re-running over an existing `build/` silently ignores your edits.

Host unit tests build *without* the toolchain file (native gcc) so the mock-based suites run on the VM.

---

## 7. Device-tree overlay (display)

The ILI9341 overlay lives at [kernel/overlays/BBB-VOICE-ASSISTANT.dts](../kernel/overlays/BBB-VOICE-ASSISTANT.dts). Build + install with the helper:

```bash
cd kernel/overlays
./copy_dtbo.sh        # dtc compile → /lib/firmware/BBB-VOICE-ASSISTANT.dtbo
```

Enable it in `/boot/uEnv.txt` on the board and reboot:

```
uboot_overlay_addr0=/lib/firmware/BBB-VOICE-ASSISTANT.dtbo
```

Verify `/dev/fb0` appears at 320×240 (see [troubleshooting.md](troubleshooting.md) if not).

---

## 8. Deploy loop

```bash
source ./prepare.sh
scp build/app/app ${BBB_PATH}/                  # scripts/deploy.sh (planned) will wrap this
ssh gia@192.168.7.2 'systemctl --user restart bbb-voice-assistant'
```

`scripts/deploy.sh` (planned) rsyncs the binary + `config/config.json`, then restarts the systemd service.

---

## 9. Sanity checklist

- [ ] `arm-cortex_a8-linux-gnueabihf-gcc --version` works after sourcing `prepare.sh`
- [ ] `~/bbb-sysroot/usr/include/features.h` exists (sysroot pulled with `-R`)
- [ ] `cmake --build build` produces an ARM binary (`file build/app/app → ARM`)
- [ ] `/dev/fb0` present at 320×240
- [ ] `arecord -D plughw:CARD=...,DEV=0 -f S16_LE -r 16000 -c 1 test.wav` records cleanly
- [ ] `curl http://<PC-LAN-IP>:1234/v1/models` reachable from the BBB
- [ ] Whisper server endpoint reachable + path confirmed

---

## Appendix — cross-build errors & root causes

The bring-up hit these in order; each row is a symptom you may see again after rebuilding the sysroot.

| Symptom | Root cause | Fix |
|---------|-----------|-----|
| rsync `code 12`, `a terminal is required` | `sudo` over rsync has no TTY | drop `sudo` (§3.1) |
| `features.h: No such file` | rsync without `-R` put headers in `bbb-sysroot/include` | `-R` (§3.1) |
| `bits/wordsize.h: No such file` | arch-specific headers in multiarch include dir | `-isystem` (§5) |
| `cannot find crt1.o` | startup objects in multiarch lib dir | `-B` (§5) |
| `cannot find -lm` | libs in multiarch lib dir | `-L` (§5) |
| `cannot find .../libc_nonshared.a` | `libc.so` ld-script has absolute paths | basename rewrite (§3.2) |
| edits to toolchain file ignored | stale `build/` cache | `rm -rf build` (§6) |
