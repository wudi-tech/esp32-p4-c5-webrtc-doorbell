# 板端 HTTP/HTTPS 信令服务与浏览器前端页面 —— 技术分析

> 分析对象：ESP32-P4 WebRTC 门铃（`solutions/doorbell_local`）中"浏览器 ↔ 板端"这一层的实现。
> 涉及文件：`main/webrtc_http_server.c/.h`、`main/webrtc_test.html`、`main/certs/*.pem`、
> `main/doorbell_status.c/.h`、`main/Kconfig`、`sdkconfig.defaults.esp32p4`，以及跨界的 `main/webrtc.c`、
> `components/esp_webrtc/`。
>
> 说明：本文只做只读分析，未修改任何既有文件。所有结论都给 `文件:行号` 证据；
> 凡代码里查不到的，明确写"未在代码中确认"。

---

## 0. 一句话总览

板端用 **ESP-IDF 的 `esp_https_server`（HTTPS/443，自签名证书）** 起了一个 HTTP 服务器，只注册 **4 个 URI**：
一个返回内嵌的 64KB 单页 HTML，一个用 **SSE（`text/event-stream`）** 把"信令 + 传感器/视觉事件"单向推给浏览器，
一个 **POST `/webrtc/signal/post`** 接收浏览器发来的 JSON 信令（offer/answer/candidate/customized），
一个返回门铃音效 AAC。板端自己**不跑信令协议栈**：它把 HTTP 层适配成 `esp_peer_signaling_impl_t`
（`start/send_msg/stop` 三个函数），交给 `esp_webrtc` 组件使用（`webrtc_http_server.c:596-604`）。

```
浏览器                                      ESP32-P4
  │  GET /webrtc/test          ──────────►  webrtc_test_get_handler  (返回内嵌 HTML)
  │  GET /webrtc/signal (SSE)  ──────────►  webrtc_signal_get_handler → signal_hdlr 任务
  │  ◄────────── data: {"type":"connected"}\n\n
  │  ◄────────── data: {"type":"customized","data":"RING"}  (门铃被按)
  │  点击"接听"
  │  POST /webrtc/signal/post  ──────────►  webrtc_signal_post_handler
  │   {"type":"customized","data":"ACCEPT_CALL"}
  │                                        → esp_webrtc 打开 peer connection → 产生 offer
  │  ◄────────── data: {"type":"offer","sdp":"...."}
  │  setRemoteDescription / createAnswer
  │  POST {"type":"answer","sdp":"...."} ─►  转发给 esp_peer；并从中抽出 a=candidate 行再喂一遍
  │  POST {"type":"candidate", ...}     ─►  转发给 esp_peer
  │  ◄────────── data: {"type":"candidate","candidate":"...."}
  │  ◄────────── data: {"type":"vision_state", ...} / {"type":"radar_event", ...}
  │  ◄────────── data: {"type":"heartbeat"}   (每 5 秒)
```

---

## 1. HTTP 服务器是怎么创建的

### 1.1 调用链

1. `app_main()` → `network_init(..., network_event_handler)`（`main.c:474`）；
2. Wi-Fi 连上后回调 `network_event_handler(true)` → `RUN_ASYNC(start, { start_webrtc(NULL); })`（`main.c:420-427`），
   日志里直接打印给用户的入口 URL：`Use browser to enter https://<ip>/webrtc/test`（`main.c:426`）；
3. `start_webrtc()` 组装 `esp_webrtc_cfg_t`，其中 `.signaling_impl = esp_signaling_get_http_impl()`（`webrtc.c:1254`）；
4. `esp_webrtc_open()`（`webrtc.c:1257`）→ `esp_webrtc_enable_peer_connection(false)`（`webrtc.c:1336`，先禁用自动建连）
   → `esp_webrtc_start()`（`webrtc.c:1339`）；
5. `esp_webrtc_start()` 填 `esp_peer_signaling_cfg_t` 并调用 `esp_peer_signaling_start()`（`components/esp_webrtc/src/esp_webrtc.c:880-890`）；
6. `esp_peer_signaling_start()` 调用实现体的 `start`，也就是本文件的 `webrtc_http_server_init()`（`webrtc_http_server.c:445-487`）。

### 1.2 服务器本体参数（`webrtc_http_server.c:376-407`）

```c
httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();   // :378
conf.httpd.max_uri_handlers = 16;                       // :379  (默认 8)
conf.httpd.stack_size       = 8192;                     // :380  (默认 10240)
conf.httpd.max_open_sockets = 7;                        // :382  (默认 4)
conf.httpd.lru_purge_enable = true;                     // :383
conf.httpd.recv_wait_timeout = 5;                       // :384  (秒)
conf.httpd.send_wait_timeout = 5;                       // :385  (秒)
conf.httpd.max_resp_headers = 8;                        // :386
...
esp_err_t ret = httpd_ssl_start(&server, &conf);        // :403
```

要点（面向"从 0 讲清楚"）：

| 项目 | 值 | 说明 |
| --- | --- | --- |
| 服务器框架 | `esp_http_server` + `esp_https_server` 封装（`#include` 见 `:14-15`） | 底层同一套 httpd，外面套一层 TLS |
| 启动函数 | `httpd_ssl_start()`（`:403`），停止用 `httpd_ssl_stop()`（`:585`） | |
| 端口 | **未在代码中显式设置**，因此取 `HTTPD_SSL_CONFIG_DEFAULT()` 的 `.port_secure = 443`、`.transport_mode = HTTPD_SSL_TRANSPORT_SECURE`（依据 IDF v5.5.5 `components/esp_https_server/include/esp_https_server.h:213-214`） | 浏览器要访问 `https://<IP>/webrtc/test` |
| 后端 httpd 任务栈 | 8192 字节（`:380`） | 比 IDF 默认 10240 更小，说明是按内存余量压缩过 |
| 最大并发 socket | 7（`:382`，注释解释了原因：页面 + SSE + 音频 + 探测请求要能共存） | 每个 TLS socket 约 40KB RAM（IDF 头文件注释 `:167`），所以不能开太大 |
| LRU 驱逐 | 开启（`:383`） | 防止长连接把 socket 池耗干 |
| 收发超时 | 各 5 秒（`:384-385`） | 注意：SSE 走的是 `httpd_req_async_handler_begin()` 异步模式，不占用这个超时的同步语义 |
| 日志静音 | `esp-tls-mbedtls` / `esp_https_server` / `httpd` 三个 tag 全关（`:389-391`） | 注释写明是"浏览器 TLS 预探测/中断是预期行为" |

### 1.3 注册了哪些 URI handler

注册代码集中在 `:409-440`，一共 4 个：

| # | URI | 方法 | 处理函数 | 职责 | 证据 |
| --- | --- | --- | --- | --- | --- |
| 1 | `/webrtc/signal` | GET | `webrtc_signal_get_handler` | 建立 **SSE 事件流**（`text/event-stream`），长连接；同一时刻只允许 1 个客户端 | `:410-416`、`:178-211` |
| 2 | `/webrtc/signal/post` | POST | `webrtc_signal_post_handler` | 浏览器上行信令入口（JSON） | `:418-424`、`:214-348` |
| 3 | `/webrtc/test` | GET | `webrtc_test_get_handler` | 返回内嵌的前端单页 `text/html; charset=utf-8` | `:426-432`、`:351-360` |
| 4 | `/webrtc/ring.aac` | GET | `webrtc_ring_get_handler` | 返回门铃音效 `audio/aac`，带 1 年强缓存 | `:434-440`、`:363-373` |

`max_uri_handlers = 16`（`:379`）但只用了 4 个，留了扩展余量。

### 1.4 三个 handler 的请求/响应格式细节

**(a) `GET /webrtc/test`（`:351-360`）**
用链接器符号读内嵌文件，不经过文件系统：

```c
extern const unsigned char webrtc_test_html_start[] asm("_binary_webrtc_test_html_start");
extern const unsigned char webrtc_test_html_end[]   asm("_binary_webrtc_test_html_end");
const size_t webrtc_test_html_size = webrtc_test_html_end - webrtc_test_html_start;  // :355
httpd_resp_set_type(req, "text/html; charset=utf-8");                                 // :357
httpd_resp_send(req, (const char *)webrtc_test_html_start, webrtc_test_html_size);    // :358
```
这些 `_binary_*_start/end` 符号来自 `CMakeLists.txt:14-15` 的 `EMBED_TXTFILES "ring.aac" "open.aac" "join.aac" "webrtc_test.html" "certs/servercert.pem" "certs/prvtkey.pem"`。
即：**页面、音效、证书全部编译进固件**，没有 SPIFFS/LittleFS 文件系统依赖。

**(b) `GET /webrtc/ring.aac`（`:363-373`）**

```c
httpd_resp_set_type(req, "audio/aac");                          // :369
httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000");  // :370
httpd_resp_send(req, ring_aac_start, ring_aac_size);            // :371
```
HTML 里的 `<audio id="ringSound" loop><source src="/webrtc/ring.aac" type="audio/aac">`（`webrtc_test.html:485-487`）就是消费方。
注意这里只做了"能播门铃音"，**未实现 Range 请求**（未在代码中确认有 206 分支），所以是整段下载。

**(c) `POST /webrtc/signal/post`** —— 见第 4 节。

### 1.5 一个容易踩的坑：GET 成功、但 POST 用异步 handler 的"完成"语义

SSE handler 在返回 `ESP_OK` 之前先调用了 `httpd_req_async_handler_begin(req, &session->req)`（`:196`），
把请求的生命周期交给 `signal_hdlr` 任务，任务结束时调 `httpd_req_async_handler_complete(session->req)`（`:168`）。
这是 ESP-IDF 规定的异步 handler 用法；如果忘记 complete，socket 不会释放，7 个 socket 很快被耗尽。

---

## 2. SSE（Server-Sent Events）实现

SSE 是"服务器 → 浏览器"的单向推送通道，协议非常简单：HTTP 响应头声明 `text/event-stream`，
之后服务器不断往这个响应体里写 `data: <一行文本>\n\n`，浏览器 `EventSource` 每收到一段就触发一次 `onmessage`。

### 2.1 三条队列与全局会话

```c
static QueueHandle_t signaling_queue = NULL;  // :32  长度 10，元素 char*（已是 JSON 字符串）
static QueueHandle_t telemetry_queue = NULL;  // :33  长度 16，元素 telemetry_event_t（256B 定长）
static QueueHandle_t vision_queue    = NULL;  // :34  长度 1，元素 vision_event_t（1024B + sequence）
static sse_session_t *event_stream   = NULL;  // :52  当前 SSE 会话（全局唯一）
```

