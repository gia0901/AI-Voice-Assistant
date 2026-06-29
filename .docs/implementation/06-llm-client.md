# 06 — LlmClient (libcurl → LM Studio)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> **Phong cách mục 5:** mỗi hàm theo 3 lớp — **Bản chất** → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
>
> Nền: [../knowledge/ai_server.md](../knowledge/ai_server.md) §4/§5 · [../server_setup.md](../server_setup.md) §1 · CLAUDE.md §5/§9. **Làm sau [05-stt-client.md](05-stt-client.md)** (tái dùng HttpClient base).

---

## 1. 🎯 Mục tiêu & vị trí

HTTP client gửi text → **LM Studio** (`POST /v1/chat/completions`, OpenAI-compatible), nhận reply. Chạy trên network worker, đẩy `LlmResult`/`NetworkError`.

**File:** `middleware/llm_client/LlmClient.hpp/.cpp` (+ `HttpClient` base chung với STT).
**Phụ thuộc:** libcurl, nlohmann/json, `EventBus`+`Config`.

---

## 2. 📜 Hợp đồng

```cpp
class LlmClient {
public:
    LlmClient(LlmConfig cfg);
    Result<std::string> chat(const std::string& userText);   // blocking; worker thread
};
```
**Ràng buộc:** worker thread; timeout connect ngắn / total dài hơn STT (LLM sinh token lâu hơn); trả `Result`.

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Body JSON tối thiểu gửi LM Studio gồm gì?</summary>

> ```json
> {"model":"local-model","messages":[
>   {"role":"system","content":"<giữ câu trả lời ngắn>"},
>   {"role":"user","content":"<userText>"}],
>  "max_tokens":120,"temperature":0.7}
> ```
> `model` LM Studio thường bỏ qua (chạy model đang load) nhưng cứ điền. System prompt ngắn gọn **bảo vệ NFR-1** (ai_server §4.4/§5).
</details>

<details><summary><b>Q2.</b> Vì sao total timeout của LLM dài hơn STT?</summary>

> LLM sinh tuần tự từng token; câu dài → lâu hơn STT. Nhưng vẫn phải hữu hạn để không treo UI. Cân với NFR-1 (tổng <5s). Nếu hay vượt → model nhỏ hơn / max_tokens nhỏ hơn (ai_server §5).
</details>

<details><summary><b>Q3.</b> Parse response — lấy field nào?</summary>

> `json["choices"][0]["message"]["content"]`. Phải phòng `choices` rỗng / thiếu field → trả Error thay vì ném. Tách `parseBody()` ra hàm test được không cần mạng.
</details>

<details><summary><b>Q4.</b> Refactor HttpClient base — giờ là lúc chứ?</summary>

> Có. Sau khi viết STT (05), phần `perform + timeout + thu body + RAII handle` lặp lại. Rút ra `HttpClient::post(url, headers, body) -> Result<string>`; STT/LLM chỉ khác cách *dựng body* và *parse*. Refactor *sau khi* thấy lặp thật, không trước.
</details>

**⚖️ Bạn tự chốt:** system prompt, max_tokens, temperature, total timeout.

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | (nếu refactor) `HttpClient::post` base | STT vẫn pass sau refactor |
| 2 | dựng JSON body (nlohmann) | body đúng schema |
| 3 | POST + header `Content-Type: application/json` | server trả 200 + JSON |
| 4 | `parseBody` lấy content | reply đúng |
| 5 | lỗi/timeout/`choices` rỗng → `Error` | tắt server → Error, không crash |

---

## 5. 🧩 Khung code tự điền

### 5.1 `LlmClient.hpp` — *skeleton*
```cpp
#pragma once
#include "Types.hpp"
#include <string>
namespace bbb {
struct LlmConfig { std::string host; int port; std::string path, model, systemPrompt;
                   int maxTokens=120; double temperature=0.7;
                   int connectMs=3000, totalMs=15000; };
class LlmClient {
public:
    explicit LlmClient(LlmConfig c) : cfg_(std::move(c)) {}
    Result<std::string> chat(const std::string& userText);
    static Result<std::string> parseBody(const std::string& body);  // test được
private:
    LlmConfig cfg_;
};
} // namespace bbb
```

---

### 5.2 `chat()`

