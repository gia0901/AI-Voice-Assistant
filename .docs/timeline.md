# Timeline — 4 Week Plan (daily breakdown)

> Expands CLAUDE.md §13 into day-level tasks. English per Rule §18. Checkboxes mirror [CHECK_LIST.md](../CHECK_LIST.md) — that file is the live tracker, this one is the plan of record. Dates are relative (Day n) so they survive schedule slips.

Legend: 🔴 highest-risk / decision-gate task · 🟢 verifiable deliverable

---

## Week 1 — Foundation & bring-up

The riskiest week (open hardware/kernel questions). Front-load the decision gates; do not let them slip into Week 2.

| Day | Tasks | Done-when |
|-----|-------|-----------|
| 1 | 🔴 Display-driver decision gate (CLAUDE.md §4.4). Flash Debian 12 IoT, boot from SD, confirm kernel 5.10.x. | 🟢 `/dev/fb0` exists OR DRM card enumerated — path chosen & recorded in architecture.md |
| 2 | SSH over USB gadget (`192.168.7.2`); bring up RJ45 + static LAN IP. | 🟢 SSH works on both links; `ping` PC over LAN |
| 3 | 🔴 SPI0 + ILI9341 device-tree overlay (`BBB-VOICE-ASSISTANT.dts`), `copy_dtbo.sh`, `/boot/uEnv.txt`. | 🟢 fbtft binds, console/test pattern visible on LCD |
| 4 | USB audio bring-up: enumerate card, `arecord`/`aplay`, `amixer scontrols`. | 🟢 record+playback at 16k/S16_LE/mono; volume control identified |
| 5 | Network reachability to PC: `curl` LM Studio `/v1/models` and the Whisper endpoint. | 🟢 both servers answer from the BBB via `curl` |

Exit criteria: every Week-1 row in CHECK_LIST.md checked; the display path is locked.

---

## Week 2 — HAL layer + build system

| Day | Tasks | Done-when |
|-----|-------|-----------|
| 1 | crosstool-ng toolchain finalized; `prepare.sh` sourced; cross-compile hello-world to BBB. | 🟢 ARM hello-world runs on board |
| 2 | Build sysroot via rsync; write `toolchain/bbb-toolchain.cmake`; top-level CMake skeleton. | 🟢 CMake cross-build of `app/` produces ARM binary |
| 3 | `IAudioHal` + ALSA impl (capture/playback/software gain). | 🟢 HAL records a wav, plays it back on-device |
| 4 | `IDisplayHal` (fbdev mmap + flush) and `IGpioHal` (libgpiod edge-wait + LED). | 🟢 pixel pushed to LCD; button edge logged |
| 5 | Mock HAL implementations + first `tests/hal` unit tests (host build). | 🟢 mock-based tests pass on the VM |

---

## Week 3 — Middleware + App

| Day | Tasks | Done-when |
|-----|-------|-----------|
| 1 | `common/`: Logger (spdlog), Config (nlohmann/json), Types, **EventBus** (variant + mutex/cv). | 🟢 EventBus push/pop unit-tested |
| 2 | `AudioPipeline` (capture assembly + FR-8 timeout) and `TtsEngine` (spawn eSpeak-ng → PCM). | 🟢 record→PCM and text→PCM verified |
| 3 | `SttClient` (Whisper HTTP) and `LlmClient` (LM Studio HTTP) with libcurl + timeouts. | 🟢 audio→text→reply round-trips against the PC |
| 4 | `StateMachine` (7 states, CLAUDE.md §8) + `ButtonController`; wire to EventBus. | 🟢 FSM transition tests pass (fake bus/middleware) |
| 5 | `LvglUi`: per-state screens, response text, error messages. | 🟢 LCD shows INIT→IDLE→LISTENING→… driven by FSM |

---

## Week 4 — Integration & hardening

| Day | Tasks | Done-when |
|-----|-------|-----------|
| 1 | Full pipeline wire-up: PTT → STT → LLM → TTS → playback on real hardware. | 🟢 one spoken question gets a spoken answer |
| 2 | Error handling per CLAUDE.md §9 (server down, eSpeak fail, USB-audio yank, stuck PTT). | 🟢 each failure shows a distinct non-crashing ERROR state |
| 3 | systemd service; survives `systemctl restart` and power cycle; boot-time check (NFR-3). | 🟢 auto-starts on boot, auto-restarts on crash |
| 4 | Measure NFR-1 (latency) and NFR-2 (`free -m` < 200 MB); tune model sizes if over budget. | 🟢 latency < 5s, RAM < 200 MB recorded |
| 5 | Documentation pass (architecture/troubleshooting/knowledge), final acceptance run (CLAUDE.md §14). | 🟢 all Definition-of-Done items met |

---

## Tracking

- **Live status:** [CHECK_LIST.md](../CHECK_LIST.md) (current: Week 1–2 bring-up — booting, SSH, ILI9341, USB-audio done; cross-compile in progress).
- **When a day slips:** move the row, don't delete it; re-confirm the gate it depends on. Week-1 gates block everything downstream, so a slip there is more expensive than a slip in Week 3–4.
