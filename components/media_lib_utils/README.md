# 媒体库工具

帮助您构建跨平台应用的工具 **media_lib_sal**。

## 亮点

**`data_queue`** — **免拷贝环形缓冲 FIFO**: 一个备份缓冲区; 生产者和消费者获取 **直接指针** 进入环形缓冲。

**`msg_q`** — 固定深度的固定大小消息队列; send/recv 可选择无等待。

## 使用方法

新增依赖项于 `idf_component.yml`：
```yaml
dependencies:
  espressif/media_lib_utils:
    version: "~0.9"
```

示例用法 (调用顺序)：

**数据队列** — 生产者和消费者各自持有环形缓冲的指针; 该路径内队列中无额外的 memcpy。

```c
#include "data_queue.h"
#include <string.h>

/* Stage 1: create backing ring */
data_queue_t *q = data_queue_init(64 * 1024);

/* Stage 2 and 3 run on different threads; order here is for reading only */

/* Stage 2: producer (own thread/task) */
for (;;) {
    int   want = 4096;
    int   got;
    void *wr;

    /* Reserve contiguous write span; blocks when ring is full */
    wr = data_queue_get_buffer(q, want);
    if (!wr) {
        /* Shutdown path or allocation failure */
        break;
    }

    /* Write payload into wr .. (read, memcpy, encoder output, etc.) */
    got = want;

    /* Hand chunk to reader side of the FIFO */
    data_queue_send_buffer(q, got);
}

/* Stage 3: consumer (own thread/task) */
for (;;) {
    void *rd = NULL;
    int   sz = 0;

    /* Lock front chunk; blocks until data exists or queue tears down */
    if (data_queue_read_lock(q, &rd, &sz) != 0) {
        /* No data / error / closing */
        break;
    }

    /* Use rd[0 .. sz) in place (no extra copy out of the ring) */
    process(rd, sz);

    /* Release chunk so producer can reclaim space */
    data_queue_read_unlock(q);
}

/* Stage 4: shutdown */
/* Wake blocked producer/consumer if you stop out-of-band */
/* data_queue_wakeup(q); */

/* Free mutex, events, and backing buffer */
data_queue_deinit(q);
```

**消息队列** — 固定槽位; 每次发送大小必须 ≤ `msg_size` 从创建。

```c
#include "msg_q.h"
#include <stdint.h>
#include <string.h>

enum {
    MSG_MAX  = 16,
    MSG_SIZE = 256,
};

/* Stage 1: create slot pool */
msg_q_handle_t mq = msg_q_create(MSG_MAX, MSG_SIZE);

/* Stage 2: enqueue */
uint8_t in[MSG_SIZE];
int     n;

memset(in, 0, sizeof(in));
/* Application fills in[0 .. n) */
n = 32;

/* Block while all slots are busy */
msg_q_send(mq, in, n);

/* Stage 3: dequeue */
uint8_t out[MSG_SIZE];
int      r;

/* false = wait for a message; true = no_wait (r==1 if empty) */
r = msg_q_recv(mq, out, sizeof(out), false);
if (r == 0) {
    /* Valid message in out */
}
if (r == 1) {
    /* Only when no_wait: queue was empty */
}

/* Stage 4: status and teardown */
int pending = msg_q_number(mq);

msg_q_destroy(mq);
```

API 在头文件中有文档说明. 更多内容 SAL: [media_lib_sal README](https://github.com/espressif/esp-webrtc-solution/tree/main/components/media_lib_sal/README.md)。