`signaling_queue` 在 `webrtc_http_server_init()` 里创建（`:463`），并被当成 **signaling handle 返回**给上层：

```c
*h = (esp_peer_signaling_handle_t)signaling_queue;   // :484
```

也就是说：**"信令通道句柄"就是那个队列的指针**。后续每次 `send_msg(sig, msg)` 都会校验 `sig != signaling_queue`（`:516`），
这等于一个轻量级的"这个句柄是不是我的"标识。

`telemetry_queue`（`:447-454`）和 `vision_queue`（`:455-461`）是**惰性创建**的：只有在第一次
`publish_event` / `publish_latest_vision` 被调用（且此时为 NULL）时才建。所以：
**传感器/视觉事件的上报依赖于 HTTP 信令服务器已经启动**；如果 `start_webrtc()` 没跑，这些事件会静默丢弃，
调用点会打印 `... not queued because SSE is not ready`（`main.c:94`、`main.c:194`）。

### 2.2 注册一个客户端（`GET /webrtc/signal`，`:178-211`）

```c
httpd_resp_set_type(req, "text/event-stream");            // :181
httpd_resp_set_hdr(req, "Cache-Control", "no-cache");     // :182
httpd_resp_set_hdr(req, "Connection", "keep-alive");      // :183
httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");// :184
if (event_stream) { event_stream->stopping = true; }      // :185-190  旧会话让位
sse_session_t *session = calloc(1, sizeof(sse_session_t)); // :191
send_event_stream_msg(req, "{\"type\":\"connected\"}");    // :195  先同步发一条
httpd_req_async_handler_begin(req, &session->req);         // :196
event_stream = session;                                    // :201
xTaskCreate(signaling_msg_send_task, "signal_hdlr", 4096, session, 5, NULL); // :203
```

关键设计点：

* **单客户端模型**：`event_stream` 是单个全局指针，新连接会先把旧会话标记 `stopping = true`（`:189`），
  代码注释（`:186-188`）明确写了原因：浏览器刷新时新的 `EventSource` 会先到，旧 TCP 流还没走到下一次心跳，
  所以让新页面立刻接管，避免丢门铃事件。
* **第一条 `connected` 是"同步"发出的**（`:195`，在 `async_handler_begin` 之前），
  这样浏览器 `onopen` 之后很快能拿到一条 `{"type":"connected"}`；前端只是 `console.log`（`webrtc_test.html:946-948`）。
* 会话栈 4096 字节、优先级 5（`:203`）。

### 2.3 注销（任务退出路径，`:107-175`）

```c
while (!session->stopping && event_stream == session) { ... }   // :111  循环条件即"我是否还是当前会话"
...
httpd_req_async_handler_complete(session->req);                 // :168
if (event_stream == session) { event_stream = NULL; }            // :169-171
free(session);                                                   // :172
vTaskDelete(NULL);                                               // :174
```
退出条件有 3 类：① 被新连接顶掉（`stopping`）；② 某个 `httpd_resp_send_chunk` 返回非 0（对端断开/发送超时）；
③ 心跳发送失败（`:162-165`）。任何一条退出路径都会走上面 4 行收尾。
服务器停止时也会等它退出（`:576-583`，100ms 轮询 `event_stream` 直到为 NULL）。

### 2.4 发送任务的调度顺序（`:107-167`）—— 为什么有三条队列

每个循环最多做一件事，优先级如下：

1. **信令队列**（`xQueueReceive(signaling_queue, ..., 0)`，阻塞 0，`:116`）：SDP/candidate/bye/customized。
   取到后 `printf("Received %s\n", msg)`（`:122`，调试打印）→ `send_event_stream_msg()` → `free(msg)`。
   **队列元素是 `char*`，所有权随出队转移给这个任务**，所以必须 free（`:124`）。
2. **遥测队列**（`:129-137`）：雷达事件等语义事件，定长结构体，无内存管理负担。
3. **视觉快照队列**（`xQueuePeek(..., 100ms)`，`:138-154`）：这是"最新值合并"队列。
   单槽队列意味着**若生产端快于这里，中间的帧会直接被覆盖丢掉**；这里的 `100ms` 只是"没有新数据时的让出节拍"，
   不是生产端限速。如果 `vision.sequence == session->last_vision_sequence`（快照没有更新），
   就再 `vTaskDelay(100ms)` 让出 CPU（`:141-144`）——注释解释了原因：`xQueuePeek` 在单槽队列仍有数据时会立即返回，
   不 delay 就会忙等变成"100% 占用 signal_hdlr"。
4. 都没有数据 → `vTaskDelay(100ms)`（`:156`）。

**心跳**（`:158-166`）：每超过 5000ms 未发送就写一条 `data: {"type":"heartbeat"}\n\n`。
它的作用不是保活（`send_wait_timeout` 才管写超时），而是**探活**：一旦对端浏览器已经消失，
`httpd_resp_send_chunk` 会失败，任务借此 `break` 退出并释放 socket。
前端对 `heartbeat` 的处理是空分支（`webrtc_test.html:982-983`），只是"收到消息"这件事本身有价值。

### 2.5 `send_event_stream_msg()`（`:94-105`）

```c
int len = strlen(data) + strlen("data: ") + strlen("\n\n") + 1;
char *buf = malloc(len);
len = snprintf(buf, len, "data: %s\n\n", data);
int ret = httpd_resp_send_chunk(req, buf, len);   // 每次一条 SSE 消息
free(buf);
```
* 只发 `data:` 字段，**没有 `event:` / `id:` / `retry:` 字段**，所以前端只用 `onmessage`（`webrtc_test.html:940`）。
* 每条消息一次 `malloc/free` + 一次 chunk 写。对 1024B 的视觉快照、按 100ms 节流，开销可接受，但属于"每帧一次堆分配"。
* 数据里如果本身含 `\n`，会破坏 SSE 单条消息语义。现有三个生产者产出的都是单行 JSON（`vision_event.c:66-105`、
  `doorbell_event.c:24-31`、`webrtc_http_server.c:519-545`），所以实际不会触发；但这属于"格式约定"而不是"被校验过的约束"。

### 2.6 `webrtc_http_server_publish_event()` 的语义与线程安全（`:54-72`）

```c
if (json == NULL || telemetry_queue == NULL) return false;
size_t length = strlen(json);
if (length == 0U || length >= MAX_TELEMETRY_EVENT_SIZE) return false;  // 上限 256，含 '\0'
telemetry_event_t event = {0};
memcpy(event.json, json, length + 1U);
if (xQueueSend(telemetry_queue, &event, 0) == pdTRUE) return true;     // 队列满则不等待
telemetry_event_t discarded;
(void)xQueueReceive(telemetry_queue, &discarded, 0);                   // 丢最老的一条
return xQueueSend(telemetry_queue, &event, 0) == pdTRUE;               // 再试一次
```

语义：**语义事件（雷达进入/离开），最多缓存 16 条，队列满时丢最老的，永不阻塞调用者**。
头文件注释明确了这个取舍（`webrtc_http_server.h:31-35`）："When the queue is full, the oldest telemetry item is
discarded so WebRTC signaling is never blocked."

线程安全性：
* `xQueueSend/xQueueReceive` 本身是 FreeRTOS 线程安全的（内部临界区）；
* 结构体按值拷贝进出队列，**调用者的缓冲区可以立刻复用**——这一点很重要，`main.c:191-195` 就是用一个栈上
  `char json[256]` 反复格式化后调用的；
* 唯一非线程安全的地方是 `telemetry_queue == NULL` 的检查与"惰性创建"（`webrtc_http_server_init():447-454`）
  之间没有互斥。若极端情况下两个任务在服务器启动的同一瞬间首次 publish，理论上可能重复创建队列并泄漏一个。
  实践中这两个生产者（雷达任务、视觉任务）都在 `start_webrtc()` 之后才活跃，未在代码中确认存在该竞态的实际触发路径。

调用点：`main.c:193`（雷达事件）。JSON 由 `doorbell_radar_event_format_json()` 生成（`doorbell_event.c:6-38`）：

```json
{"type":"radar_event","event":"enter","timestamp":123456,"track_id":1,
 "x_mm":-320,"y_mm":1450,"speed":42,"zone":"door_approach",
 "confidence":null,"source":"ld2450"}
```
注意 `confidence` 恒为 `null`、`zone` 恒为 `door_approach`（`doorbell_event.c:28`），前端对这两个字段有对应翻译函数
（`translateRadarZone` / `translateRadarSource`，`webrtc_test.html:636-645`）。

### 2.7 `webrtc_http_server_publish_latest_vision()` 的语义与线程安全（`:74-92`）

```c
static uint32_t sequence;                                   // :76  函数内 static
if (json == NULL || vision_queue == NULL) return false;
if (length == 0U || length >= MAX_VISION_EVENT_SIZE) return false;   // 上限 1024
++sequence; if (sequence == 0U) ++sequence;                 // :85-88  跳过 0
event.sequence = sequence;
memcpy(event.json, json, length + 1U);
return xQueueOverwrite(vision_queue, &event) == pdTRUE;     // :91  单槽覆盖写
```

语义：**高频视觉快照只保留"最新一帧"**。`xQueueOverwrite` 要求队列长度为 1（`:456` 正是 `xQueueCreate(1, ...)`），
它覆盖旧值、永不阻塞、永不丢弃"队列满"错误。头文件注释（`webrtc_http_server.h:38-44`）说明了两点设计意图：
快照与语义事件分开、不会挤占那 16 条历史队列；新接入的 SSE 客户端会拿到当前快照一次。

"新客户端能拿到一次快照"是怎么实现的：`sse_session_t.last_vision_sequence` 初值 0（`:49`，`calloc` 清零，`:191`），
而 `sequence` 从 1 开始（`:85-88`），所以新会话第一次 `xQueuePeek` 必然不相等 → 立刻推一条（`:138-154`）。

线程安全性：
* `xQueueOverwrite` 线程安全；结构体按值拷贝，调用者缓冲区可复用（`main.c:43` 的 `vision_snapshot_json[1024]` 就是复用的）。
* **`static uint32_t sequence` 的自增不是原子的**（`:85`）：`publish_latest_vision` 若被两个任务并发调用，
  序列号可能重复或丢失一次自增。后果很轻（最多导致某帧快照被判定为"同序列"而少推一次），
  也没有用 `_Atomic`/互斥保护。
* 该函数目前只有 **1 个** 调用点（`main.c:93`，`doorbell_report_vision_snapshot()`），所以当前实际是单生产者。

