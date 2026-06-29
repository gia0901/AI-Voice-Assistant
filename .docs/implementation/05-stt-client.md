# 05 — SttClient (libcurl → Whisper)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> **Phong cách mục 5:** mỗi hàm theo 3 lớp — **Bản chất** → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
>
> Nền: [../knowledge/ai_server.md](../knowledge/ai_server.md) §3 · [../server_setup.md](../server_setup.md) §2 · CLAUDE.md §5/§9

---

## 1. 🎯 Mục tiêu & vị trí

HTTP client gửi audio → **faster-whisper-server** (`POST /v1/audio/transcriptions`), nhận text. Chạy trên network worker thread, đẩy `SttResult`/`NetworkError` lên bus.

**File:** `middleware/stt_client/SttClient.hpp/.cpp` (và nên có `HttpClient` base dùng chung với [06](06-llm-client.md)).
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

> Nên: phần set timeout, perform, thu response giống nhau. STT và LLM khác ở body (multipart vs JSON) và parse. Tách base tránh lặp. *Nhưng* đừng trừu tượng quá sớm — viết STT trước, thấy LLM lặp thì refactor ([06](06-llm-client.md)).
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
    static Result<std::string> parseBody(const std::string& body);  // tách ra để test không cần mạng
private:
    SttConfig cfg_;
};
} // namespace bbb
```

---

### 5.2 `transcribe()`

**Bản chất.** Một lời gọi HTTP là chu trình **acquire → cấu hình → perform → cleanup**, và vì có nhiều điểm `return` lỗi giữa chừng, nó là **bài toán RAII điển hình**: `CURL*` và `curl_mime*` phải tự huỷ dù thoát ở nhánh nào (gói trong wrapper hoặc `unique_ptr` custom-deleter). Hai ranh giới "không tin được" cần tách bạch: (1) **lỗi truyền tải** (`perform` != OK — server off, timeout) khác (2) **lỗi ứng dụng** (HTTP code != 200, body không có `text`). Việc tách timeout connect/total (Q2) chính là để phân biệt "không tới được server" với "server đang xử lý lâu". Phần parse tách thành hàm `static` thuần để unit-test không cần mạng.

**API toolbox** (libcurl + nlohmann):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `curl_easy_init()` / `curl_easy_cleanup(h)` | Tạo/huỷ easy handle | rò nếu return trước cleanup → RAII |
| `curl_mime_init(h)` + `curl_mime_addpart` + `curl_mime_name/_data/_filedata` | Dựng body multipart/form-data | field `file` cần filename + content-type |
| `curl_easy_setopt(h, OPT, v)` | Set URL/MIMEPOST/CONNECTTIMEOUT_MS/TIMEOUT_MS/WRITEFUNCTION/WRITEDATA | timeout đặt theo **ms** (`*_MS`) |
| `curl_easy_perform(h)` | Thực hiện request (blocking) | trả `CURLE_*`; != OK = lỗi truyền tải |
| `curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &c)` | Lấy HTTP status | 200 mới đọc body như thành công |
| `curl_easy_strerror(rc)` | Chuỗi lỗi người đọc | dùng cho `Error.message` |
| `nlohmann::json::parse(body)` | Body → JSON | bọc try/catch; lấy `j.at("text")` |

**Pseudo:**
```
url = "http://{host}:{port}{path}";  wav = wrapWav(pcm)            // 44B header + samples
h = curl_easy_init()                                              // → gói RAII ngay
mime: addpart file=wav (filename "audio.wav", type "audio/wav"); model=cfg.model; language=cfg.lang
setopt: URL, MIMEPOST=mime, CONNECTTIMEOUT_MS, TIMEOUT_MS, WRITEFUNCTION→gom vào std::string
rc = curl_easy_perform(h)
rc != CURLE_OK            → return Error{rc, curl_easy_strerror(rc)}     // (1) truyền tải
getinfo RESPONSE_CODE; != 200 → return Error{code, body}                 // (2) ứng dụng
return parseBody(body)                                                   // tách hàm test được
```
> **TODO(you):** viết `wrapWav(pcm)` (header WAV PCM 16k mono — tra format chunk) và `WRITEFUNCTION` (gom body vào `std::string`). Gói `CURL*`/`curl_mime*` bằng RAII để không rò khi return giữa chừng — **Claude sẽ soi chỗ này**.

---

### 5.3 `parseBody()` (tách để test không cần mạng)

**Bản chất.** Parse là **biên không tin cậy thứ hai** (sau mạng): JSON có thể thiếu `text`, sai kiểu. Tách thành hàm `static` thuần ⇒ unit-test bằng chuỗi cố định, không cần server.

**Pseudo:** `try { json j = parse(body); return j.at("text").get<string>(); } catch { return Error{...}; }`

---

### 5.4 Khung test
```cpp
TEST_CASE("parse STT response lấy đúng text") {
    auto r = SttClient::parseBody(R"({"text":"hello world"})");
    // TODO(you): assert giữ nhánh std::string == "hello world"
}
// Integration (cần faster-whisper-server chạy): transcribe(wav_mẫu) → text khớp.
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

**Xong khi:** audio thật → text đúng từ server; tắt server → `Error` (không crash/treo); `parseBody` test pass không cần mạng.

**Đưa Claude review:** `SttClient.cpp` + RAII wrapper cho curl. Hỏi:
1. *"`CURL*`/`curl_mime` của tôi có rò khi return lỗi giữa chừng không?"*
2. *"WAV header tôi bọc có đúng để server đọc 16k mono không?"*
3. *"Tách connect/total timeout đã đúng ý đồ chưa?"*