**Bản chất.** So với STT, đây là cùng một chu trình HTTP nhưng **đối xứng JSON-vào / JSON-ra** thay vì multipart — chính vì phần *truyền tải* (timeout/perform/RAII) giống hệt STT mà ta rút `HttpClient::post` làm base (Q4): `chat` chỉ còn lo *dựng body* và *parse*. Dùng trình dựng JSON (nlohmann) thay vì nối chuỗi tay — để nó tự escape, tránh hỏng cú pháp khi `userText` có dấu ngoặc kép. Điểm cần canh là **total timeout** dài hơn STT (LLM sinh token tuần tự) nhưng vẫn hữu hạn để giữ NFR-1.

**API toolbox** (nlohmann + base):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `nlohmann::json` object + `.dump()` | Dựng body rồi serialize ra chuỗi | để nlohmann tự escape; đừng nối chuỗi tay |
| `HttpClient::post(url, headers, body)` → `Result<string>` | Gửi POST, trả body hoặc Error (base lo timeout/RAII) | header `Content-Type: application/json` |

**Pseudo:**
```
body = json{ "model": cfg.model,
             "messages": [ {role:"system", content:cfg.systemPrompt},
                           {role:"user",   content:userText} ],
             "max_tokens": cfg.maxTokens, "temperature": cfg.temperature }.dump()
url  = "http://{host}:{port}{path}"
resp = http_.post(url, {"Content-Type: application/json"}, body)
resp là Error → return Error                  // truyền tải (server off/timeout)
return parseBody(std::get<std::string>(resp)) // ứng dụng
```

---

### 5.3 `parseBody()`

**Bản chất.** Response LLM **lồng sâu** (`choices[0].message.content`) và *không đảm bảo có mặt* — server lỗi có thể trả `choices` rỗng hoặc đổi schema. Nguyên tắc: **truy cập phòng thủ**, thiếu field thì trả `Error`, **không để ném** lan ra worker. Tách `static` ⇒ test bằng chuỗi cố định, không cần mạng.

**API toolbox** (nlohmann):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `json::parse(body)` | Chuỗi → cây JSON | ném `parse_error` → bọc try/catch |
| `j.contains("k")` / `arr.empty()` | Kiểm tồn tại trước khi truy cập | thiếu kiểm = ném/UB khi méo |
| `j.at(...).get<std::string>()` | Lấy giá trị có kiểm | `at` ném nếu thiếu → nằm trong try |

**Pseudo:**
```
try:
   j = json::parse(body)
   nếu !j.contains("choices") || j["choices"].empty() → return Error{...}
   return j["choices"][0].at("message").at("content").get<string>()
catch (json::exception) → return Error{...}
```
> **TODO(you):** viết `parseBody` chịu được response méo (thiếu field, choices rỗng). Test trước (không cần mạng).

---

### 5.4 Khung test
```cpp
TEST_CASE("LLM parseBody: content + response méo") {
    CHECK(std::get<std::string>(LlmClient::parseBody(
        R"({"choices":[{"message":{"content":"hi"}}]})")) == "hi");
    // TODO(you): assert response thiếu "choices" → trả Error (nhánh Error), không ném
}
```

---

## 6. ⚠️ Cạm bẫy ([troubleshooting.md §5](../troubleshooting.md))

- **LM Studio bind localhost** → BBB không gọi được. Bật "Serve on Local Network" (server_setup §1.2).
- **Parse cứng `choices[0]`** không kiểm rỗng → crash khi response lỗi.
- **max_tokens lớn + câu dài** → vượt NFR-1. System prompt "trả lời ngắn".
- **Gọi main thread** → UI đứng. Worker thread.
- **Nối JSON body bằng chuỗi tay** → hỏng khi text có `"`. Dùng nlohmann dump.
- **Lặp code với STT** → rút HttpClient base.

---

## 7. ✅ Checkpoint review

**Xong khi:** text → reply đúng từ LM Studio; `parseBody` test (cả response méo) pass không cần mạng; tắt server → Error.

**Đưa Claude review:** `LlmClient.cpp` + `HttpClient` base (nếu đã refactor). Hỏi:
1. *"`parseBody` của tôi có chịu được response thiếu field mà không ném không?"*
2. *"HttpClient base có chia đúng trách nhiệm với STT/LLM, hay tôi trừu tượng quá sớm/sai chỗ?"*
3. *"Total timeout của tôi có cân với NFR-1 không?"*