调用点生成的 JSON（`vision_event.c:52-107`，先做边界/合法性校验 `:39-50`）：

```json
{"type":"vision_state","timestamp_ms":123456,"state":"confirmed",
 "source_width":640,"source_height":480,"person_count":1,
 "targets":[{"track_id":3,"kind":"pedestrian","score":0.87,
             "x":120,"y":80,"width":90,"height":220,"confirmed":true}],
 "pedestrian_model":"ready","face_model":"ready"}
```
`state` 取值 `confirmed` / `candidate` / `clear`（`vision_event.c:61-64`）；`kind` 为 `pedestrian` / `face`
（`target_is_valid()` 只允许这两种，`vision_event.c:43-44`）；`score` 以 `%u.%02u` 形式输出两位小数（`:86-91`）。

---

## 3. SSE 与前端的契约汇总（板 → 浏览器）

`signaling_msg_send_task` 发出的每条消息都是一个 JSON 对象，包在 `data: ...\n\n` 里：

| `type` | 产生位置 | 关键字段 | 前端处理 |
| --- | --- | --- | --- |
| `connected` | `:195` | 无 | 仅 log（`html:946-948`） |
| `heartbeat` | `:161` | 无 | 空分支（`html:982-983`） |
| `offer` | `:524-527` | `sdp` | `handleOffer()`（`html:949-955` → `1235-1329`） |
| `candidate` | `:528-531` | `candidate` | `handleCandidate()`（`html:963-969` → `1349-1372`） |
| `bye` | `:532-534` | 无 | `finalizeHangup()`（`html:970-972`） |
| `customized` | `:535-538` | `data`（命令字符串） | `handleCustomCommand()`（`html:973-975` → `1010-1082`） |
| `radar_event` | 遥测队列（`doorbell_event.c`） | `event`,`track_id`,`x_mm`,`y_mm`,`speed`,`zone`,`source`,`timestamp` | `addRadarEvent()`（`html:976-978` → `647-658`） |
| `vision_state` | 视觉队列（`vision_event.c`） | `state`,`targets[]`,`person_count`,`source_width/height`,`*_model` | `handleVisionState()`（`html:979-981` → `736-756`） |

**注意：`bye` 虽然板端支持（`:522-534` 与 `:322-323`），但前端从不主动发 `bye`。**
前端挂断走的是 `customized: DENY_CALL`（`html:844`），板端处理完后回 `customized: CALL_ENDED`
（`webrtc.c:601-619` 里 `SEND_CMD(command_handle, DOOR_BELL_CALL_ENDED_CMD)`），前端再 `finalizeHangup()`（`html:1066-1069`）。
所以 `bye` 这条路径在浏览器侧目前是"备用/未使用"。

### 3.1 `customized` 命令字表（双向）

板 → 浏览器（值来自 `webrtc.c:26-39` 的宏）：

| 命令 | 触发时机 | 前端反应 |
| --- | --- | --- |
| `RING` | 门铃按下（`webrtc.c:373`） | 播 `ring.aac`、`callButton` 变可点并加 `ringing` 样式（`html:1036-1042`） |
| `RING_STOPPED` | 4 秒后（`webrtc.c:293-305`；`DOOR_BELL_RING_DURATION_MS=4000`，`webrtc.c:46`） | 停铃声（`html:1043-1046`） |
| `DOOR_OPENING/OPENED/CLOSING/LOCKED/FAILED` | 门锁状态回调（`webrtc.c:421-434`） | 按钮禁用 + 顶部提示条（`html:1013-1035`） |
| `CALL_ENDED` / `CALL_END_FAILED` | 挂断结果（`webrtc.c:520-522`） | 收尾 or 报错（`html:1066-1078`） |
| `PEDESTRIAN_DETECTED` | 前端有处理分支（`html:1047-1050`），但**板端不会发**：`.detected = NULL`（`webrtc.c:1392-1395`） | 死分支；"检测到人员"实际靠 `vision_state` 状态沿记录 |

浏览器 → 板（前端发出，`html:913-918` 统一封装为 `{"type":"customized","data":cmd}`）：

| 命令 | 前端发出点 | 板端处理 |
| --- | --- | --- |
| `ACCEPT_CALL` | 点"接听"（`html:812`） | `webrtc.c:586-600`：置 `CONNECTING` 并异步打开 peer connection → 触发 offer |
| `DENY_CALL` | 点"挂断"（`html:844`） | `webrtc.c:601-619`：置 `DISCONNECTING` 并异步关闭 peer connection |
| `OPEN_DOOR` | 点"开门"（`html:830`） | `webrtc.c:574-585` → `door_lock_request_open()`，失败回 `DOOR_FAILED` |
| `ENABLE_RTP_TRANSFORMER` / `DISABLE_RTP_TRANSFORMER` | 切换按钮（`html:1419`、`1422`） | `webrtc.c:620-629`：置内部标志位 |

注意 `html:1052-1054` 里 `OPEN_DOOR` 与 `ACCEPT_CALL`（`1055-1061`）也有分支，注释写着
"Following command is by controlling only"，即这些分支是给"板端把命令回显回来"的场景准备的。

### 3.2 三条"定义了但当前走不到"的路径（汇报时容易被问到）

| 路径 | 现状 | 证据 |
| --- | --- | --- |
| `PEDESTRIAN_DETECTED` 命令 | 板端有发送函数 `pedestrian_detected()`，但检测配置里 `.detected = NULL`，永不触发；前端对应分支（`html:1047-1050`）为死代码 | `webrtc.c:450-467`、`webrtc.c:1392-1395`、`common.h:92-97` |
| 板端接收 `bye` | 解析与转发都实现了（`:322-323`、`signaling_msg_send_task` 里的 `bye` 分支 `:532-534`、前端 `html:970-972`），但前端从不发送 `bye`（挂断用 `DENY_CALL`）。浏览器刷新/关闭时也不会发，靠心跳探活发现断开 | 见第 3 节表与 4.2 表 |
| 前端主动发 `offer` | 板端支持接收（`:273-282`），前端代码里只有"回 `answer`"，没有主动发 offer 的调用点。**用途未在代码中确认** | `html:1321-1324`（唯一发 SDP 的地方，type 固定为 `answer`） |

---

## 4. 浏览器 → 板的 POST 接口

### 4.1 唯一入口：`POST /webrtc/signal/post`（`:214-348`）

请求：
```
POST /webrtc/signal/post HTTP/1.1
Content-Type: application/json

{ "type": "...", ... }
```

处理流程与错误分支（逐段）：

| 步骤 | 代码 | 行为 |
| --- | --- | --- |
| 打印调试 | `:216`、`:220`、`:254` | `printf("Get post request")`、`Request length %d`、`Get post %s`。**这是无条件 stdout 打印，包含完整 SDP**（见第 8 节风险） |
| CORS 头 | `:217-219` | `Access-Control-Allow-Origin: *`、`Allow-Methods: POST, OPTIONS`、`Allow-Headers: Content-Type` |
| 预检 | `:223-226` | `HTTP_OPTIONS` → 空 body 200 返回，不读 body |
| 长度校验 | `:227-230` | `content_len > 16KB`（`MAX_CONTENT_LEN`，`:23`）→ 400 `"Content too long"` |
| 分配缓冲 | `:231-235` | `malloc(content_len + 1)`，失败 → 400 `"No memory"` |
| 循环收包 | `:240-252` | 循环 `httpd_req_recv` 直到收满；**`HTTPD_SOCK_ERR_TIMEOUT` 时 `continue` 重试**（`:244-246`），其它 ≤0 视为失败 |
| JSON 解析 | `:257-262` | `cJSON_Parse` 失败 → 400 `"Invalid JSON"` |
| 取 `type` | `:264-269` | 缺失或非字符串 → 400 `"Missing type"` |
| 分发 | `:273-338` | 见下 |
| 回调 | `:339-342` | `if (msg.type) sig_cfg.on_msg(&msg, sig_cfg.ctx);` |
| 成功响应 | `:343` | `httpd_resp_sendstr(req, "OK")`（纯文本，无 `Content-Type: application/json`） |
| 清理 | `:344-347` | `free(buf); cJSON_Delete(root);` |

### 4.2 六种 `type` 的字段格式

| `type` | 必需字段 | 映射到的信令类型 | 证据 |
| --- | --- | --- | --- |
| `offer` | `sdp`（字符串） | `ESP_PEER_SIGNALING_MSG_SDP` | `:273-282` |
| `answer` | `sdp`（字符串） | `ESP_PEER_SIGNALING_MSG_SDP`，**并额外从 SDP 文本里逐行抽取 `a=candidate:` 行单独再发一遍** | `:283-311` |
| `candidate` | `candidate`（字符串，行首**不含** `a=`） | `ESP_PEER_SIGNALING_MSG_CANDIDATE` | `:312-321` |
| `bye` | 无 | `ESP_PEER_SIGNALING_MSG_BYE` | `:322-323` |
| `customized` | `data`（字符串，如 `ACCEPT_CALL`） | `ESP_PEER_SIGNALING_MSG_CUSTOMIZED` | `:324-333` |
| 其它 | — | 400 `"Unknown message type"` | `:334-338` |

典型请求体（前端实际发出，见第 5 节）：

```json
{"type":"offer","sdp":"v=0\r\no=- ..."}                       // 前端 handleAnswer 只发 answer；offer 场景是"浏览器作为发起方"的兼容路径
{"type":"answer","sdp":"v=0\r\no=- ... a=candidate:... \r\n"}
{"type":"candidate","candidate":"candidate:1 1 UDP 2122252543 192.168.1.7 54321 typ host","sdpMid":"0","sdpMLineIndex":0}
{"type":"customized","data":"ACCEPT_CALL"}
```

`candidate` 里前端多带了 `sdpMid` / `sdpMLineIndex`（`html:1187-1192`），但**板端解析时忽略这两个字段**（`:313-321` 只取 `candidate`）。

### 4.3 两个"从 SDP 里挖 candidate"的实现，且不一致

这是本文件最值得注意的一处细节。

**(a) POST 上行路径（`:294-311`）**：`strdup` 一份 answer 的 SDP，用 `strtok(..., "\r\n")` 逐行扫，
命中 `strncmp(line, "a=candidate:", 11) == 0` 后，`candidate_msg.data = (uint8_t *)line;`
——**直接把整行（含 `a=` 前缀）当作 candidate 数据**发给 `on_msg`。

