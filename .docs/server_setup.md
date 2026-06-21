# PC AI Server — Setup (Windows + NVIDIA)

> Operational, step-by-step setup of the two PC-side servers the BBB talks to. English per Rule §18. The *why* behind these choices lives in [knowledge/ai_server.md](knowledge/ai_server.md); topology is CLAUDE.md §5. Target environment: **Windows 11 + NVIDIA RTX 5070Ti (16 GB VRAM)**.

Two servers, two ports:

| Server | Software | Port | Endpoint (OpenAI-compatible) |
|--------|----------|------|------------------------------|
| LLM | LM Studio | `1234` | `POST /v1/chat/completions`, `GET /v1/models` |
| STT | faster-whisper-server | `8000` | `POST /v1/audio/transcriptions` |

---

## 0. Prerequisites

- Windows 11, NVIDIA driver up to date (CUDA 12.x runtime ships with recent drivers).
- The PC and the BBB on the **same LAN** (RJ45). Decide the PC's **static LAN IP** now — call it `<PC_LAN_IP>` below (e.g. `192.168.1.50`).
- ~20 GB free disk for models.

> Throughout: `<PC_LAN_IP>` is the address the BBB will dial. It is **not** `127.0.0.1` and **not** the `192.168.7.x` USB-gadget address.

---

## 1. LM Studio (LLM server)

### 1.1 Install + pick a model
1. Download LM Studio for Windows from lmstudio.ai, install, launch.
2. In **Discover/Search**, download a GGUF model. Recommended balance for 16 GB VRAM (see [knowledge/ai_server.md](knowledge/ai_server.md) §4.3):
   - **Primary:** `Qwen2.5-7B-Instruct` or `Llama-3.1-8B-Instruct`, quant **Q4_K_M**.
   - Optional "smarter": `Qwen2.5-14B-Instruct` Q4_K_M (re-measure latency before keeping).
3. Load the model; set **GPU offload = max** (all layers on GPU — 16 GB is plenty for 7–8B).

### 1.2 Start the server on the LAN
1. Open the **Developer** tab → **Local Server** (or "Start Server").
2. Port: `1234`.
3. Enable **"Serve on Local Network"** (binds `0.0.0.0` instead of localhost) — required so the BBB can reach it.
4. Set a short **system prompt** to keep replies brief (protects NFR-1):
   > "You are a concise voice assistant. Answer in 1–2 short sentences."

### 1.3 Verify locally
```powershell
curl http://localhost:1234/v1/models
curl http://localhost:1234/v1/chat/completions ^
  -H "Content-Type: application/json" ^
  -d "{\"model\":\"local-model\",\"messages\":[{\"role\":\"user\",\"content\":\"Say hi\"}],\"max_tokens\":20}"
```
Expect a JSON reply with `choices[0].message.content`.

---

## 2. faster-whisper-server (STT server)

Two install paths. **Docker is the least painful on Windows for GPU.**

