# BuildRoot Setup Guide

## 1. Clone Buildroot Source
```bash
# Clone buildroot source
git clone https://gitlab.com/buildroot.org/buildroot.git

# Checkout stable version (Bootlin)
git checkout -b bootlin 2025.02.6

```
## 2. Configure and Build
```bash
# Open menu configuration
make menuconfig
```

## 3. How to configure for Beaglebone Black
### Target Options:
- **Target Architecture:** ARM (little endian)
- **Target Architecture Variant:** cortex-A8
- **Application Binary Interfaces:** EABIhf
- **Others:** Default

### Build Options:

### Toolchain:
- **Toolchain Type:** external toolchain
- **Toolchain:** Bootlin toolchains

### System Configuration:
- **System hostname:** BBB

### Kernel:
- **Linux kernel:** Enabled
- **Kernel version:** Custom version - 6.12.47
- **Defconfig name:** omap2plus
- **Build a Device Tree Blob (DTB):** Enabled
    - **In-tree Device Tree Source file names:** ti/omap/am335x-boneblack
- **Needs host OpenSSL:** Enabled

### Target Packages
- **BusyBox:** Enabled
- **Audio and Video applications:**
    - [x] **alsa-utils**
    - [x] **alsa-lib**
- **Debugging, profiling and benchmarks:**
    - **gdb:** Enabled
    - **valgrind:** Enabled
- **Libraries:**
    - **Audio/Sound:**
        - [x] alsa-lib
        - [x] tinyalsa


### Bootloaders
- [x] U-Boot
    - U-Boot Version: 2024.04
    - U-Boot configuration: Using an in-tree board defconfig file 
    - Board defconfig: am335x_evm
    - U-Boot binary format: u-boot.img
    - [x] Install U-Boot SPL Binary image:
        - U-boot SPL/TPL binary image name(s): MLO
    - Custom make options: DEVICE_TREE=am335x-boneblack