**(b) 下行路径 `resend_all_candidate()`（`:491-512`）**：同样是扫 `a=candidate:`，但**剥掉了前缀**：

```c
char *cand = line + 2;             // :499  跳过 "a="
candidate_msg.data = (uint8_t *)cand;
```

两条路径对同一个字段的编码**不一致**（一个带 `a=`、一个不带）。浏览器端对 `resend_all_candidate` 的输出做了
"带不带 `a=` 都尽力兼容"的处理（见 `webrtc.c` 的 `STR_SAME(sdp, "candidate:")` 分支，`components/esp_webrtc/src/esp_webrtc.c:725-727`），
而上行路径带 `a=` 的字符串是否被底层接受，**未在本仓库中确认**（`esp_peer` 实现在托管组件里，分析范围内未见）。
建议项目作者实测一次"浏览器侧候选是否全部被接收"，或统一两处格式。

另外，`strtok()` 使用**进程内全局状态**，不是线程安全的。httpd 里多个 socket 可以并发进入
`webrtc_signal_post_handler`（`max_open_sockets = 7`），两个并发请求同时 `strtok` 会互相破坏解析。
当前实际只有一个浏览器在发信令，所以问题不显现，但这是隐患。

### 4.4 无认证

`webrtc_signal_post_handler` 没有任何鉴权（token / basic auth / IP 白名单）。
配合 `Access-Control-Allow-Origin: *`（`:217`），同一网络内任何页面都可以调用这个接口，
其中包含 `customized: OPEN_DOOR`（开锁）。这一点在第 8 节展开。

---

## 5. 信令与 `webrtc.c` 的交互边界

### 5.1 谁调用谁：接口注入方向

板端 HTTP 服务器**不主动调用** `webrtc.c`；它把自己实现成 `esp_peer_signaling_impl_t` 三个函数指针，
由 `webrtc.c` 注入给 `esp_webrtc`，之后所有交互都通过 `sig_cfg` 里的回调反向进行：

```
webrtc.c:1254   .signaling_impl = esp_signaling_get_http_impl()
        ↓
webrtc_http_server.c:596-604   impl = { .start = webrtc_http_server_init,
                                        .send_msg = webrtc_http_server_send_msg,
                                        .stop = webrtc_http_server_deinit }
        ↓  esp_peer_signaling_start()（components/esp_webrtc/src/esp_peer_signaling.c:45）
webrtc_http_server_init(cfg, h) 保存 cfg 副本 → sig_cfg（:476）
        ├─ sig_cfg.on_ice_info(&ice_info, cfg->ctx)   :482   （告知 ICE 角色 + STUN）
        └─ sig_cfg.on_connected(cfg->ctx)             :483   （告知信令就绪）
```

`on_ice_info` / `on_connected` / `on_msg` 这三个回调都是 `esp_webrtc` 在 `esp_webrtc_start()` 时填进来的
（`components/esp_webrtc/src/esp_webrtc.c:884-888`），分别对应 `signal_ice_received` / `signal_connected` / `signal_new_msg`。

**ICE 角色**：`webrtc_http_server.c:477-480` 硬编码

```c
esp_peer_signaling_ice_info_t ice_info = {
    .is_initiator = true,
    .server_info.stun_url = "stun:stun.l.google.com:19302",
};
```
即"板端是 controlling 方、用 Google 公共 STUN"。这个字符串**硬编码在 C 里**，可通过 Kconfig/sdkconfig 配置：
未在代码中确认（`Kconfig` 只有一项行人检测开关，见第 6 节）。

### 5.2 板 → 浏览器：`send_msg` 路径

```
webrtc.c SEND_CMD 宏（:42-43）
   esp_webrtc_send_custom_data(handle, ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING, cmd, strlen(cmd))
      → esp_peer_signaling_send_msg(rtc->signaling, &msg)   components/esp_webrtc/src/esp_webrtc.c:904-911
         → impl.send_msg == webrtc_http_server_send_msg()   webrtc_http_server.c:514
```

`webrtc_http_server_send_msg()`（`:514-558`）的职责是**把 `esp_peer_signaling_msg_t` 序列化成浏览器看得懂的 JSON**：

| 输入 `msg->type` | 输出 JSON | 代码 |
| --- | --- | --- |
| `ESP_PEER_SIGNALING_MSG_SDP` | `{"type":"offer","sdp":"..."}` | `:524-527` |
| `ESP_PEER_SIGNALING_MSG_CANDIDATE` | `{"type":"candidate","candidate":"..."}` | `:528-531` |
| `ESP_PEER_SIGNALING_MSG_BYE` | `{"type":"bye"}` | `:532-534` |
| `ESP_PEER_SIGNALING_MSG_CUSTOMIZED` | `{"type":"customized","data":"..."}` | `:535-538` |
| 其它 | 直接返回 0，不发送 | `:539-542` |

序列化用 `cJSON_PrintUnformatted()`（`:545`），之后：

```c
if (xQueueSend(signaling_queue, &json_str, pdMS_TO_TICKS(100)) != pdTRUE) {  // :549
    ESP_LOGW(TAG, "Failed to send message to signaling queue");
    free(json_str);                                                          // :551  队列满则丢
}
if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
    resend_all_candidate(sig, (char *)msg->data);                            // :554-556
}
```

两点语义：
* **队列满（10 条）时等 100ms 仍失败就丢消息并告警**（`:549-552`）。信令消息"丢一条 candidate"通常还能靠其它候选兜住，
  但"丢一条 offer"就会导致这次通话建立不起来（前端会因为收不到 offer 而一直不建连）。
* **SDP 之后紧接着把 SDP 内嵌的候选逐条单独再发一遍**（`:554-556`），这是为了在没有 trickle ICE 支持的对端上
  也能尽快收集候选。注意这里 `resend_all_candidate` 内部又调回 `webrtc_http_server_send_msg`（`:505`），
  但对方是 CANDIDATE 类型，不会再递归（`:554` 的条件只对 SDP 成立）。

### 5.3 浏览器 → 板：`on_msg` 路径

```
webrtc_signal_post_handler 解析完 JSON 后
   sig_cfg.on_msg(&msg, sig_cfg.ctx)                       webrtc_http_server.c:341
      → signal_new_msg(rtc)                                components/esp_webrtc/src/esp_webrtc.c:681
         ├─ BYE        → 断开 peer、停流（:684-706）
         ├─ CUSTOMIZED → rtc->rtc_cfg.peer_cfg.on_custom_data(...)  (:708-717)
         │                 → webrtc.c:1239 注册的 door_bell_on_cmd()  webrtc.c:557-633
         └─ 其它(SDP/CANDIDATE) → esp_peer_send_msg(rtc->pc, &peer_msg)  (:719-728)
```

`door_bell_on_cmd()`（`webrtc.c:557-633`）的边界条件：

* **只接受 `ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING` 来源**（`webrtc.c:560-562`），datachannel 来的命令会被忽略；
* 先拿 `door_bell_mutex`（`:563-566`），退出时解锁（`:631`），并把 `webrtc` 句柄快照到局部变量（`:567`）；
* 命令分发见 3.1 表。

### 5.4 建连的时间点：offer 不是一启动就发的

这是理解"为什么浏览器必须点接听"的关键：

1. `webrtc.c:1336` `esp_webrtc_enable_peer_connection(opened_webrtc, false)`
   → `components/esp_webrtc/src/esp_webrtc.c:779` 置 `rtc->pending_connect = true`；
2. 随后 `esp_webrtc_start()` → `signal_ice_received()` 看到 `pending_connect == true`，
   只打印 `Pending connection until user enable` 直接返回（`esp_webrtc.c:659-662`）；
3. `signal_connected()` 也因 `no_auto_reconnect && pending_connect` 提前返回（`esp_webrtc.c:670-673`）；
4. 直到 `ACCEPT_CALL` → `schedule_peer_connection_change(..., true, ...)`（`webrtc.c:593-599`）
   → worker 线程调 `esp_webrtc_enable_peer_connection(peer_handle, true)`（`webrtc.c:495`）
   → `pending_connect = false`，且 `signaling_connected` 已为 true，于是 `esp_peer_new_connection(rtc->pc)`（`esp_webrtc.c:796-797`）
   → 板端产生 SDP → `send_msg` → 队列 → SSE → 浏览器 `handleOffer()`。

`no_auto_reconnect = true` 的配置在 `webrtc.c:1246`，注释写明"No auto connect peer when signaling connected"。

### 5.5 挂断的另一条路径

`webrtc.c:509-522` 是 peer 侧连接结束的处理：根据结果选择 `CALL_ENDED` 或 `CALL_END_FAILED`
（`:520-521`）并 `SEND_CMD`。所以浏览器有两条"知道通话结束"的来源：
自己发出的 `DENY_CALL` 的回执，以及板端主动推的 `CALL_ENDED`。

---

## 6. 配置项（Kconfig / sdkconfig）

### 6.1 `main/Kconfig`（全文 9 行）

```kconfig
menu "DoorBell Local Demo"
config DOORBELL_SUPPORT_PEDESTRIAN_DETECT
   bool "Support pedestrian detection"
   default n
   depends on IDF_TARGET_ESP32P4 || IDF_TARGET_ESP32S3
endmenu
```
**这个 Kconfig 只定义了"是否支持行人检测"，与 HTTP/HTTPS 服务器没有任何关系。**
即：**端口、socket 数、超时、证书路径、SSE 行为全部是代码里的硬编码常量**，不通过 menuconfig 暴露。
这是个可以改进的点（例如把端口、最大客户端数、STUN URL 变成 Kconfig 项）。

### 6.2 `sdkconfig.defaults.esp32p4`（49 行）

逐项确认：**没有任何 `CONFIG_HTTPD_*` / `CONFIG_ESP_HTTPS_SERVER_*` / `CONFIG_MBEDTLS_*` 条目**
（已用 grep 在 `solutions/doorbell_local/sdkconfig.defaults.esp32p4` 内搜索 `HTTPD|HTTPS_SERVER|esp_https_server|CONFIG_ESP_HTTPS|SSL`，0 命中）。
因此 TLS/HTTPD 行为完全取 **ESP-IDF v5.5.5 的默认值**，仅有本文件里 7 行显式覆盖（`webrtc_http_server.c:379-386`）。

文件里与本功能间接相关的条目：

