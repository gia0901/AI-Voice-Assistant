# AI-Voice Assistant Checklist

# Bring-up phase
### 0. Booting
- [x] Booting được (kernel 5.10 official của BBB)

### 1. Bring-up
- [x] USB-To-Ethernet và SSH hoạt động được
- [x] ILI9341 hoạt động được
- [x] USB-Audio hoạt động được (Loa + Mic)

### 2. Development environment setup
- [x] Setup cross-compile (cross-tool ng)
    - [x] CMake compiled

- [x] Xử lý thư viện, package, dependency
    - [ ] LVGL
    - [x] Alsa
    - [x] json
    - [x] spdlog

### 3. Kickstart!
- [x] Chuẩn bị base structure
- [x] Các thành phần cơ bản phải compile được
- [x] Hello World! Beaglebone Black

### 4. Development
- [ ] common
- [ ] GPIO
    - [ ] Device tree update
    - [ ] Quick test
- [ ] Display
    - [ ] Device tree Touch screen update
- [ ] Audio 
