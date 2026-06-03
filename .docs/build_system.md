# BBB Voice Assistant - Build System Guide

> **Buildroot-based cross-compilation** for Beaglebone Black on **Windows WSL2 (Ubuntu 22.04)**

## 📋 Overview

| Aspect | Detail |
|--------|--------|
| **Build system** | Buildroot |
| **Host OS** | Windows WSL2 with Ubuntu 22.04 LTS |
| **Target** | Beaglebone Black (AM335x ARMv7) |
| **Toolchain** | arm-linux-gnueabihf (hardfloat) |
| **Output** | Bootable SD card image (with kernel, rootfs, bootloader) |
| **Workflow** | Buildroot → Linux kernel (device tree) → Custom HAL libs → App |

---

## 1. Prerequisites & Host Setup

### 1.1 WSL2 + Ubuntu 22.04 Setup

```bash
# On Windows (PowerShell as Admin)
wsl --install -d Ubuntu-22.04
```

### 1.2 Install Build Dependencies

```bash
# Update package lists
sudo apt-get update && sudo apt-get upgrade -y

# Install essential build tools
sudo apt-get install -y \
    build-essential \
    git \
    bc \
    bison \
    flex \
    libssl-dev \
    libncurses-dev \
    pkg-config \
    cmake \
    wget \
    cpio \
    u-boot-tools \
    rsync \
    dosfstools \
    mtools \
    unzip

# For Buildroot specifically
sudo apt-get install -y \
    qemu-user-static \
    binfmt-support \
    libelf-dev
```

### 1.3 Verify Toolchain Version

```bash
gcc --version  # Should be >= 9.x
```

---

## 2. Buildroot Installation & Configuration

### 2.1 Download & Extract Buildroot

```bash
# Create workspace
mkdir -p ~/bbb-workspace && cd ~/bbb-workspace

# Download latest LTS or stable Buildroot
wget https://buildroot.org/downloads/buildroot-2024.02.tar.gz
tar xzf buildroot-2024.02.tar.gz
cd buildroot-2024.02
```

### 2.2 Initial Configuration (defconfig)

Buildroot provides a default BBB config:

```bash
# Load Beaglebone Black default config
make beaglebone_defconfig

# Customize via menuconfig
make menuconfig
```

### 2.3 Key Configuration Options

Navigate in `menuconfig` and enable/disable:

**Target Options:**
- Architecture: `ARM (little endian)`
- Target Architecture Variant: `cortex-A8`
- Target ABI: `EABIhf` (hardfloat)
- Floating point: `VFPv3-D16`

**Toolchain:**
- Toolchain type: `External` (or `Buildroot` for self-built)
- Binutils version: `2.40+`
- GCC version: `12.x` or `13.x`

**System Configuration:**
- Hostname: `bbb-voice-assistant`
- System banner: Add project name
- Root password: Set or leave empty for SSH key auth

**Kernel:**
- Kernel version: `6.x` (recent stable)
- Custom kernel patches: Yes (for device tree customization)
- Device tree source directory: Point to custom overlays

**Filesystem:**
- Root filesystem type: `ext4` (or `squashfs` for read-only)
- Root filesystem size: `512 MB` (adjust as needed)
- Target device nodes: Use `devtmpfs` + `udev`

**Package Selection (menuconfig → Target packages):**
- C library: `glibc` (required for ALSA, LVGL)
- Development tools: `pkg-config`
- System utilities: `udev`, `systemd` (optional)
- Audio/Video:
  - `alsa-lib` ✓
  - `alsa-utils` ✓
  - `pulseaudio` (optional, heavier)
- Graphics/UI:
  - `lvgl` (if packaged, else manual)
  - `libdrm` (for display backend)
  - `mesa` (for GPU acceleration, if available)
- Networking:
  - `curl` (HTTP client for LM Studio API)
  - `openssl` (for HTTPS)
- Development:
  - `cmake` ✓
  - `git` ✓ (useful on target for development)

**Example `.config` snippet:**
```ini
BR2_ARM_EABIHF=y
BR2_ARM_FPU_VFPV3_D16=y
BR2_TOOLCHAIN_EXTERNAL=y
BR2_KERNEL_LINUX_SITE_METHOD="git"
BR2_LINUX_KERNEL_VERSION="6.6.x"
BR2_TARGET_ROOTFS_EXT4=y
BR2_TARGET_ROOTFS_EXT4_SIZE="512M"
BR2_PACKAGE_ALSA_LIB=y
BR2_PACKAGE_ALSA_UTILS=y
BR2_PACKAGE_CURL=y
```