| 行 | 配置 | 与 Web/信令的关系 |
| --- | --- | --- |
| `:46` | `CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT=y` | 打开行人检测 → 才会有 `vision_state` JSON 走 SSE（`main.c:39`、`main.c:86-129`） |
| `:26-45` | ESP32-C5 hosted / SDIO / Wi-Fi buffer | 网络通路；网络连上才 `start_webrtc()`（`main.c:420-427`） |
| `:5` | `CONFIG_SPIRAM_SPEED_200M=y` | PS RAM，间接支撑 7 个 TLS socket 的内存 |
| `:15` | `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` | 前面那些 `printf` 调试输出走 UART 控制台 |

**结论：HTTPS 相关"配置"实际都在 C 代码里**；`sdkconfig` 只负责"网络能通 + 行人检测打开"。

---

## 7. 前端单页 `webrtc_test.html` 结构（1566 行）

### 7.1 三个大块

| 区块 | 行范围 | 内容 |
| --- | --- | --- |
| `<head>` + `<style>` | `:1-408` | 全部内联 CSS（约 400 行），无外部依赖；`<link rel="icon" href="data:,">`（`:7`）避免浏览器请求 `/favicon.ico`（很聪明的一招：服务器没注册这个 URI，会 404） |
| `<body>` DOM | `:409-487` | 见下表 |
| `<script>` | `:489-1564` | 全部逻辑，约 1070 行 |

### 7.2 UI 区域与元素 id

| 元素 | id | 行 | 说明 |
| --- | --- | --- | --- |
| 页面标题 | — | `:410` | 《智能门铃 WebRTC 测试台》，`<html lang="zh-CN">`（`:2`） |
| 左：门外画面容器 | `remoteVideoBox` / `remoteVideoStage` | `:414`、`:416` | |
| 门外视频 | `remoteVideo` | `:417` | `autoplay playsinline` |
| 检测框画布 | `visionOverlay` | `:418` | `<canvas>`，覆盖在视频上，`pointer-events: none`（CSS `:122`） |
| 视觉状态条 | `visionStatus` | `:419` | 初始文案"视觉检测数据暂停"，`aria-live="polite"` |
| 右：本机画面 | `localVideoBox` / `localVideo` | `:422`、`:424` | 默认 `hidden`，`muted` |
| 按钮：接听 | `callButton` | `:429-434` | 内联 SVG + 文案"接听" |
| 按钮：挂断 | `hangupButton` | `:435-440` | 初始 `disabled` |
| 按钮：开门 | `openDoorButton` | `:441-446` | |
| 按钮：清除事件 | `clearEventsButton` | `:448-456` | 附 `eventCount` 显示"N 条记录"（`:454`） |
| RTP 转换器开关 | `rtpTransformerToggleBtn` / `rtpTransformerText` / `rtpTransformerIcon` | `:459-469` | 默认关闭（`html:507`） |
| 事件列表 | `detectionList` | `:474` | 空列表时 CSS `::after` 显示"暂无传感器事件"（CSS `:343-344`） |
| 全局状态条 | `status` | `:481` | 初始"正在连接设备…"，`class="disconnected"` |
| 命令提示条 | `commandNotice` | `:482` | 3 秒后自动隐藏（`html:582-589`） |
| 门铃音 | `ringSound` | `:485-487` | `<audio loop>`，源 `/webrtc/ring.aac` |

布局：`.main-container` 用 flex，视频区 `flex: 1`，控制区固定 `width: 380px`（CSS `:16-41`）；
本机画面是浮在门外画面右下角的 240×180 小窗，点击可在"最大化/最小化"之间切换（CSS `:58-97`，JS `:1446-1480`）。

### 7.3 JS 全局状态（`:490-519`）

```js
let peerConnection = null;            // :491
let signalingUrl = '/webrtc/signal';        // :492
let signalingPostUrl = '/webrtc/signal/post'; // :493
let eventSource = null;               // :494
let pendingCandidates = [];           // :498  远端描述未设置前的候选缓冲
let detectionEvents = []; let eventCounter = 0; const MAX_DETECTION_EVENTS = 100;  // :500-502
const RING_FALLBACK_TIMEOUT_MS = 4500;        // :503
const PEDESTRIAN_EPISODE_TIMEOUT_MS = 3000;   // :504
const HANGUP_ACK_TIMEOUT_MS = 8000;           // :505
const VISION_STATE_STALE_MS = 3000;           // :506
const DISCONNECT_GRACE_PERIOD_MS = 15000;     // :519
```
这些常量把"板端可能不回消息"的各种情况都兜住了，是前端健壮性的核心：
* `RING_FALLBACK_TIMEOUT_MS`：若 4.5s 内没收到 `RING_STOPPED`，浏览器自己停铃声（`html:565-568`）；
* `VISION_STATE_STALE_MS`：若 3s 内没有新的 `vision_state`，把检测框擦掉、状态改成"视觉检测数据暂停"（`html:754-755`、`724-729`）；
* `HANGUP_ACK_TIMEOUT_MS`：发出 `DENY_CALL` 后 8s 没收到 `CALL_ENDED` 就提示"开发板未确认挂断，请重试"（`html:846-852`）；
* `DISCONNECT_GRACE_PERIOD_MS`：`connectionState === 'disconnected'` 时先等 15s，仍为 disconnected 才主动挂断（`html:902-910`）。

### 7.4 SSE 订阅（`startSignaling()`，`:927-1007`）

```js
eventSource = new EventSource(signalingUrl);                 // :932
eventSource.onopen  = () => { statusDiv.textContent = '已连接到门铃设备'; ... };  // :934-938
eventSource.onmessage = async (event) => {                    // :940
    const message = JSON.parse(event.data);
    switch (message.type) { case 'offer': ... case 'radar_event': ... }  // :945-986
};
eventSource.onerror = (error) => {                            // :994-1006
    statusDiv.textContent = '设备连接中断，正在重连…';
    eventSource.close();
    setTimeout(() => startSignaling(), 5000);                 // :1001-1004  5 秒后重连
};
```
`window.onload` 里调用（`:1431`）。注意：因为 `EventSource` 自带重连，代码里又手写了一次
"close + 5s 后重连"，属于**重复重连**（不是 bug，但重连节奏由这里决定，而不是 HTTP `retry:` 字段，
因为板端不发 `retry:`，见 2.5）。

`offer/answer/candidate` 三个分支都带了 `if (!peerConnection)` / `if (peerConnection)` 保护（`:949-969`），
避免乱序消息导致建连状态机错乱。

### 7.5 WebRTC 建连代码位置

| 功能 | 函数 | 行 |
| --- | --- | --- |
| 创建 `RTCPeerConnection` + 2 个 DataChannel + 各种事件回调 | `createPeerConnection()` | `:1139-1232` |
| 收到 offer → 建 PC → 加本地音轨 → setRemote → 回放 pendingCandidates → createAnswer → 回 answer | `handleOffer()` | `:1235-1329` |
| 收到 answer → `setRemoteDescription` | `handleAnswer()` | `:1332-1346` |
| 收到 candidate → 有远端描述就 `addIceCandidate`，否则入 `pendingCandidates` | `handleCandidate()` | `:1349-1372` |
| 统一 POST 出口 | `sendSignalingMessage()` | `:1375-1394` |
| 本地 ICE 候选 → POST `{"type":"candidate",...}` | `onicecandidate` | `:1184-1196` |
| 连接状态机（connected / disconnected / failed） | `onconnectionstatechange` | `:1207-1223` |
| ICE 失败时 `restartIce()` | `oniceconnectionstatechange` | `:1170-1176` |
| DataChannel（channel1/channel2）建立与周期性 8KB 压测消息 | `setupDataChannel()` | `:1491-1563` |

几个值得注意的设计：

* `RTCPeerConnection` 配置里 **`iceServers: []`（空）**（`:1141`）：浏览器不需要 STUN——因为板和浏览器同一局域网，
  host 候选足够。`bundlePolicy: 'max-bundle'`、`rtcpMuxPolicy: 'require'`（`:1143-1144`）。
* 两个 DataChannel 是**浏览器创建**的（`:1153-1154`），并用 `setInterval(..., 2000)` 每 2 秒发 8KB 数据做压力/完整性自检
  （`:1506-1525`），还在 `bufferedAmount > 65536` 时打印 `SCTP DEADLOCK` 告警（`:1517-1519`）。
  板端侧对应 `enable_data_channel = true` + `manual_ch_create = true`（`webrtc.c:1244-1245`）。
* RTP 脚本转换（`RTCRtpScriptTransform` + 内联 Worker 字符串）在 answer 创建后挂到每个 transceiver 上
  （`:1287-1318`），音频 Worker 会在 RTP 载荷后附加 `TAIL_<count>\0` 尾巴用于端到端校验（`:1085-1122`），
  视频 Worker 是直通（`:1125-1137`）。**该功能默认关闭**（`:507`，需点按钮并发送 `ENABLE_RTP_TRANSFORMER`）。
* 音视频能力：`handleOffer` 里只 `getUserMedia({ audio: true })`（`:1245-1247`），
  **浏览器侧不发送视频**；视频是板 → 浏览器单向（对应板端 `video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY`，`webrtc.c:1238`）。

### 7.6 检测框绘制（`drawVisionOverlay()`，`:686-722`）

```js
const rect = remoteVideoStage.getBoundingClientRect();          // :687  以"容器"为坐标系
const pixelRatio = window.devicePixelRatio || 1;                // :688  适配高分屏
if (visionOverlay.width !== canvasWidth ...) { ... }            // :691-694  尺寸变化时重设 canvas 分辨率
context.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);       // :696
context.clearRect(0, 0, rect.width, rect.height);               // :697
if (!message.source_width || !message.source_height) return;    // :698
// 视频用 object-fit: contain，所以有黑边，按比例缩放并居中：
const scale = Math.min(rect.width / source_width, rect.height / source_height);  // :701
const offsetX = (rect.width  - source_width  * scale) / 2;      // :702
const offsetY = (rect.height - source_height * scale) / 2;      // :703
(message.targets || []).forEach(target => {
    const x = offsetX + target.x * scale; ...                   // :705-708
    const color = target.confirmed ? (target.kind === 'face' ? '#fbbf24' : '#22c55e') : '#9ca3af';  // :709
    const label = `${visionKindLabel(target.kind)} ${Math.round(target.score * 100)}%`;             // :710
    context.strokeRect(x, y, width, height);                    // :713
    // 再画一个实心标签底色 + 文字（:717-720）
});
```

要点：**坐标来源是板端发的 `targets[].x/y/width/height`（原图 640×480 像素）**，
前端负责"原图坐标 → 显示容器坐标"的 letterbox 换算（`:700-708`）。
颜色约定：确认的人脸=琥珀色 `#fbbf24`、确认的行人=绿色 `#22c55e`、未确认=灰 `#9ca3af`（`:709`）。
窗口 `resize` 时会用缓存的 `lastVisionMessage` 重绘（`:1396-1398`）。

