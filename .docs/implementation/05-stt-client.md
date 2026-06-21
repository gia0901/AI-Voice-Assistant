# 05 — SttClient (libcurl → Whisper)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> Nền: [../knowledge/ai_server.md](../knowledge/ai_server.md) §3 · [../server_setup.md](../server_setup.md) §2 · CLAUDE.md §5/§9

---

## 1. 🎯 Mục tiêu & vị trí

HTTP client gửi audio → **faster-whisper-server** (`POST /v1/audio/transcriptions`), nhận text. Chạy trên network worker thread, đẩy `SttResult`/`NetworkError` lên bus.

**File:** `middleware/stt_client/SttClient.hpp/.cpp` (và nên có `HttpClient` base dùng chung với guide 06).
**Phụ thuộc:** libcurl, nlohmann/json, `EventBus`+`Config`.

> ⚠️ **Xác nhận endpoint thật trước khi code** — `curl` server của bạn (server_setup §2.1). Đây là Rủi ro #1 (CLAUDE.md Risk Register).

---

## 2. 📜 Hợp đồng

```cpp
class SttClient {
public:
    SttClient(SttConfig cfg);
    Result<std::string> transcribe(const PcmBuffer& pcm);   // blocking; gọi trên worker
};
```
**Ràng buộc:** **không** gọi trên main thread (block mạng); connect timeout ngắn (~3s) để bắt "server chưa bật" (NFR-3/§9); trả `Result`, worker gói thành event.

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Gửi raw PCM hay WAV?</summary>

> Server thường cần một container nhận diện được (WAV header). PCM 16k mono → bọc WAV header 44 byte là đủ. *Xác nhận* server chấp nhận gì (multipart field `file`). Đừng giả định gửi raw chạy.
</details>

<details><summary><b>Q2.</b> connect timeout vs total timeout — đặt riêng?</summary>

> Có. `CURLOPT_CONNECTTIMEOUT` ngắn (~3s, phát hiện server off nhanh) tách khỏi `CURLOPT_TIMEOUT` tổng (dài hơn, cho STT xử lý). Một timeout duy nhất sẽ hoặc cắt oan mạng chậm, hoặc treo lâu khi server off.
</details>

<details><summary><b>Q3.</b> Khởi tạo libcurl — global ở đâu?</summary>

> `curl_global_init(CURL_GLOBAL_ALL)` gọi **một lần** lúc khởi động app (không phải mỗi request, không phải mỗi thread). Mỗi request tạo/destroy một `CURL*` easy handle (hoặc tái dùng).
</details>

<details><summary><b>Q4.</b> Tách <code>HttpClient</code> base chung với LlmClient?</summary>

> Nên: phần set timeout, perform, thu response giống nhau. STT và LLM khác ở body (multipart vs JSON) và parse. Tách base tránh lặp. *Nhưng* đừng trừu tượng quá sớm — viết STT trước, thấy LLM lặp thì refactor (guide 06).
</details>

**⚖️ Bạn tự chốt:** format gửi (WAV/raw — theo server), giá trị timeout.

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | `curl_global_init` + easy handle dựng/hủy | build + link libcurl OK |
| 2 | bọc WAV header quanh PcmBuffer | file.wav phát được bằng aplay |
| 3 | multipart POST tới server | server trả JSON có `text` |
| 4 | parse `json["text"]` → `Result` | text đúng câu nói |
| 5 | timeout/lỗi → `Result<Error>` | tắt server → trả Error, không treo |

---

## 5. 🧩 Khung code tự điền

### 5.1 `SttClient.hpp` — *skeleton*
```cpp
#pragma once
#include "Types.hpp"
#include <curl/curl.h>
#include <string>
namespace bbb {
struct SttConfig { std::string host; int port; std::string path, model, lang;
                   int connectMs=3000, totalMs=10000; };
class SttClient {
public:
    explicit SttClient(SttConfig c) : cfg_(std::move(c)) {}
    Result<std::string> transcribe(const PcmBuffer& pcm);
private:
    SttConfig cfg_;
};
} // namespace bbb
```

### 5.2 Thuật toán `transcribe`
```
url = "http://" + host + ":" + port + path
wav = wrapWav(pcm)                 // 44-byte header + samples
curl = curl_easy_init()
dựng mime (curl_mime_init):
    field "file"  → data wav (filename "audio.wav", type "audio/wav")
    field "model" → cfg.model
    field "language" → cfg.lang
set: URL, MIMEPOST, CONNECTTIMEOUT_MS, TIMEOUT_MS, WRITEFUNCTION→string
rc = curl_easy_perform(curl)
nếu rc != CURLE_OK → return Error{rc, curl_easy_strerror(rc)}
lấy HTTP code; nếu != 200 → return Error{code, body}
json j = parse(body); return j["text"].get<string>()
cleanup mime + handle  (RAII tốt hơn: wrapper tự hủy)
```
> TODO(you): viết `wrapWav(pcm)` (header WAV PCM 16k mono — tra format chunk). Và `WRITEFUNCTION` callback gom body vào std::string. Bọc `CURL*`/`curl_mime*` bằng RAII để khỏi rò khi return giữa chừng — **Claude sẽ soi chỗ này**.

### 5.3 Khung test (cần server thật hoặc mock HTTP)
```cpp
// Integration test (cần faster-whisper-server chạy):
//   transcribe(wav_mẫu) → text khớp.
// Unit test thuần: tách parseResponse(json) ra hàm riêng để test không cần mạng.
TEST_CASE("parse STT response lấy đúng text") {
    auto r = SttClient::parseBody(R"({"text":"hello world"})");
    // TODO(you): assert == "hello world"
}
```

---

## 6. ⚠️ Cạm bẫy ([troubleshooting.md §5](../troubleshooting.md))

- **Đoán endpoint path** → 404. Xác nhận với server thật trước.
- **Gọi trên main thread** → UI đứng khi mạng chậm. Phải worker thread.
- **Không tách connect/total timeout** → hoặc cắt oan, hoặc treo lâu.
- **Rò `CURL*`/mime** khi return lỗi giữa chừng → RAII.
- **`curl_global_init` mỗi request** → không thread-safe + chậm. Một lần lúc khởi động.
- **Gửi raw PCM** server không nhận → bọc WAV.

---

## 7. ✅ Checkpoint review

**Xong khi:** audio thật → text đúng từ server; tắt server → `Error` (không crash/treo); `parseResponse` test pass không cần mạng.

**Đưa Claude review:** `SttClient.cpp` + RAII wrapper cho curl. Hỏi:
1. *"`CURL*`/`curl_mime` của tôi có rò khi return lỗi giữa chừng không?"*
2. *"WAV header tôi bọc có đúng để server đọc 16k mono không?"*
3. *"Tách connect/total timeout đã đúng ý đồ chưa?"*