---

## 3. Custom Kernel & Device Tree

### 3.1 Kernel Source & Device Tree Location

```bash
# After `make menuconfig`, Buildroot will download kernel to:
build/linux-6.6.x/

# Device tree files:
build/linux-6.6.x/arch/arm/boot/dts/

# Default BBB .dts:
build/linux-6.6.x/arch/arm/boot/dts/am335x-boneblack.dts
```

### 3.2 Create Custom Device Tree Overlay

For I2C audio codec, SPI LCD, GPIO buttons:

**File: `board/beaglebone/am335x-boneblack-audio-lcd.dts`**

```dts
/*
 * Custom device tree overlay for BBB Voice Assistant
 * I2C audio codec + SPI LCD + GPIO
 */

/dts-v1/;
/plugin/;

&i2c0 {
    clock-frequency = <400000>;
    
    wm8960: audio-codec@1a {
        compatible = "wlf,wm8960";
        reg = <0x1a>;
        #sound-dai-cells = <0>;
        wlf,gpio-cfg = <0x18>;  // GPIO5 for jack detect (optional)
    };
};

&spi0 {
    status = "okay";
    cs-gpios = <&gpio0 5 0>;  // CS on GPIO0_5
    
    display@0 {
        compatible = "ilitek,ili9341";
        reg = <0>;
        spi-max-frequency = <16000000>;
        dc-gpios = <&gpio1 14 0>;     // Data/Command pin
        reset-gpios = <&gpio1 13 0>;  // Reset pin
        backlight-gpios = <&gpio0 26 0>; // Backlight PWM
    };
};

&gpio0 {
    ptt_btn {
        gpio-hog;
        gpios = <3 0>;   // GPIO0_3
        input;
        line-name = "ptt-button";
    };
    
    vol_up {
        gpio-hog;
        gpios = <4 0>;   // GPIO0_4
        input;
        line-name = "volume-up";
    };
    
    vol_down {
        gpio-hog;
        gpios = <24 0>;  // GPIO0_24
        input;
        line-name = "volume-down";
    };
};
```

### 3.3 Apply Device Tree in Build

Edit `.config`:
```ini
BR2_LINUX_KERNEL_CUSTOM_DTS_PATH="../board/beaglebone/am335x-boneblack-audio-lcd.dts"
```

Or copy to Buildroot after extraction:
```bash
cp board/beaglebone/am335x-boneblack-audio-lcd.dts \
   build/linux-6.6.x/arch/arm/boot/dts/

# Add to arch/arm/boot/dts/Makefile
echo "dtb-\$(CONFIG_ARCH_MULTIPLATFORM) += am335x-boneblack-audio-lcd.dtb" \
    >> build/linux-6.6.x/arch/arm/boot/dts/Makefile
```

---

## 4. Build Process

### 4.1 Full Build (Kernel + Rootfs + Bootloader)

```bash
cd ~/bbb-workspace/buildroot-2024.02

# Full rebuild (clean)
make clean
make -j$(nproc)

# Incremental rebuild (after config changes)
make -j$(nproc)

# Build specific component (optional)
make linux-rebuild        # Rebuild kernel only
make linux-reconfigure    # Reconfigure kernel with menuconfig
make alsa-lib-rebuild     # Rebuild specific package
```

### 4.2 Build Output

```
output/
├── build/              # Temporary build artifacts
├── host/               # Host tools (arm-linux-gnueabihf)
├── images/             # Final bootable images
│   ├── am335x-boneblack.dtb           # Device tree blob
│   ├── zImage                         # Compressed kernel
│   ├── rootfs.ext4                    # Root filesystem
│   ├── rootfs.tar                     # Rootfs tarball
│   └── u-boot.img (optional)          # Bootloader
└── staging/            # Staging area (cross-compilation libs)
```

### 4.3 Build Logs & Troubleshooting

```bash
# Check build log
tail -50 output/build/linux-6.6.x/.log

# Clean rebuild if issues occur
make distclean  # Remove everything
make menuconfig # Reconfigure
make -j$(nproc)
```

---

## 5. Image Deployment to SD Card

### 5.1 Prepare SD Card