### Option A — Docker (recommended)
1. Install **Docker Desktop** + enable WSL2 backend.
2. Install the **NVIDIA Container Toolkit** so containers see the GPU (NVIDIA's WSL CUDA guide).
3. Run the CUDA image (project repo: `fedirz/faster-whisper-server`, now also published as **Speaches** — confirm the current image tag in its README):
```powershell
docker run --gpus all -p 8000:8000 ^
  -e WHISPER__MODEL=Systran/faster-distil-whisper-large-v3 ^
  -e WHISPER__COMPUTE_TYPE=float16 ^
  fedirz/faster-whisper-server:latest-cuda
```
The model downloads on first run (cached afterwards).

### Option B — Python (pip)
```powershell
python -m venv .venv && .venv\Scripts\activate
pip install faster-whisper-server          # or the 'speaches' package — check current name
# GPU needs CUDA 12 + cuDNN 9 on PATH for CTranslate2
set WHISPER__MODEL=Systran/faster-distil-whisper-large-v3
set WHISPER__COMPUTE_TYPE=float16
faster-whisper-server                       # serves on :8000
```

> Model choice: `distil-large-v3` / `large-v3-turbo` give near-large accuracy much faster for English (rationale: [knowledge/ai_server.md](knowledge/ai_server.md) §3.2). On 16 GB you could run full `large-v3`, but turbo/distil keeps STT inside the NFR-1 budget.

### 2.1 Verify locally
```powershell
# record or grab a 16k mono wav, then:
curl http://localhost:8000/v1/audio/transcriptions ^
  -F "file=@test.wav" ^
  -F "model=Systran/faster-distil-whisper-large-v3" ^
  -F "language=en"
```
Expect JSON containing `"text": "..."`. **Confirm the exact path** for your build — the OpenAI-shaped `/v1/audio/transcriptions` is what `SttClient` will target (CLAUDE.md §5 warns paths differ between Whisper builds).

---

## 3. Expose to the LAN (so the BBB can reach it)

### 3.1 Static IP
Give the PC a static LAN IP or a DHCP reservation. Note it as `<PC_LAN_IP>`.

### 3.2 Windows Firewall — allow inbound, LAN-only
Open the two ports **for the Private/Domain profile only** (never Public). PowerShell (Admin):
```powershell
New-NetFirewallRule -DisplayName "LM Studio 1234" -Direction Inbound ^
  -LocalPort 1234 -Protocol TCP -Action Allow -Profile Private
New-NetFirewallRule -DisplayName "Whisper 8000" -Direction Inbound ^
  -LocalPort 8000 -Protocol TCP -Action Allow -Profile Private
```
Do **not** port-forward these on the router — the servers are unauthenticated (CLAUDE.md §5).

### 3.3 Verify from the BBB
SSH into the BBB (or use its RJ45 link) and:
```bash
curl http://<PC_LAN_IP>:1234/v1/models                 # LLM reachable?
curl http://<PC_LAN_IP>:8000/v1/audio/transcriptions \
     -F file=@/tmp/test.wav -F model=Systran/faster-distil-whisper-large-v3 -F language=en
```
Both answering from the BBB = topology done. If refused: check the server bound to `0.0.0.0` (not localhost) and the firewall rule profile matches your network type.

---

## 4. Wire into the app config

The BBB app reads server addresses from `config/config.json` (planned, CLAUDE.md §7). Use the LAN IP + ports:
```jsonc
{
  "llm":     { "host": "<PC_LAN_IP>", "port": 1234, "path": "/v1/chat/completions", "model": "local-model" },
  "stt":     { "host": "<PC_LAN_IP>", "port": 8000, "path": "/v1/audio/transcriptions",
               "model": "Systran/faster-distil-whisper-large-v3", "language": "en" },
  "timeouts":{ "connect_ms": 3000, "total_ms": 10000 }
}
```
`connect_ms` short (3 s) so "PC not ready" surfaces fast as ERROR (CLAUDE.md §9 / NFR-3).

---

## 5. Daily startup checklist

Before testing the BBB end-to-end, on the PC:
- [ ] LM Studio open, model loaded, Local Server **running** on `:1234`, "Serve on Local Network" ON
- [ ] faster-whisper-server container/process up on `:8000`
- [ ] `curl http://<PC_LAN_IP>:1234/v1/models` answers **from the BBB**
- [ ] Whisper transcription answers **from the BBB**
- [ ] PC IP unchanged (static) — else update `config.json`

> Tip: both servers can be set to auto-start (LM Studio "start server on launch"; Docker `--restart unless-stopped`) so a PC reboot doesn't silently break the BBB.

---

## 6. Troubleshooting

See the network/AI rows in [troubleshooting.md](troubleshooting.md) §5. Most common:
- **Connection refused from BBB, works on PC** → server bound to localhost, or firewall rule on the wrong profile.
- **STT 404 / wrong JSON shape** → endpoint path/model name mismatch; re-check against *your* build (§2.1).
- **STT slow / falls back to CPU** → NVIDIA Container Toolkit not wired; `--gpus all` missing; cuDNN not on PATH (Option B).
- **LLM latency > NFR-1** → smaller model or shorter replies ([knowledge/ai_server.md](knowledge/ai_server.md) §5).
