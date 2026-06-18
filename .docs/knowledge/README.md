# Knowledge Base

> Ghi chú nền tảng/lý thuyết (tiếng Việt theo Rule §18) — phần "vì sao" sâu hơn, tách khỏi tài liệu thực thi trong [../development/](../development/). Mục tiêu: tư duy hệ thống & nền tảng, so sánh giải pháp, lý giải quyết định.

## Mục lục

| Tài liệu | Nội dung |
|----------|----------|
| [strategy_roadmap.md](strategy_roadmap.md) | **Bắt đầu ở đây.** Lộ trình tư duy 2 tuần (Phase A→E): học gì, giải bài toán nào, tự quyết ra sao, khi nào nhờ review. Có mục handoff cho hội thoại mới |
| [threading_eventbus.md](threading_eventbus.md) | Concurrency, race condition, mô hình hàng đợi một-consumer, move qua thread |
| [audio_alsa.md](audio_alsa.md) | PCM, sample rate/bit depth, ALSA `hw`/`plughw`, period/buffer, xrun, software gain |
| [cross_compile.md](cross_compile.md) | Host vs target, toolchain ABI hardfloat, sysroot, vì sao crosstool-ng |

## Sẽ bổ sung khi gặp (gợi ý)

- `spi_displays.md` — sâu hơn về MIPI DBI, so sánh fbtft vs DRM vs spidev (hiện tạm gói trong [../development/device_driver.md](../development/device_driver.md))
- `http_libcurl.md` — easy/multi interface, timeout, vì sao không block main thread
- `lvgl_internals.md` — draw buffer, flush callback, vì sao single-thread
- `systemd_service.md` — unit file, auto-restart, boot integration (NFR-3)

> Quy ước: thêm một file khi một chủ đề lý thuyết đã đủ lớn để tách khỏi tài liệu thực thi. Đừng tạo file rỗng "để dành".