状态文案合成在 `renderVisionStatus()`（`:731-734`）与 `handleVisionState()`（`:736-756`）：
最终显示成 `"<视觉摘要>；<雷达摘要>"`（`:732`），并且视觉摘要里会拼上模型状态
`行人模型就绪/初始化中/不可用`（`visionModelLabel()`，`:676-679`）。

### 7.7 事件列表（`addDetectionEvent()`，`:592-634`）

* 结构：`detectionEvents` 数组 + DOM `<li>` 双份维护；新事件 `unshift` 到数组头、`insertBefore` 到列表头（`:603`、`:619`）；
* 上限 `MAX_DETECTION_EVENTS = 100`，超出时 `pop()` + 删最后一个 DOM 节点（`:620-623`）；
* 每条带时间戳（`formatTimestamp()`，`:759-770`，用 `toLocaleDateString('en-US', ...)` 再手工重排成 `YYYY-MM-DD HH:MM:SS`）；
* 触发源：
  * `addRadarEvent()`（`:647-658`）← `radar_event`，文案形如
    `雷达进入：目标 1，横向位置 -320 毫米，纵向距离 1450 毫米，速度 42 厘米/秒，门前区域，来源 LD2450 雷达，设备时间 123456 毫秒`；
  * `recordPedestrianDetection()`（`:660-670`）← `PEDESTRIAN_DETECTED`，用 3 秒静默窗口把连续检测合并成一条。
    **注意：这条路径当前是死的**——板端 `webrtc.c:1392-1395` 把行人检测配置里的 `.detected` 设为 `NULL`，
    所以 `pedestrian_detected()`（`webrtc.c:450-467`）不会被调用，浏览器收不到 `PEDESTRIAN_DETECTED`；
    "检测到人员"实际是通过下面这条 `vision_state` 的状态沿记录进列表的。
  * `handleVisionState()`（`:749-752`）← `vision_state` 的 `state` 变化沿（→confirmed 记"摄像头确认有人"，confirmed→其它 记"摄像头确认无人"）。
* "清除"按钮弹 `confirm()` 后清空（`:791-801`），无事件时 `alert('当前没有可清除的传感器事件。')`（`:794`）。

### 7.8 中文文案在哪里

**全部硬编码在 HTML 里，没有 i18n 机制**（无 `lang` 切换、无 `messages` 字典）。分布：

| 位置 | 例子 | 行 |
| --- | --- | --- |
| 标题/区块标题 | 智能门铃 WebRTC 测试台 / 门外画面 / 本机画面 | `:410`、`:415`、`:423` |
| 按钮文案 | 接听 / 挂断 / 开门 / 传感器事件 / N 条记录 / 启用 RTP 数据转换器 | `:433`、`:439`、`:445`、`:453-454`、`:466` |
| CSS 伪元素 | 暂无传感器事件 | `:344` |
| 说明段落 | 记录雷达进入/离开和摄像头人员存在事件；这里显示的是事件历史，不是实时人数统计。 | `:473` |
| 状态条 init | 正在连接设备… / 视觉检测数据暂停 | `:481`、`:419` |
| JS 动态文案 | 已连接到门铃设备 / 设备连接中断，正在重连… / 获取麦克风失败： / 开发板未确认挂断，请重试 / 正在开门… / 门已打开 / 门已锁定 / 门锁操作失败 / 通话已结束 / 通话建立失败 / 摄像头确认有人 / 雷达进入 / 雷达离开 / 横纵位置单位换算 | `:936`、`:996`、`:819`、`:850`、`:1016`、`:1020`、`:1029`、`:1034`、`:861`、`:1327`、`:744-745`、`:648-656` |
| `alert` / `confirm` | 当前没有可清除的传感器事件。/ 确定清除全部 N 条传感器事件吗？ | `:794`、`:798` |

另外存在一份**自动化校验**这些中文串的测试：`tests/verify_round_two_ui.py:58-91`
（`require_tokens` 断言"智能门铃 WebRTC 测试台""接听""挂断""开门"……必须在 HTML 里），
并在 `:92-101` 反向断言不能出现英文旧文案（`>Remote Video<`、`No detection events yet` 等）。
这说明中文 UI 是"被测试锁住"的契约，改动文案时这条测试会失败——项目作者汇报时可以提这一点。

---

## 8. 安全性与健壮性评估

### 8.1 证书与 TLS

| 项 | 内容 | 证据 |
| --- | --- | --- |
| 证书主体 | `CN = ESP32 HTTPS server example`，自签名（Issuer 与 Subject 相同），`CA:TRUE` | `certs/servercert.pem:3-4`、`:12-13` |
| 有效期 | `notBefore 2018-10-17` → `notAfter 2028-10-14` | `certs/servercert.pem:3-4` |
| 私钥格式 | PKCS#8 未加密（`-----BEGIN PRIVATE KEY-----`），RSA 2048 | `certs/prvtkey.pem:1-2` |
| 来源 | 就是 ESP-IDF `esp_https_server` 官方示例的默认证书（CN 字样一致） | — |
| 嵌入方式 | `EMBED_TXTFILES "certs/servercert.pem" "certs/prvtkey.pem"`（`CMakeLists.txt:15`），代码里用 `_binary_servercert_pem_start/_end`、`_binary_prvtkey_pem_start/_end` 取地址与长度 | `webrtc_http_server.c:393-401` |
| 客户端校验 | 未设置 `cacert_pem`/`cacert_len`，保持默认的"不要求客户端证书" | `:376-401` 中未出现 cacert |
| 安全元件 | 未启用 `use_secure_element` | 同上 |
| 会话票据 | 未启用 | 同上 |

后果（对项目作者要说清楚）：
* 浏览器会显示"连接非私密/证书无效"，必须手动点"继续访问"。这是自签名证书的必然结果，不是 bug。
  项目文档里 `docs/submission/02_使用指南.md` 应有对应说明（本文未逐条核对）。
* 证书**有效期到 2028-10-14**，之后设备会直接连不上，需要在文档里记一笔。
* 私钥是**公开的示例私钥**（随 IDF 示例分发），因此 TLS 只提供"加密"不提供"身份认证"效果；
  局域网内任何能拿到该私钥的人都可以做中间人。对 Demo 可接受，但不应写成"已实现安全通信"。

### 8.2 并发与连接数

* `max_open_sockets = 7`（`:382`）：1 个页面 + 1 个 SSE + 1 个 ring.aac + 若干 TLS 探测/重连。
  SSE 是**长连接**，刷新页面时旧连接不会立刻消失，代码用 `event_stream->stopping` + 让位机制（`:185-190`）缓解，
  但**旧 socket 的关闭仍依赖其任务下一次发送失败或心跳**，因此这一瞬间可能占用 2 个 SSE socket。
* `max_uri_handlers = 16`（`:379`）：够用。
* SSE **只支持一个并发客户端**（`event_stream` 单指针，`:52`）：第二个浏览器打开页面时，第一个的 SSE 会被顶掉，
  表现为"第一个页面突然 5 秒后重连"（前端 `onerror` 兜底，`html:994-1006`）。**这是重要的产品限制**，
  汇报时应明确"当前设计为单观看端"。
* `lru_purge_enable = true`（`:383`）：socket 池满时会驱逐最久未用的连接，避免彻底拒绝服务。
* 信令队列长度 10、等待 100ms（`:24`、`:549`）；遥测队列 16、满了丢最老（`:25`、`:69-71`）；
  视觉队列 1、覆盖写（`:456`、`:91`）。三者都不会无限增长，**没有内存泄漏式堆积风险**。

### 8.3 错误处理清单（板端）

| 场景 | 处理 | 证据 |
| --- | --- | --- |
| POST body 超 16KB | 400 `"Content too long"` | `:227-230` |
| `malloc` 失败 | 400 `"No memory"` | `:231-235` |
| 收包失败（非超时） | `goto _exit`，返回 `ESP_FAIL`，**未显式发错误响应** | `:243-249` |
| JSON 非法 | 400 `"Invalid JSON"` | `:257-262` |
| 缺 `type` / 缺 `sdp` / 缺 `candidate` / 缺 `data` | 对应的 400 错误 | `:264-269`、`:274-279`、`:313-318`、`:325-330` |
| 未知 `type` | 400 `"Unknown message type"` | `:334-338` |
| SSE 客户端断开 | `httpd_resp_send_chunk` 返回非 0 → 任务退出并释放 socket | `:125-127`、`:134-137`、`:149-152` |
| SSE `calloc` / `xTaskCreate` 失败 | 返回 `ESP_ERR_NO_MEM` / `ESP_FAIL`，并回收已申请资源 | `:191-208` |
| 服务器启停 | `httpd_ssl_stop()` + 队列释放 + 等 SSE 任务退出 | `:561-594` |

发现的**代码级问题**（供作者修 bug 用，以下均为静态阅读结论）：

1. **`root` 未初始化即可能 `goto _exit`**：`cJSON *root = NULL;`（`:238`）已初始化为 NULL，`cJSON_Delete(NULL)` 是安全空操作，
   这一条**不是**问题；但 `buf` 在 `malloc` 之后的**所有**错误路径都通过 `_exit` 统一 `free(buf)`（`:344-345`），
   而 `:228`/`:233` 两个提前 `return ESP_OK` 的分支发生在 `malloc` 之前，所以也没有泄漏。**结论：这两处无泄漏，符合直觉但依赖"提前 return 都在 malloc 之前"这一脆弱前提。**
2. **成功应答与回调的先后顺序**：`:341` 先 `sig_cfg.on_msg()`，紧接着 `:343 httpd_resp_sendstr(req, "OK")`。
   而 `:255` 在解析前把 `ret` 重置为 `ESP_OK`，与上面 `:237` 的初值重复（`:237` 与 `:255` 都写了一次 `ret = ESP_OK`）。
   逐条走完所有 `goto _exit` 的分支后可以确认：**每个错误分支都在 `:343` 之前跳走，静态阅读未发现"报错后又回 OK"的实际执行路径**；
   但"写响应"没有和"是否成功"绑定（不依赖 `ret`、也不依赖 `msg.type`），
   对后续维护是脆弱点——一旦有人在 `:339-343` 之间插入 `goto` 或改成条件分支，就会引入双重响应。
   另外 HTTP 成功响应是 `text/plain` 的 `OK`，没有 `Content-Type: application/json`，前端只检查 `response.ok`（`html:1384-1386`），
   所以当前不影响功能。