```bash
# Identify SD card (e.g., /dev/sdb)
lsblk

# Unmount all partitions
sudo umount /media/*/bbb* 2>/dev/null || true

# Create partition table
sudo parted /dev/sdb --script \
    mklabel msdos \
    mkpart primary fat16 1MiB 100MiB \
    mkpart primary ext4 100MiB 100%

# Format partitions
sudo mkfs.vfat -F 16 -n "BOOT" /dev/sdb1
sudo mkfs.ext4 -F -L "rootfs" /dev/sdb2
```

### 5.2 Deploy Root Filesystem

```bash
# Mount rootfs partition
sudo mkdir -p /mnt/rootfs
sudo mount /dev/sdb2 /mnt/rootfs

# Extract Buildroot rootfs
sudo tar xf output/images/rootfs.tar -C /mnt/rootfs

# Set correct permissions
sudo chown -R root:root /mnt/rootfs
```

### 5.3 Deploy Kernel & Device Tree

```bash
# Mount boot partition
sudo mkdir -p /mnt/boot
sudo mount /dev/sdb1 /mnt/boot

# Copy kernel + DTB
sudo cp output/images/zImage /mnt/boot/
sudo cp output/images/am335x-boneblack.dtb /mnt/boot/
sudo cp output/images/am335x-boneblack-audio-lcd.dtb /mnt/boot/ 2>/dev/null || true

# Create uEnv.txt for U-Boot
cat | sudo tee /mnt/boot/uEnv.txt << 'EOF'
bootloader_delay=0
consoledevice=ttyO0
console=ttyO0,115200n8
vram=16M
netargs=setenv bootargs console=${console} root=/dev/mmcblk0p2 rw rootwait
uenvcmd=run netargs; run findfdt; fatload mmc 0 0x80200000 zImage; fatload mmc 0 0x82000000 am335x-boneblack.dtb; bootz 0x80200000 - 0x82000000
EOF
```

### 5.4 Sync & Eject

```bash
# Sync filesystems
sudo sync

# Unmount
sudo umount /mnt/boot /mnt/rootfs

# Eject SD card
sudo eject /dev/sdb
```

---

## 6. Development Workflow

### 6.1 Cross-Compile Custom HAL Libraries

After Buildroot build completes, use the cross-toolchain:

```bash
export PATH="~/bbb-workspace/buildroot-2024.02/output/host/bin:$PATH"
export CROSS_COMPILE=arm-linux-gnueabihf-
export CC=arm-linux-gnueabihf-gcc
export CXX=arm-linux-gnueabihf-g++

# Build custom HAL library (example)
cd ~/projects/bbb-voice-assistant/src/hal

mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j$(nproc)

# Install to staging area
make install DESTDIR="~/bbb-workspace/buildroot-2024.02/output/staging"
```

**toolchain.cmake example:**
```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

set(CMAKE_FIND_ROOT_PATH "/home/user/bbb-workspace/buildroot-2024.02/output/staging")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### 6.2 Incremental Updates to Running BBB

After building updates:

```bash
# Mount rootfs from SD or via NFS
scp output/images/zImage root@bbb:/boot/
scp output/staging/usr/lib/libhal*.so* root@bbb:/usr/lib/

# Or rebuild Buildroot and reflash
```

### 6.3 Rebuild After .config Changes

```bash
# If only app code changed (no Buildroot packages)
make app-rebuild

# If Buildroot packages changed
make clean
make -j$(nproc)  # Full rebuild
```

---

## 7. Kernel Configuration & Drivers

### 7.1 Enable Required Kernel Modules

```bash
make linux-menuconfig  # Inside Buildroot