3. **`strtok` 非线程安全**：`:297-308`（POST 路径）与 `:495-507`（下行路径）。多浏览器/多并发 POST 时可能互相破坏解析。
4. **候选格式不一致**：上行保留 `a=`（`:302`），下行去掉 `a=`（`:499`）。见 4.3。
5. **`printf` 打印完整 SDP 和自定义命令**：`:122`、`:216`、`:220`、`:254`。
   在 `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`（`sdkconfig.defaults.esp32p4:15`）下会从 UART 输出，
   属于**信息泄露**与**性能开销**（60+ 行的 SDP 逐字符走控制台）。建议改成 `ESP_LOGD` 或只打印长度/类型。
6. **`event_stream` 无锁**：`:52`、`:111`、`:118`、`:131`、`:146`、`:169`、`:185-190`、`:201`、`:576-583`。
   多任务读写同一指针，使用 `bool stopping` 做协作式退出。当前任务模型下（httpd 任务 + SSE 任务 + 停止任务）实际风险较低，
   但这是**数据竞争**，严格来说应加互斥或原子操作。
7. **`publish_latest_vision` 的 `static uint32_t sequence` 非原子**（`:76`、`:85-88`）。
8. **无鉴权 + `Access-Control-Allow-Origin: *`**（`:184`、`:217`）：
   `OPEN_DOOR` 这条"开门"命令只靠 `{"type":"customized","data":"OPEN_DOOR"}` 一个字符串。
   局域网内任何设备/网页都可以 POST 它（CORS 允许任何源，且 `OPEN_DOOR` 既不需要已建立通话，也不需要配对），
   板端 `door_bell_on_cmd()` 在 `door_bell_state < CONNECTING` 时也会执行开锁（`webrtc.c:574-585`）。
   **这是本项目最需要向评审说明的安全边界**：目前是"可信局域网 Demo"级别。
9. **无 HTTPS 到 HTTP 重定向 / 无 HTTP 端口**：`transport_mode = HTTPD_SSL_TRANSPORT_SECURE`（IDF 默认），
   只监听 443，不监听 80。浏览器输 `http://` 会连不上——文档需提示用 `https://`。
10. **`Kconfig` 未暴露任何 Web 参数**：端口、并发数、STUN URL、是否启用 SSE 都写死在 C 里（见 6.1）。

### 8.4 前端健壮性（正面项）

* 所有"等板端回消息"的地方都有超时兜底（4.5s 铃声 / 3s 视觉 / 8s 挂断回执 / 15s 断连宽限），见 7.3。
* `handleCandidate` 会缓存"远端描述未就绪时的候选"（`:1361-1365`），并在 `handleOffer` 里补放（`:1266-1280`），
  解决了 candidate 早于 offer 到达的经典竞态。
* `msg.type` 未知时只 `console.warn`，不抛异常（`:984-985`）。
* 收到 offer 时若已有 `peerConnection` 则拒绝重复建连（`:952-954`）。

### 8.5 前端健壮性（负面项）

* `EventSource.onerror` 里既 `close()` 又 `setTimeout(..., 5000)` 手动重连（`:999-1005`）——
  与浏览器内建重连语义叠加，重连时机不完全可控；且**没有指数退避**，设备长时间不在线时会持续每 5 秒重连。
* `drawVisionOverlay` 每个视觉事件都新建/重设 canvas 尺寸判断并整屏重绘（`:686-722`），手机上高频事件时有掉帧风险；
  板端靠"单槽队列覆盖写 + 100ms 让出节拍"（`:138-154`）**被动合并**快照，
  但**没有对外的显式帧率承诺**：若视觉生产端比 SSE 消费端更快，前端仍可能收到接近生产速率的 `vision_state`，
  同时中间的帧会被静默丢弃（前端无法知道丢了哪些帧）。
* DataChannel 每 2 秒发 8KB（`:1509-1524`），如果同时打开多个页面，会给板端带来成倍的 SCTP 压力（且 SSE 只服务一个）。

---

## 9. `doorbell_status.c/.h` 与网页状态展示的关系（重要发现）

`doorbell_status.h` 定义了一套**面向"面板式状态展示"的枚举快照**：

```c
typedef enum { DOORBELL_PRESENCE_UNKNOWN=0, _CLEAR, _CANDIDATE, _CONFIRMED } doorbell_presence_state_t;  // .h:11-16
typedef enum { DOORBELL_CALL_IDLE=0, _RINGING, _ACTIVE }      doorbell_call_state_t;                     // .h:18-22
typedef enum { DOORBELL_LOCK_UNKNOWN=0, _LOCKED, _OPENING, _OPEN } doorbell_lock_status_t;               // .h:24-29
typedef enum { DOORBELL_NETWORK_DISCONNECTED=0, _CONNECTING, _CONNECTED } doorbell_network_state_t;       // .h:31-35
typedef struct {
    doorbell_presence_state_t presence; uint8_t confirmed_person_count;
    doorbell_call_state_t call; doorbell_lock_status_t lock;
    doorbell_network_state_t network;
    vision_model_state_t pedestrian_model, face_model;
} doorbell_status_snapshot_t;                                                                            // .h:37-45
```

实现只有两个函数（`doorbell_status.c` 全文 35 行）：
* `doorbell_status_init()`：清零 + 设初值（UNKNOWN / IDLE / UNKNOWN / DISCONNECTED / 两个模型 INITIALIZING），`c:6-18`；
* `doorbell_status_apply_vision()`：由 `vision_snapshot_t` 推导
  `presence`（`confirmed_present` → CONFIRMED；否则 `target_count>0` → CANDIDATE；否则 CLEAR）
  与 `confirmed_person_count`、两个模型状态，`c:20-35`（其中 `c:26-30` 是三目嵌套）。

**关键结论（必须如实汇报）**：
* 在整个 `solutions/doorbell_local` 生产代码中，**没有任何一处调用 `doorbell_status_init()` / `doorbell_status_apply_vision()`**。
  全仓库命中只有这两个文件自身、`tests/doorbell_status_test.c:8-22`、`tests/verify_doorbell_status.py`。
* 更重要的：**它不参与网页状态展示**。网页上的状态文字/检测框全部直接来自 SSE 的两条 JSON
  （`vision_state` 与 `radar_event`），数据源是 `main.c:86-129` 的 `doorbell_report_vision_snapshot()` →
  `webrtc_http_server_publish_latest_vision()`，与 `doorbell_status` 无关。
* `doorbell_status_snapshot_t` 里的 `call` / `lock` / `network` 三个字段**在代码中从未被赋值**（除 `init` 的初值），
  也没有序列化成 JSON 的格式化函数（无 `doorbell_status_format_json` 之类）。也就是说：
  **"通话状态/门锁状态/网络状态"在网页上是靠 `customized` 命令字符串间接体现的**
  （`DOOR_OPENING/DOOR_OPENED/DOOR_LOCKED`、`RING`、`CALL_ENDED` 等，见 3.1），
  而不是通过这个 status 结构。
* 因此，如果汇报材料里写"网页状态由 doorbell_status 统一提供"，是**不准确**的。
  准确的表述是：`doorbell_status` 目前是一个**已定义但未接线（unwired）的状态聚合模块**，
  可能原本打算作为"统一状态面板"的数据源，但网页最终走的是 SSE 事件流。

> 建议（供作者参考，不影响本次分析）：要么把 `doorbell_status` 接进 `main.c` 并用它生成一条
> `{"type":"status"}` SSE 事件给网页的 `status` 条，要么在文档里明确它是预留模块。

---

## 10. 完整时序：浏览器打开页面 → 媒体建立

下面用文字把整条链路画清楚。标注了每一步的落点代码。

```
【阶段 0：设备侧准备（浏览器打开之前）】
 0.1  上电 → app_main()（main.c:435）→ init_board() → doorbell_webrtc_init()（main.c:442）
 0.2  门锁/雷达/媒体子系统初始化（main.c:455-472），console 起来（main.c:473）
 0.3  network_init()（main.c:474）→ Wi-Fi 连接成功
 0.4  network_event_handler(true)（main.c:420）
        → RUN_ASYNC: start_webrtc(NULL)（main.c:424-427）
        → UART 打印 "Use browser to enter https://<IP>/webrtc/test"（main.c:426）
 0.5  start_webrtc() 组装 cfg → esp_webrtc_open()（webrtc.c:1257）
        → esp_webrtc_enable_peer_connection(false)（webrtc.c:1336）→ pending_connect = true
        → esp_webrtc_start()（webrtc.c:1339）
 0.6  esp_webrtc_start() → esp_peer_signaling_start()（esp_webrtc.c:890）
        → webrtc_http_server_init()（webrtc_http_server.c:445）
            ├ 建遥测队列/视觉队列/信令队列（:447-467）
            ├ init_http_server()：httpd_ssl_start(443, 自签名证书)（:376-407）
            ├ 注册 4 个 URI（:410-440）
            ├ on_ice_info(is_initiator=true, stun=google)（:477-482）
            │     → signal_ice_received() 因 pending_connect 只记录不建连（esp_webrtc.c:659-662）
            └ on_connected()（:483）
                  → signal_connected() 因 pending_connect 提前返回（esp_webrtc.c:670-673）
 结果：HTTPS 服务已在 https://<IP>:443 监听；此时**还没有任何 offer**。

【阶段 1：打开页面】
 1.1  浏览器输入 https://<IP>/webrtc/test
 1.2  TLS 握手（自签名证书 → 浏览器警告 → 用户选择继续）
 1.3  GET /webrtc/test → webrtc_test_get_handler（:351）
        → 一次性返回内嵌的 64KB HTML（:355-358，Content-Type: text/html; charset=utf-8）
 1.4  浏览器解析 DOM；顺带 GET /webrtc/ring.aac（<audio preload="auto">，html:485-487）
        → webrtc_ring_get_handler（:363），返回 audio/aac + 1 年缓存

【阶段 2：SSE 订阅】
 2.1  window.onload → initRingSound() + startSignaling()（html:1429-1431）
 2.2  new EventSource('/webrtc/signal')（html:932）
 2.3  GET /webrtc/signal → webrtc_signal_get_handler（:178）
        ├ 设置 text/event-stream / no-cache / keep-alive / CORS:*（:181-184）
        ├ 若有旧会话 → stopping = true 让它退出（:185-190）
        ├ 同步写第一条 data: {"type":"connected"}（:195）
        ├ httpd_req_async_handler_begin()（:196）
        └ 创建 "signal_hdlr" 任务（:203），handler 返回 ESP_OK
 2.4  浏览器 onopen → 状态条变"已连接到门铃设备"（html:934-938）
 2.5  signal_hdlr 立即把视觉队列里的"当前快照"推一次（因为 last_vision_sequence=0 ≠ sequence≥1）
        → data: {"type":"vision_state",...}（:138-154）
        → 前端 drawVisionOverlay + 状态文案（html:736-756）
 2.6  之后每 5 秒：data: {"type":"heartbeat"}（:158-166）

【阶段 3：门铃被按下（板 → 浏览器，单向）】
 3.1  实体按键/雷达融合 → doorbell_request_ring()（webrtc.c:373 附近）
 3.2  SEND_CMD(RING) → esp_webrtc_send_custom_data(VIA_SIGNALING)（webrtc.c:42-43、898-911）
        → webrtc_http_server_send_msg()（:514）→ {"type":"customized","data":"RING"} 入队（:549）
 3.3  signal_hdlr 出队 → data: {"type":"customized","data":"RING"}（:116-128）
 3.4  前端 handleCustomCommand('RING')（html:1036-1042）
        → playRingSound()（html:552-570，播 ring.aac）
        → callButton 启用并加 ringing 样式、hangupButton 启用
        → 启动 4.5s 兜底定时器
 3.5  4 秒后板端发 RING_STOPPED（webrtc.c:293-305）
        → 前端 stopRingSound()（html:1043-1046）；若没收到，靠 3.4 的兜底定时器停（html:565-568）

【阶段 4：用户点"接听" → offer/answer 交换（核心）】
 4.1  点接听（html:804-821）
        → getUserMedia({audio:true}) 拿麦克风（html:807-811）
        → sendCustomCommand('ACCEPT_CALL')（html:812）
 4.2  sendSignalingMessage()（html:1375）→ POST /webrtc/signal/post
        body: {"type":"customized","data":"ACCEPT_CALL"}
 4.3  板端 webrtc_signal_post_handler（:214）
        → cJSON_Parse → type=customized → msg.type = CUSTOMIZED, msg.data="ACCEPT_CALL"（:324-333）
        → sig_cfg.on_msg()（:341）→ signal_new_msg()（esp_webrtc.c:708-717）
        → door_bell_on_cmd()（webrtc.c:557）→ 命中 ACCEPT_CALL（webrtc.c:586-600）
            ├ 状态置 CONNECTING（webrtc.c:592）
            └ schedule_peer_connection_change(..., enable=true)（webrtc.c:593）
        → worker 线程 esp_webrtc_enable_peer_connection(true)（webrtc.c:495）
            → pending_connect=false，且 signaling_connected=true
            → esp_peer_new_connection(pc)（esp_webrtc.c:796-797）→ 板端生成 SDP offer
        → 板端 send_msg(SDP) → webrtc_http_server_send_msg()（:514）
            ├ {"type":"offer","sdp":"..."} 入队（:524-527、:549）
            └ resend_all_candidate()：把 SDP 里自带的候选逐条作为 candidate 入队（:554-556、:491-512）
        → HTTP 响应 "OK"（:343）
 4.4  signal_hdlr 依次推送：
        data: {"type":"offer","sdp":"..."}
        data: {"type":"candidate","candidate":"..."} × N
 4.5  前端 onmessage → case 'offer'（html:949-955）→ handleOffer(message)（html:1235）
        ├ 若还没有 PC：createPeerConnection()（html:1239、1139）——含 2 个 DataChannel、ICE 回调
        ├ 若还没麦克风：getUserMedia（html:1244-1249）
        ├ addTrack 把麦克风加到 PC（html:1252-1257）
        ├ setRemoteDescription(offer)（html:1261-1264）
        ├ 补放之前缓存的 pendingCandidates（html:1266-1280）
        ├ createAnswer + setLocalDescription（html:1283-1285）
        ├ （可选）挂 RTCRtpScriptTransform（html:1288-1318，默认关）
        └ sendSignalingMessage({type:'answer', sdp})（html:1321-1324）→ POST
 4.6  case 'candidate'（html:963-969）→ handleCandidate()（html:1349）
        ├ 若远端描述已就绪 → addIceCandidate（html:1355-1360）
        └ 否则 push 到 pendingCandidates（html:1362-1365）
 4.7  板端收到 answer（POST）→ webrtc_signal_post_handler 的 answer 分支（:283-311）
        ├ msg.type = SDP，data = answer 的 sdp（:290-292）
        ├ 逐行抽 a=candidate: → 每条单独 on_msg(CANDIDATE)（:294-311）
        ├ sig_cfg.on_msg(&msg)（:341）→ signal_new_msg → esp_peer_send_msg(pc)（esp_webrtc.c:719-728）
        └ 响应 "OK"
 4.8  浏览器 onicecandidate（html:1184-1196）持续 POST
        {"type":"candidate","candidate":"candidate:...","sdpMid":"0","sdpMLineIndex":0}
      → 板端 candidate 分支（:312-321）→ on_msg → esp_peer_send_msg
 4.9  双向候选交换完成 → ICE/DTLS 建立 → SDP 协商完成

【阶段 5：媒体流建立】
 5.1  板端 track 到达浏览器 → peerConnection.ontrack（html:1199-1204）→ remoteVideo.srcObject = stream
        → "门外画面"开始播放（H.264，640×480@15fps，settings.h:31-33、webrtc.c:1231-1236）
 5.2  浏览器麦克风轨 → 板端（音频双向：板端 audio_dir = SEND_RECV，webrtc.c:1237）
 5.3  DataChannel 打开 → channel.onopen（html:1506）→ 每 2 秒发 8KB（html:1509-1524）
 5.4  connectionState === 'connected' → 清掉断连定时器（html:1213-1214）
 5.5  持续过程：vision_state（由单槽队列覆盖合并后的最新帧）+ radar_event + heartbeat 走同一条 SSE 推送
        → 检测框绘制（html:686-722）、事件列表追加（html:592-634）

【阶段 6：挂断】
 6.1  点"挂断"（html:824-826）→ requestHangup()（html:833-859）
        → sendCustomCommand('DENY_CALL')（html:844）
        → 启动 8s 回执超时（html:846-852）
 6.2  板端 door_bell_on_cmd() 命中 DENY_CALL（webrtc.c:601-619）
        → 状态 DISCONNECTING → 异步 disable peer connection → 关闭 PC/停流
        → SEND_CMD(CALL_ENDED)（webrtc.c:520-521 的 peer 结束路径，或 :603 的 NONE 分支）
 6.3  前端 case 'CALL_ENDED'（html:1066-1069）→ finalizeHangup()（html:861-893）
        → close PC、停本地轨、清 srcObject、按钮复位
 6.4  SSE **保持连接**（html:872 注释 "Should keep event source open"），页面可以继续看状态、再按接听
```

---

## 11. 汇报时可以用的一句话总结（分层）

1. **传输层**：一块 P4 用 `esp_https_server` 在 443 端口起 HTTPS，证书/私钥/网页/音效四个文件都通过
   `EMBED_TXTFILES` 编进固件，没有任何文件系统依赖（`CMakeLists.txt:14-15`、`webrtc_http_server.c:351-401`）。
2. **适配层**：把 HTTP 收发的 JSON 适配成 ESP WebRTC 组件的 `esp_peer_signaling_impl_t` 接口
   （`webrtc_http_server.c:596-604`），所以**"信令服务器"本质是三个函数 + 三条 FreeRTOS 队列**，没有独立协议栈。
3. **推送层**：SSE 单连接 + 三条队列（信令 10 / 遥测 16 / 视觉 1 覆盖写）+ 每 5 秒心跳探活，
   优先级保证信令不被高频视觉数据饿死（`webrtc_http_server.c:107-175`）。
4. **前端层**：单文件 1566 行（400 行 CSS + 1070 行 JS）的纯原生页面，
   `EventSource` 收、`fetch` 发，中文文案硬编码且有测试锁定（`tests/verify_round_two_ui.py`）。
5. **边界与风险**：单观看端、无鉴权、CORS `*`、示例私钥（2028 年到期）、`printf` 打印完整 SDP、`strtok` 线程不安全、
   候选格式上下行不一致；`doorbell_status` 模块已定义但**未接线到网页**；
   `PEDESTRIAN_DETECTED` 命令与前端对应分支**同为死代码**（`.detected = NULL`，`webrtc.c:1392-1395`）。

---

## 12. 明确"未在代码中确认"的事项

* `/webrtc/signal/post` 之外是否存在其它 HTTP 上行接口：未发现（全仓库仅 4 个 `httpd_uri_t`，`:410-440`）。
* `esp_peer` 底层是否接受带 `a=` 前缀的候选字符串（上行路径 `:302`）：未在本仓库确认（实现在托管组件内）。
* 板端是否会发送 `PEDESTRIAN_DETECTED`：代码里**有发送点但当前不会触发**。
  发送函数是 `webrtc.c:450-467` 的 `pedestrian_detected()`（`:455` 用 `snprintf` 拼字符串、`:462` `SEND_CMD`），
  该字符串没有 `#define` 宏常量（全部命令里唯一一处"裸字符串"）。
  但启动行人检测时 `pedestrian_detect_cfg_t` 的 `.detected` 显式设为 `NULL`（`webrtc.c:1392-1395`，
  结构体定义见 `common.h:92-97`），所以这个回调没有任何调用方 → 该命令与前端 `webrtc_test.html:1047-1050` 的分支**同为死代码**。
  当前实际的视觉触发路径是 `.snapshot = vision_snapshot_received`（`webrtc.c:1394`、`:469-474`）→ `vision_state`。
  未在代码中确认的是：未来是否打算把 `.detected` 接上（例如给不跑语义模型的场景保留一条轻量通道）。
* HTTP 端口是否为 443 之外的值：代码未设置端口，按 IDF v5.5.5 默认取 443；如现场实际不同，需以实机日志为准。
* 是否存在第二条 HTTP（非 TLS）监听：代码只调用 `httpd_ssl_start()`，且 `transport_mode` 为默认的
  `HTTPD_SSL_TRANSPORT_SECURE`，因此未在代码中确认存在 80 端口监听。
* `doorbell_status_*` 是否有计划中的调用方（如未来的状态面板）：未在代码中确认，仅有单元测试。
* 前端 `type:'offer'` 发送路径：`webrtc_test.html` 中没有主动发送 offer 的调用点
  （`handleOffer` 只发 `answer`），板端却支持接收 `offer`（`:273-282`）；该分支的实际使用场景未在代码中确认。