# Navigate to:
# Device Drivers → Sound card support → ALSA
#   ├─ ALSA I2C sound device support ✓
#   ├─ I2C ALSA device register ✓
#   └─ ALSA ASoC device support ✓
# Device Drivers → I2C support
#   ├─ I2C device interface ✓
# Device Drivers → SPI support
#   ├─ SPI master controller drivers
#   │   └─ OMAP SPI master controller ✓
# Device Drivers → GPIO support
#   ├─ GPIO character device support (cdev) ✓
# Device Drivers → USB support
#   ├─ USB core ✓
#   ├─ USB device filesystem ✓
```

### 7.2 Verify Built Modules

```bash
ls output/build/linux-6.6.x/drivers/{spi,i2c,sound}/*.ko
```

---

## 8. Build Troubleshooting

| Issue | Cause | Fix |
|-------|-------|-----|
| **Permission denied** | Staging dir permissions | `sudo chown -R $USER output/staging` |
| **Toolchain not found** | PATH not set | Export `CROSS_COMPILE` env vars |
| **Kernel build fails** | Missing headers | `apt-get install linux-headers-*` |
| **SD card write fails** | Partition table broken | Use `parted` to recreate partitions |
| **BBB doesn't boot** | Wrong U-Boot config | Check `uEnv.txt`, reflash bootloader |
| **Module load fails** | Kernel mismatch | Ensure kernel version matches modules |

**Debug boot logs:**
```bash
# Connect serial (USB-TTL converter)
# On Linux host:
screen /dev/ttyUSB0 115200
# Or use picocom: picocom -b 115200 /dev/ttyUSB0
```

---

## 9. Repository Structure (Recommended)

```
97_AI_Voice_Assistant/
├── .docs/
│   ├── PLAN.md
│   ├── build_system.md  (this file)
│   ├── architecture.md
│   ├── hardware_setup.md
│   └── timeline.md
├── buildroot-custom/
│   ├── board/
│   │   └── beaglebone/
│   │       ├── am335x-boneblack-audio-lcd.dts
│   │       └── custom.config
│   └── patches/
│       └── linux/
│           └── 0001-custom-drivers.patch
├── src/
│   ├── CMakeLists.txt
│   ├── hal/
│   │   ├── audio/
│   │   ├── display/
│   │   └── gpio/
│   ├── middleware/
│   └── app/
├── toolchain.cmake
└── Makefile  (convenience wrapper)
```

---

## 10. Quick Build & Deploy Script

**File: `scripts/build_and_deploy.sh`**

```bash
#!/bin/bash
set -e

BUILDROOT_DIR=~/bbb-workspace/buildroot-2024.02
SD_CARD_DEV=${1:-/dev/sdb}
IMAGE_PATH=$BUILDROOT_DIR/output/images

echo "🔨 Building Buildroot..."
cd $BUILDROOT_DIR
make -j$(nproc)

echo "📝 Preparing SD card at $SD_CARD_DEV..."
sudo parted $SD_CARD_DEV --script \
    mklabel msdos \
    mkpart primary fat16 1MiB 100MiB \
    mkpart primary ext4 100MiB 100%

sudo mkfs.vfat -F 16 -n "BOOT" ${SD_CARD_DEV}1
sudo mkfs.ext4 -F -L "rootfs" ${SD_CARD_DEV}2

echo "📂 Deploying rootfs..."
sudo mkdir -p /mnt/rootfs
sudo mount ${SD_CARD_DEV}2 /mnt/rootfs
sudo tar xf $IMAGE_PATH/rootfs.tar -C /mnt/rootfs
sudo chown -R root:root /mnt/rootfs

echo "📦 Deploying kernel & DTB..."
sudo mkdir -p /mnt/boot
sudo mount ${SD_CARD_DEV}1 /mnt/boot
sudo cp $IMAGE_PATH/zImage $IMAGE_PATH/*.dtb /mnt/boot/

echo "🎬 Creating uEnv.txt..."
cat | sudo tee /mnt/boot/uEnv.txt << 'EOF'
bootloader_delay=0
consoledevice=ttyO0
console=ttyO0,115200n8
uenvcmd=run netargs; run findfdt; fatload mmc 0 0x80200000 zImage; fatload mmc 0 0x82000000 am335x-boneblack.dtb; bootz 0x80200000 - 0x82000000
EOF

echo "✅ Syncing & ejecting..."
sudo sync
sudo umount /mnt/boot /mnt/rootfs
sudo eject $SD_CARD_DEV

echo "✨ Done! Insert SD card into BBB and power on."
```

Usage:
```bash
chmod +x scripts/build_and_deploy.sh
./scripts/build_and_deploy.sh /dev/sdb
```

---

## 11. Next Steps

- [ ] Customize device tree for audio codec + SPI LCD + GPIO
- [ ] Add ALSA UCM (Use Case Manager) config for I2C codec
- [ ] Cross-compile HAL libraries (libasound wrapper, SPI driver, GPIO)
- [ ] Set up systemd service for app startup
- [ ] Create incremental update mechanism (delta images)

---

## References

- [Buildroot Official](https://buildroot.org/)
- [Beaglebone Black @ elinux.org](https://elinux.org/Beagleboard:BeagleBoneBlack)
- [Linux Device Tree @ kernel.org](https://www.kernel.org/doc/html/latest/devicetree/)
- [ALSA @ kernel.org](https://www.kernel.org/doc/html/latest/sound/)

