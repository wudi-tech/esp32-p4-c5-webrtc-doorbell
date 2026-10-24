# LD2450 区拆迁和门铃响铃实施计划

> **代理工人:** REQUIRED SUB-SKILL: 使用超能力:亚剂驱动开发 (建议) 或超级大国:逐项任务执行计划. 步骤使用复选框 (`- []`) 跟踪的语法。

**目标:** 转换已解析 LD2450 目标进入时断时断的门前状态，并触发现有的 WebRTC 门铃环路，每次合格访问一次。

**架构:** 保留 UART 框架设置 `radar_ld2450`, 添加可测试的主机 `presence_fusion` 状态机器， 让 `main.c` 将确认的进入事件与被守卫的事件连接 WebRTC 环请求. 小型的纯C环政策保持了准备状态和繁忙状态的决定可以检验而不连接 ESP-IDF。

**技术堆栈:** ESP-IDF 5.5.5, ESP32-P4, FreeRTOS UART 任务， C11 用 MinGW 进行主机测试 GCC, Python 验证运行器， WebRTC 信令， COM5 硬件验证。

---

## 文件结构

- 创建 `solutions/doorbell_local/main/presence_fusion.h`: 公开配置， 状态， 事件， 结果， 和处理 API。
- 创建 `solutions/doorbell_local/main/presence_fusion.c`: 区域过滤， 方向分类， 解除状态机器， 和安全时间。
- 创建 `solutions/doorbell_local/tests/presence_fusion_test.c`: 使用合成框架和时间戳进行确定性状态机测试。
- 创建 `solutions/doorbell_local/tests/verify_presence_fusion.py`: 编译并执行主机侧存在测试。
- 创建 `solutions/doorbell_local/main/doorbell_ring_policy.h`: 护戒 decision/result 合同。
- 创建 `solutions/doorbell_local/main/doorbell_ring_policy.c`: 纯 readiness/busy 决定和结果名称。
- 创建 `solutions/doorbell_local/tests/doorbell_ring_policy_test.c`: 主机测试准备就绪， 未准备好， 和繁忙的决定。
- 创建 `solutions/doorbell_local/tests/verify_doorbell_ring_policy.py`: 汇编并执行环政策测试。
- 修改 `solutions/doorbell_local/main/radar_ld2450.h`: 允许应用程序接收每个有效的解析框架。
- 修改 `solutions/doorbell_local/main/radar_ld2450.c`: 保留一次性原始样本， 停止每帧诊断垃圾邮件， 和前进框架。
- 修改 `solutions/doorbell_local/main/common.h`: 打开门铃请求。
- 修改 `solutions/doorbell_local/main/webrtc.c`: 路径 BOOT, CLI, 和雷达请求 通过一个安全环功能。
- 修改 `solutions/doorbell_local/main/main.c`: 拥有核聚变状态并处理已确认的雷达进入事件。
- 修改 `solutions/doorbell_local/main/CMakeLists.txt`: 汇编两个新的生产单元。
- 修改 `solutions/doorbell_local/tests/verify_radar_ld2450.py`: 更新新召回流程的整合合同检查。
- 修改 `docs/bringup/radar-test-matrix.md`: 记录任务 8.2 参数和实际设备结果。
- 修改 `C:/Users/dell/Documents/Codex/2026-07-24/new-chat/docs/superpowers/plans/2026-08-09-webrtc-doorbell-bringup.md`: 标记 8.2 仅在物理验证后完成。

### 任务1: 添加失败的存在核聚变测试

**文件:**
- 创建: `solutions/doorbell_local/tests/presence_fusion_test.c`
- 创建: `solutions/doorbell_local/tests/verify_presence_fusion.py`
- 测试: `solutions/doorbell_local/tests/presence_fusion_test.c`

- [ ] **步骤1: 写入状态机测试可执行文件**

创建 `solutions/doorbell_local/tests/presence_fusion_test.c`：

```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "presence_fusion.h"

static ld2450_frame_t frame_with_target(size_t slot, int16_t x_mm,
                                        int16_t y_mm, int16_t speed_cm_s)
{
    ld2450_frame_t frame = {0};
    assert(slot < LD2450_MAX_TARGETS);
    frame.targets[slot] = (ld2450_target_t){
        .valid = true,
        .x_mm = x_mm,
        .y_mm = y_mm,
        .speed_cm_s = speed_cm_s,
        .distance_resolution_mm = 360,
    };
    frame.target_count = 1;
    return frame;
}

static void confirm_entry(presence_fusion_t *fusion, uint32_t start_ms,
                          const ld2450_frame_t *frame, bool should_ring)
{
    presence_fusion_result_t result =
        presence_fusion_process(fusion, frame, start_ms);
    assert(result.event == PRESENCE_EVENT_NONE);
    assert(result.state == PRESENCE_STATE_ENTERING);

    result = presence_fusion_process(fusion, frame, start_ms + 999U);
    assert(result.event == PRESENCE_EVENT_NONE);

    result = presence_fusion_process(fusion, frame, start_ms + 1000U);
    assert(result.event == PRESENCE_EVENT_ENTER_CONFIRMED);
    assert(result.state == PRESENCE_STATE_PRESENT);
    assert(result.ring_requested == should_ring);
}

static void confirm_leave(presence_fusion_t *fusion, uint32_t start_ms)
{
    ld2450_frame_t empty = {0};
    presence_fusion_result_t result =
        presence_fusion_process(fusion, &empty, start_ms);
    assert(result.event == PRESENCE_EVENT_NONE);
    assert(result.state == PRESENCE_STATE_EXITING);

    result = presence_fusion_process(fusion, &empty, start_ms + 1999U);
    assert(result.event == PRESENCE_EVENT_NONE);

    result = presence_fusion_process(fusion, &empty, start_ms + 2000U);
    assert(result.event == PRESENCE_EVENT_LEAVE_CONFIRMED);
    assert(result.state == PRESENCE_STATE_CLEAR);
    assert(!result.ring_requested);
}

static void test_defaults_zone_and_direction(void)
{
    presence_fusion_config_t cfg = presence_fusion_default_config();
    assert(cfg.x_min_mm == -1500);
    assert(cfg.x_max_mm == 1500);
    assert(cfg.y_min_mm == 300);
    assert(cfg.y_max_mm == 3000);
    assert(cfg.enter_confirm_ms == 1000U);
    assert(cfg.leave_confirm_ms == 2000U);
    assert(cfg.cooldown_ms == 10000U);
    assert(cfg.direction_deadband_cm_s == 10);

    ld2450_target_t edge = {.valid = true, .x_mm = -1500, .y_mm = 300};
    assert(presence_fusion_target_in_zone(&cfg, &edge));
    edge.x_mm = 1500;
    edge.y_mm = 3000;
    assert(presence_fusion_target_in_zone(&cfg, &edge));
    edge.x_mm = 1501;
    assert(!presence_fusion_target_in_zone(&cfg, &edge));
    edge.x_mm = 0;
    edge.y_mm = 299;
    assert(!presence_fusion_target_in_zone(&cfg, &edge));

    assert(presence_fusion_classify_direction(&cfg, -11) ==
           PRESENCE_DIRECTION_APPROACHING);
    assert(presence_fusion_classify_direction(&cfg, -10) ==
           PRESENCE_DIRECTION_STATIONARY);
    assert(presence_fusion_classify_direction(&cfg, 10) ==
           PRESENCE_DIRECTION_STATIONARY);
    assert(presence_fusion_classify_direction(&cfg, 11) ==
           PRESENCE_DIRECTION_LEAVING);
}

static void test_entry_is_time_debounced_and_only_rings_once(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t cfg = presence_fusion_default_config();
    assert(presence_fusion_init(&fusion, &cfg));
    ld2450_frame_t target = frame_with_target(0, 200, 1200, -30);

    confirm_entry(&fusion, 100U, &target, true);
    presence_fusion_result_t result =
        presence_fusion_process(&fusion, &target, 5000U);
    assert(result.event == PRESENCE_EVENT_NONE);
    assert(result.state == PRESENCE_STATE_PRESENT);
    assert(!result.ring_requested);
    assert(result.in_zone_count == 1U);
    assert(result.has_representative_target);
    assert(result.representative_slot == 0U);
    assert(result.direction == PRESENCE_DIRECTION_APPROACHING);
}

static void test_interrupted_entry_restarts_timer(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t cfg = presence_fusion_default_config();
    assert(presence_fusion_init(&fusion, &cfg));
    ld2450_frame_t target = frame_with_target(0, 0, 1000, 0);
    ld2450_frame_t empty = {0};

    presence_fusion_process(&fusion, &target, 0U);
    presence_fusion_result_t result =
        presence_fusion_process(&fusion, &empty, 999U);
    assert(result.state == PRESENCE_STATE_CLEAR);
    presence_fusion_process(&fusion, &target, 1100U);
    result = presence_fusion_process(&fusion, &target, 2099U);
    assert(result.event == PRESENCE_EVENT_NONE);
    result = presence_fusion_process(&fusion, &target, 2100U);
    assert(result.event == PRESENCE_EVENT_ENTER_CONFIRMED);
}

static void test_short_dropout_does_not_leave_or_rering(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t cfg = presence_fusion_default_config();
    assert(presence_fusion_init(&fusion, &cfg));
    ld2450_frame_t target = frame_with_target(0, 0, 1000, 0);
    ld2450_frame_t empty = {0};

    confirm_entry(&fusion, 0U, &target, true);
    presence_fusion_process(&fusion, &empty, 1001U);
    presence_fusion_result_t result =
        presence_fusion_process(&fusion, &target, 3000U);
    assert(result.event == PRESENCE_EVENT_NONE);
    assert(result.state == PRESENCE_STATE_PRESENT);
    assert(!result.ring_requested);
}

static void test_cooldown_suppresses_complete_visit(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t cfg = presence_fusion_default_config();
    assert(presence_fusion_init(&fusion, &cfg));
    ld2450_frame_t target = frame_with_target(0, 0, 1000, 0);

    confirm_entry(&fusion, 0U, &target, true);
    confirm_leave(&fusion, 1001U);
    confirm_entry(&fusion, 4000U, &target, false);
    confirm_leave(&fusion, 5001U);
    confirm_entry(&fusion, 17002U, &target, true);
}

static void test_any_in_zone_target_maintains_presence(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t cfg = presence_fusion_default_config();
    assert(presence_fusion_init(&fusion, &cfg));
    ld2450_frame_t first = frame_with_target(0, 0, 800, 0);
    ld2450_frame_t replacement = frame_with_target(2, 1000, 1800, 20);

    confirm_entry(&fusion, 0U, &first, true);
    presence_fusion_result_t result =
        presence_fusion_process(&fusion, &replacement, 1500U);
    assert(result.state == PRESENCE_STATE_PRESENT);
    assert(result.event == PRESENCE_EVENT_NONE);
    assert(result.representative_slot == 2U);
    assert(result.direction == PRESENCE_DIRECTION_LEAVING);
}

static void test_wrap_safe_entry_timer(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t cfg = presence_fusion_default_config();
    assert(presence_fusion_init(&fusion, &cfg));
    ld2450_frame_t target = frame_with_target(0, 0, 1000, 0);
    uint32_t start = UINT32_MAX - 500U;

    presence_fusion_result_t result =
        presence_fusion_process(&fusion, &target, start);
    assert(result.event == PRESENCE_EVENT_NONE);
    result = presence_fusion_process(&fusion, &target, 499U);
    assert(result.event == PRESENCE_EVENT_ENTER_CONFIRMED);
    assert(result.ring_requested);
}

int main(void)
{
    test_defaults_zone_and_direction();
    test_entry_is_time_debounced_and_only_rings_once();
    test_interrupted_entry_restarts_timer();
    test_short_dropout_does_not_leave_or_rering();
    test_cooldown_suppresses_complete_visit();
    test_any_in_zone_target_maintains_presence();
    test_wrap_safe_entry_timer();
    puts("PASS: presence fusion state machine");
    return 0;
}
```

- [ ] **步骤2: 写入主机测试运行器**

创建 `solutions/doorbell_local/tests/verify_presence_fusion.py`：

```python
"""Build and run the host-side presence fusion tests."""

from pathlib import Path
import os
import subprocess
import tempfile


SOLUTION_DIR = Path(__file__).resolve().parents[1]
MAIN_DIR = SOLUTION_DIR / "main"
TEST_DIR = Path(__file__).resolve().parent


def main() -> int:
    compiler = os.environ.get("CC", r"D:\mingw64\bin\gcc.exe")
    with tempfile.TemporaryDirectory(prefix="presence-fusion-test-") as temp_dir:
        executable = Path(temp_dir) / "presence_fusion_test.exe"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{MAIN_DIR}",
                str(TEST_DIR / "presence_fusion_test.c"),
                str(MAIN_DIR / "presence_fusion.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **步骤3: 运行新测试， 以验证它在执行前失败**

运行：

```powershell
python solutions/doorbell_local/tests/verify_presence_fusion.py
```

预期: 编译失败， 因为 `presence_fusion.h` 或 `presence_fusion.c` 不存在。

- [ ] **步骤4: 进行失败的测试**

```powershell
git add solutions/doorbell_local/tests/presence_fusion_test.c solutions/doorbell_local/tests/verify_presence_fusion.py
git commit -m "test: define radar presence fusion behavior"
```

### 任务2: 执行基于时间的存在状态机器

**文件:**
- 创建: `solutions/doorbell_local/main/presence_fusion.h`
- 创建: `solutions/doorbell_local/main/presence_fusion.c`
- 测试: `solutions/doorbell_local/tests/presence_fusion_test.c`

- [ ] **步骤1: 增加公众参与合同**

创建 `solutions/doorbell_local/main/presence_fusion.h`：

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "radar_ld2450.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRESENCE_STATE_CLEAR = 0,
    PRESENCE_STATE_ENTERING,
    PRESENCE_STATE_PRESENT,
    PRESENCE_STATE_EXITING,
} presence_state_t;

typedef enum {
    PRESENCE_EVENT_NONE = 0,
    PRESENCE_EVENT_ENTER_CONFIRMED,
    PRESENCE_EVENT_LEAVE_CONFIRMED,
} presence_event_t;

typedef enum {
    PRESENCE_DIRECTION_UNKNOWN = 0,
    PRESENCE_DIRECTION_APPROACHING,
    PRESENCE_DIRECTION_STATIONARY,
    PRESENCE_DIRECTION_LEAVING,
} presence_direction_t;

typedef struct {
    int16_t x_min_mm;
    int16_t x_max_mm;
    int16_t y_min_mm;
    int16_t y_max_mm;
    uint32_t enter_confirm_ms;
    uint32_t leave_confirm_ms;
    uint32_t cooldown_ms;
    int16_t direction_deadband_cm_s;
} presence_fusion_config_t;

typedef struct {
    presence_fusion_config_t config;
    presence_state_t state;
    uint32_t state_since_ms;
    bool cooldown_active;
    uint32_t cooldown_started_ms;
} presence_fusion_t;

typedef struct {
    presence_state_t state;
    presence_event_t event;
    bool state_changed;
    bool ring_requested;
    size_t in_zone_count;
    bool has_representative_target;
    size_t representative_slot;
    ld2450_target_t representative_target;
    presence_direction_t direction;
} presence_fusion_result_t;

presence_fusion_config_t presence_fusion_default_config(void);
bool presence_fusion_init(presence_fusion_t *fusion,
                          const presence_fusion_config_t *config);
bool presence_fusion_target_in_zone(const presence_fusion_config_t *config,
                                    const ld2450_target_t *target);
presence_direction_t presence_fusion_classify_direction(
    const presence_fusion_config_t *config, int16_t speed_cm_s);
presence_fusion_result_t presence_fusion_process(
    presence_fusion_t *fusion, const ld2450_frame_t *frame, uint32_t now_ms);
const char *presence_state_name(presence_state_t state);
const char *presence_event_name(presence_event_t event);
const char *presence_direction_name(presence_direction_t direction);

#ifdef __cplusplus
}
#endif
```

- [ ] **步骤2: 实施区域过滤和包安全状态过渡**

创建 `solutions/doorbell_local/main/presence_fusion.c`：

```c
#include "presence_fusion.h"

#include <limits.h>
#include <string.h>

static bool elapsed_at_least(uint32_t now_ms, uint32_t start_ms,
                             uint32_t duration_ms)
{
    return (uint32_t)(now_ms - start_ms) >= duration_ms;
}

presence_fusion_config_t presence_fusion_default_config(void)
{
    return (presence_fusion_config_t){
        .x_min_mm = -1500,
        .x_max_mm = 1500,
        .y_min_mm = 300,
        .y_max_mm = 3000,
        .enter_confirm_ms = 1000,
        .leave_confirm_ms = 2000,
        .cooldown_ms = 10000,
        .direction_deadband_cm_s = 10,
    };
}

bool presence_fusion_init(presence_fusion_t *fusion,
                          const presence_fusion_config_t *config)
{
    if (fusion == NULL || config == NULL ||
        config->x_min_mm > config->x_max_mm ||
        config->y_min_mm > config->y_max_mm ||
        config->enter_confirm_ms == 0 || config->leave_confirm_ms == 0 ||
        config->direction_deadband_cm_s < 0) {
        return false;
    }
    memset(fusion, 0, sizeof(*fusion));
    fusion->config = *config;
    fusion->state = PRESENCE_STATE_CLEAR;
    return true;
}

bool presence_fusion_target_in_zone(const presence_fusion_config_t *config,
                                    const ld2450_target_t *target)
{
    return config != NULL && target != NULL && target->valid &&
           target->x_mm >= config->x_min_mm &&
           target->x_mm <= config->x_max_mm &&
           target->y_mm >= config->y_min_mm &&
           target->y_mm <= config->y_max_mm;
}

presence_direction_t presence_fusion_classify_direction(
    const presence_fusion_config_t *config, int16_t speed_cm_s)
{
    if (config == NULL) {
        return PRESENCE_DIRECTION_UNKNOWN;
    }
    if (speed_cm_s < -config->direction_deadband_cm_s) {
        return PRESENCE_DIRECTION_APPROACHING;
    }
    if (speed_cm_s > config->direction_deadband_cm_s) {
        return PRESENCE_DIRECTION_LEAVING;
    }
    return PRESENCE_DIRECTION_STATIONARY;
}

static presence_fusion_result_t inspect_frame(
    const presence_fusion_config_t *config, const ld2450_frame_t *frame)
{
    presence_fusion_result_t result = {
        .direction = PRESENCE_DIRECTION_UNKNOWN,
        .representative_slot = SIZE_MAX,
    };
    int16_t nearest_y = INT16_MAX;
    for (size_t i = 0; i < LD2450_MAX_TARGETS; ++i) {
        const ld2450_target_t *target = &frame->targets[i];
        if (!presence_fusion_target_in_zone(config, target)) {
            continue;
        }
        result.in_zone_count++;
        if (!result.has_representative_target || target->y_mm < nearest_y) {
            result.has_representative_target = true;
            result.representative_slot = i;
            result.representative_target = *target;
            nearest_y = target->y_mm;
        }
    }
    if (result.has_representative_target) {
        result.direction = presence_fusion_classify_direction(
            config, result.representative_target.speed_cm_s);
    }
    return result;
}

presence_fusion_result_t presence_fusion_process(
    presence_fusion_t *fusion, const ld2450_frame_t *frame, uint32_t now_ms)
{
    presence_fusion_result_t result = {
        .state = PRESENCE_STATE_CLEAR,
        .direction = PRESENCE_DIRECTION_UNKNOWN,
        .representative_slot = SIZE_MAX,
    };
    if (fusion == NULL || frame == NULL) {
        return result;
    }

    result = inspect_frame(&fusion->config, frame);
    presence_state_t previous_state = fusion->state;
    bool occupied = result.in_zone_count > 0;
    if (fusion->cooldown_active &&
        elapsed_at_least(now_ms, fusion->cooldown_started_ms,
                         fusion->config.cooldown_ms)) {
        fusion->cooldown_active = false;
    }

    switch (fusion->state) {
        case PRESENCE_STATE_CLEAR:
            if (occupied) {
                fusion->state = PRESENCE_STATE_ENTERING;
                fusion->state_since_ms = now_ms;
            }
            break;
        case PRESENCE_STATE_ENTERING:
            if (!occupied) {
                fusion->state = PRESENCE_STATE_CLEAR;
            } else if (elapsed_at_least(now_ms, fusion->state_since_ms,
                                        fusion->config.enter_confirm_ms)) {
                fusion->state = PRESENCE_STATE_PRESENT;
                result.event = PRESENCE_EVENT_ENTER_CONFIRMED;
                result.ring_requested = !fusion->cooldown_active;
            }
            break;
        case PRESENCE_STATE_PRESENT:
            if (!occupied) {
                fusion->state = PRESENCE_STATE_EXITING;
                fusion->state_since_ms = now_ms;
            }
            break;
        case PRESENCE_STATE_EXITING:
            if (occupied) {
                fusion->state = PRESENCE_STATE_PRESENT;
            } else if (elapsed_at_least(now_ms, fusion->state_since_ms,
                                        fusion->config.leave_confirm_ms)) {
                fusion->state = PRESENCE_STATE_CLEAR;
                fusion->cooldown_active = true;
                fusion->cooldown_started_ms = now_ms;
                result.event = PRESENCE_EVENT_LEAVE_CONFIRMED;
            }
            break;
        default:
            fusion->state = PRESENCE_STATE_CLEAR;
            break;
    }

    result.state = fusion->state;
    result.state_changed = fusion->state != previous_state;
    return result;
}

const char *presence_state_name(presence_state_t state)
{
    static const char *const names[] = {"CLEAR", "ENTERING", "PRESENT",
                                        "EXITING"};
    return state <= PRESENCE_STATE_EXITING ? names[state] : "UNKNOWN";
}

const char *presence_event_name(presence_event_t event)
{
    static const char *const names[] = {"NONE", "ENTER_CONFIRMED",
                                        "LEAVE_CONFIRMED"};
    return event <= PRESENCE_EVENT_LEAVE_CONFIRMED ? names[event] : "UNKNOWN";
}

const char *presence_direction_name(presence_direction_t direction)
{
    static const char *const names[] = {"UNKNOWN", "APPROACHING", "STATIONARY",
                                        "LEAVING"};
    return direction <= PRESENCE_DIRECTION_LEAVING ? names[direction]
                                                    : "UNKNOWN";
}
```

- [ ] **步骤3: 运行主机测试并验证它是否通过**

运行：

```powershell
python solutions/doorbell_local/tests/verify_presence_fusion.py
```

预期: `PASS: presence fusion state machine`。

- [ ] **步骤4: 提交状态机器**

```powershell
git add solutions/doorbell_local/main/presence_fusion.c solutions/doorbell_local/main/presence_fusion.h
git commit -m "feat: add radar presence fusion state machine"
```

### 任务3: 添加可测试主机门铃政策

**文件:**
- 创建: `solutions/doorbell_local/main/doorbell_ring_policy.h`
- 创建: `solutions/doorbell_local/main/doorbell_ring_policy.c`
- 创建: `solutions/doorbell_local/tests/doorbell_ring_policy_test.c`
- 创建: `solutions/doorbell_local/tests/verify_doorbell_ring_policy.py`

- [ ] **步骤1: 写入失败的环政策测试**

创建 `solutions/doorbell_local/tests/doorbell_ring_policy_test.c`：

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "doorbell_ring_policy.h"

int main(void)
{
    assert(doorbell_ring_policy_evaluate(false, true) ==
           DOORBELL_RING_NOT_READY);
    assert(doorbell_ring_policy_evaluate(true, false) ==
           DOORBELL_RING_BUSY);
    assert(doorbell_ring_policy_evaluate(true, true) ==
           DOORBELL_RING_ACCEPTED);
    assert(strcmp(doorbell_ring_result_name(DOORBELL_RING_SEND_FAILED),
                  "SEND_FAILED") == 0);
    puts("PASS: doorbell ring policy");
    return 0;
}
```

创建 `solutions/doorbell_local/tests/verify_doorbell_ring_policy.py`：

```python
"""Build and run the host-side doorbell ring policy tests."""

from pathlib import Path
import os
import subprocess
import tempfile


SOLUTION_DIR = Path(__file__).resolve().parents[1]
MAIN_DIR = SOLUTION_DIR / "main"
TEST_DIR = Path(__file__).resolve().parent


def main() -> int:
    compiler = os.environ.get("CC", r"D:\mingw64\bin\gcc.exe")
    with tempfile.TemporaryDirectory(prefix="doorbell-ring-policy-") as temp_dir:
        executable = Path(temp_dir) / "doorbell_ring_policy_test.exe"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{MAIN_DIR}",
                str(TEST_DIR / "doorbell_ring_policy_test.c"),
                str(MAIN_DIR / "doorbell_ring_policy.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **步骤2: 在执行前运行测试以验证失败**

运行：

```powershell
python solutions/doorbell_local/tests/verify_doorbell_ring_policy.py
```

预期: 编译失败， 因为 `doorbell_ring_policy.h` 或 `.c` 不存在。

- [ ] **步骤3: 执行政策和结果合同**

创建 `solutions/doorbell_local/main/doorbell_ring_policy.h`：

```c
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOORBELL_RING_ACCEPTED = 0,
    DOORBELL_RING_NOT_READY,
    DOORBELL_RING_BUSY,
    DOORBELL_RING_SEND_FAILED,
} doorbell_ring_request_result_t;

doorbell_ring_request_result_t doorbell_ring_policy_evaluate(
    bool webrtc_ready, bool doorbell_idle);
const char *doorbell_ring_result_name(doorbell_ring_request_result_t result);

#ifdef __cplusplus
}
#endif
```

创建 `solutions/doorbell_local/main/doorbell_ring_policy.c`：

```c
#include "doorbell_ring_policy.h"

doorbell_ring_request_result_t doorbell_ring_policy_evaluate(
    bool webrtc_ready, bool doorbell_idle)
{
    if (!webrtc_ready) {
        return DOORBELL_RING_NOT_READY;
    }
    if (!doorbell_idle) {
        return DOORBELL_RING_BUSY;
    }
    return DOORBELL_RING_ACCEPTED;
}

const char *doorbell_ring_result_name(doorbell_ring_request_result_t result)
{
    static const char *const names[] = {"ACCEPTED", "NOT_READY", "BUSY",
                                        "SEND_FAILED"};
    return result <= DOORBELL_RING_SEND_FAILED ? names[result] : "UNKNOWN";
}
```

- [ ] **步骤4: 进行环政策测试，验证通过**

运行：

```powershell
python solutions/doorbell_local/tests/verify_doorbell_ring_policy.py
```

预期: `PASS: doorbell ring policy`。

- [ ] **步骤5: 提交环策略**

```powershell
git add solutions/doorbell_local/main/doorbell_ring_policy.c solutions/doorbell_local/main/doorbell_ring_policy.h solutions/doorbell_local/tests/doorbell_ring_policy_test.c solutions/doorbell_local/tests/verify_doorbell_ring_policy.py
git commit -m "feat: guard doorbell ring requests"
```

### 任务4: 将已解析的雷达框架与现有的 RING 路径

**文件:**
- 修改: `solutions/doorbell_local/main/radar_ld2450.h`
- 修改: `solutions/doorbell_local/main/radar_ld2450.c`
- 修改: `solutions/doorbell_local/main/common.h`
- 修改: `solutions/doorbell_local/main/webrtc.c`
- 修改: `solutions/doorbell_local/main/main.c`
- 修改: `solutions/doorbell_local/main/CMakeLists.txt`
- 修改: `solutions/doorbell_local/tests/verify_radar_ld2450.py`

- [ ] **步骤1: 让现有的雷达合同检查在新流量上失败**

内 `solutions/doorbell_local/tests/verify_radar_ld2450.py`, 将现有的生产控件断言改为：：

```python
    required_tokens = {
        MAIN_DIR / "radar_ld2450.h": (
            "LD2450_UART_BAUD_RATE 256000",
            "LD2450_UART_RX_GPIO 4",
            "LD2450_UART_TX_GPIO 5",
            "radar_ld2450_init(ld2450_frame_callback_t callback, void *ctx)",
        ),
        MAIN_DIR / "radar_ld2450.c": (
            "uart_read_bytes",
            "ld2450_stream_feed",
            "app_frame_callback(raw_frame, frame, app_frame_callback_ctx)",
            "No valid LD2450 frame for",
        ),
        MAIN_DIR / "CMakeLists.txt": (
            '"radar_ld2450.c"',
            '"presence_fusion.c"',
            '"doorbell_ring_policy.c"',
        ),
        MAIN_DIR / "main.c": (
            '#include "presence_fusion.h"',
            "presence_fusion_process",
            'doorbell_request_ring("radar")',
            "radar_ld2450_init(radar_frame_received, NULL)",
        ),
        MAIN_DIR / "webrtc.c": (
            "doorbell_request_ring",
            "doorbell_ring_policy_evaluate",
            "DOORBELL_RING_SEND_FAILED",
        ),
    }
```

运行：

```powershell
python solutions/doorbell_local/tests/verify_radar_ld2450.py
```

预期: 失败 因为召回， 聚变， 和守护的环融合标志 还没有出现。

- [ ] **步骤2: 从雷达上曝光帧交付 UART 任务**

改变 ESP-仅声明在 `radar_ld2450.h` 改为：

```c
#ifdef ESP_PLATFORM
esp_err_t radar_ld2450_init(ld2450_frame_callback_t callback, void *ctx);
#endif
```

内 `radar_ld2450.c`, 在旁边添加应用程序召回存储 `raw_sample_tracker`：

```c
static ld2450_frame_callback_t app_frame_callback;
static void *app_frame_callback_ctx;
```

替换 `log_frame` 使用此焦点处理器：

```c
static void handle_frame(const uint8_t raw_frame[LD2450_FRAME_SIZE],
                         const ld2450_frame_t *frame, void *ctx)
{
    (void)ctx;
    if (ld2450_should_capture_sample(&raw_sample_tracker,
                                     frame->target_count)) {
        ESP_LOGI(TAG, "Raw frame sample for %u target(s)",
                 (unsigned)frame->target_count);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, raw_frame, LD2450_FRAME_SIZE,
                                 ESP_LOG_INFO);
    }
    if (app_frame_callback != NULL) {
        app_frame_callback(raw_frame, frame, app_frame_callback_ctx);
    }
}
```

过 `handle_frame` 改为 `ld2450_stream_feed`, 更改初始化签名， 并立即在 UART 任务开始：

```c
esp_err_t radar_ld2450_init(ld2450_frame_callback_t callback, void *ctx)
{
    if (uart_initialized) {
        return ESP_OK;
    }

    const uart_config_t uart_config = {
        .baud_rate = LD2450_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_param_config(LD2450_UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = uart_set_pin(LD2450_UART_PORT, LD2450_UART_TX_GPIO,
                       LD2450_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = uart_driver_install(LD2450_UART_PORT, LD2450_UART_RX_BUFFER_SIZE,
                              0, 0, NULL, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    app_frame_callback = callback;
    app_frame_callback_ctx = ctx;
    if (xTaskCreate(radar_uart_task, "ld2450_uart", LD2450_TASK_STACK_SIZE,
                    NULL, LD2450_TASK_PRIORITY, NULL) != pdPASS) {
        app_frame_callback = NULL;
        app_frame_callback_ctx = NULL;
        uart_driver_delete(LD2450_UART_PORT);
        return ESP_ERR_NO_MEM;
    }

    uart_initialized = true;
    ESP_LOGI(TAG, "UART%d ready at %d baud (RX GPIO%d, TX GPIO%d)",
             (int)LD2450_UART_PORT, LD2450_UART_BAUD_RATE,
             LD2450_UART_RX_GPIO, LD2450_UART_TX_GPIO);
    return ESP_OK;
}
```

现有任务呼叫变为：

```c
static void radar_uart_task(void *arg)
{
    (void)arg;
    uint8_t data[LD2450_READ_CHUNK_SIZE];
    ld2450_stream_decoder_t decoder;
    ld2450_stream_decoder_init(&decoder);
    TickType_t last_valid_frame_tick = xTaskGetTickCount();
    bool offline_reported = false;

    while (true) {
        int length = uart_read_bytes(LD2450_UART_PORT, data, sizeof(data),
                                     pdMS_TO_TICKS(100));
        if (length > 0) {
            size_t emitted = ld2450_stream_feed(
                &decoder, data, (size_t)length, handle_frame, NULL);
            if (emitted > 0) {
                last_valid_frame_tick = xTaskGetTickCount();
                if (offline_reported) {
                    ESP_LOGI(TAG, "Valid frame stream restored");
                    offline_reported = false;
                }
            }
        }
        if (!offline_reported &&
            (xTaskGetTickCount() - last_valid_frame_tick) >=
                pdMS_TO_TICKS(2000)) {
            ESP_LOGW(TAG, "No valid LD2450 frame for 2000 ms");
            offline_reported = true;
        }
    }
}
```

- [ ] **步骤3: 增加守卫 WebRTC 环请求**

内 `common.h`, 包括政策并宣布请求：

```c
#include "doorbell_ring_policy.h"

doorbell_ring_request_result_t doorbell_request_ring(const char *source);
```

内 `webrtc.c`, 包含 `doorbell_ring_policy.h` 添加：

```c
doorbell_ring_request_result_t doorbell_request_ring(const char *source)
{
    doorbell_ring_request_result_t decision = doorbell_ring_policy_evaluate(
        webrtc != NULL, door_bell_state == DOOR_BELL_STATE_NONE);
    if (decision != DOORBELL_RING_ACCEPTED) {
        ESP_LOGW(TAG, "Ring request from %s suppressed: %s",
                 source ? source : "unknown",
                 doorbell_ring_result_name(decision));
        return decision;
    }

    int ret = SEND_CMD(webrtc, DOOR_BELL_RING_CMD);
    if (ret != 0) {
        ESP_LOGE(TAG, "Ring request from %s failed: %d",
                 source ? source : "unknown", ret);
        return DOORBELL_RING_SEND_FAILED;
    }

    door_bell_state = DOOR_BELL_STATE_RINGING;
    ret = play_tone(DOOR_BELL_TONE_RING);
    if (ret != 0) {
        ESP_LOGE(TAG, "Ring tone playback failed: %d", ret);
    }
    ESP_LOGI(TAG, "Ring request from %s accepted",
             source ? source : "unknown");
    return DOORBELL_RING_ACCEPTED;
}
```

替换 `ring` 分支 `send_cmd` 与：

```c
if (SAME_STR(cmd, "ring")) {
    (void)doorbell_request_ring("command");
}
```

替换 BOOT-密钥调用：

```c
(void)doorbell_request_ring("boot");
```

- [ ] **步骤4: 连接核聚变状态机器 `main.c`**

在附近添加包含和模块状态 `TAG`：

```c
#include "presence_fusion.h"

static presence_fusion_t radar_presence;
```

添加雷达帧回调：

```c
static void radar_frame_received(
    const uint8_t raw_frame[LD2450_FRAME_SIZE],
    const ld2450_frame_t *frame, void *ctx)
{
    (void)raw_frame;
    (void)ctx;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    presence_fusion_result_t result =
        presence_fusion_process(&radar_presence, frame, now_ms);

    if (result.state_changed || result.event != PRESENCE_EVENT_NONE) {
        ESP_LOGI(TAG,
                 "Radar %s: state=%s targets=%u direction=%s x=%d y=%d ring=%s",
                 presence_event_name(result.event),
                 presence_state_name(result.state),
                 (unsigned)result.in_zone_count,
                 presence_direction_name(result.direction),
                 result.has_representative_target
                     ? (int)result.representative_target.x_mm
                     : 0,
                 result.has_representative_target
                     ? (int)result.representative_target.y_mm
                     : 0,
                 result.ring_requested ? "requested" : "no");
    }
    if (result.ring_requested) {
        doorbell_ring_request_result_t ring_result =
            doorbell_request_ring("radar");
        if (ring_result != DOORBELL_RING_ACCEPTED) {
            ESP_LOGW(TAG, "Radar ring not delivered: %s",
                     doorbell_ring_result_name(ring_result));
        }
    }
}
```

在雷达发射之前 `app_main`, 初始化状态机器并替换当前无参数雷达初始化器：

```c
presence_fusion_config_t presence_config = presence_fusion_default_config();
if (!presence_fusion_init(&radar_presence, &presence_config)) {
    ESP_LOGE(TAG, "Presence fusion initialization failed");
}
esp_err_t radar_ret = radar_ld2450_init(radar_frame_received, NULL);
if (radar_ret != ESP_OK) {
    ESP_LOGE(TAG, "LD2450 initialization failed: %s",
             esp_err_to_name(radar_ret));
}
```

- [ ] **步骤5: 汇编新的生产文件并更新合同检查**

改变 `srcs` 列表中 `main/CMakeLists.txt` 改为：

```cmake
set(srcs "webrtc.c" "main.c" "board.c" "media_sys.c" "webrtc_http_server.c"
         "door_lock.c" "door_lock_state.c" "radar_ld2450.c"
         "presence_fusion.c" "doorbell_ring_policy.c")
```

写完 `verify_radar_ld2450.py` 步骤1的符号变化， 然后运行：

```powershell
python solutions/doorbell_local/tests/verify_radar_ld2450.py
python solutions/doorbell_local/tests/verify_presence_fusion.py
python solutions/doorbell_local/tests/verify_doorbell_ring_policy.py
```

预期：

```text
PASS: LD2450 protocol parser
PASS: presence fusion state machine
PASS: doorbell ring policy
```

- [ ] **步骤6: 执行整合**

```powershell
git add solutions/doorbell_local/main/CMakeLists.txt solutions/doorbell_local/main/common.h solutions/doorbell_local/main/main.c solutions/doorbell_local/main/radar_ld2450.c solutions/doorbell_local/main/radar_ld2450.h solutions/doorbell_local/main/webrtc.c solutions/doorbell_local/tests/verify_radar_ld2450.py
git commit -m "feat: trigger doorbell from confirmed radar presence"
```

### 任务5: 运行回归测试并构建 ESP32-P4 固态软件

**文件:**
- 测试: `solutions/doorbell_local/tests/verify_presence_fusion.py`
- 测试: `solutions/doorbell_local/tests/verify_doorbell_ring_policy.py`
- 测试: `solutions/doorbell_local/tests/verify_radar_ld2450.py`
- 测试: `solutions/doorbell_local/tests/verify_door_lock.py`
- 测试: `solutions/doorbell_local/tests/verify_wt99p4c5_config.py`

- [ ] **步骤1: 运行所有主机和源合同测试**

```powershell
python solutions/doorbell_local/tests/verify_presence_fusion.py
python solutions/doorbell_local/tests/verify_doorbell_ring_policy.py
python solutions/doorbell_local/tests/verify_radar_ld2450.py
python solutions/doorbell_local/tests/verify_door_lock.py
python solutions/doorbell_local/tests/verify_wt99p4c5_config.py
```

预期: 所有脚本以代码 0 退出并打印其 `PASS` 信件。

- [ ] **步骤2: 装入 ESP-IDF 5.5.5 和构建**

从PowerShell 运行：

```powershell
$env:IDF_PYTHON_ENV_PATH = 'E:\Espressif\python_env\idf5.5_py3.11_env'
. 'E:\Espressif\frameworks\esp-idf-v5.5.5\export.ps1'
Set-Location 'C:\Users\dell\wt99-webrtc-native\solutions\doorbell_local'
idf.py build
```

预期: `Project build complete` 并且没有向错误推广编译器警告。

- [ ] **步骤3: 检查确切的更改集**

```powershell
Set-Location 'C:\Users\dell\wt99-webrtc-native'
git diff --check
git status --short
```

预期: `git diff --check` 出口 0. 保留所有先前存在的任务 4-8 更改; 不丢弃或覆盖无关文件。

### 任务6: 烧录 COM5 并进行实际接受

**文件:**
- 修改: `docs/bringup/radar-test-matrix.md`
- 接受后修改: `C:/Users/dell/Documents/Codex/2026-07-24/new-chat/docs/superpowers/plans/2026-08-09-webrtc-doorbell-bringup.md`

- [ ] **步骤1: 将已建成的固件闪到 ESP32-P4**

```powershell
Set-Location 'C:\Users\dell\wt99-webrtc-native\solutions\doorbell_local'
idf.py -p COM5 flash monitor
```

预期启动证据：

```text
LD2450: UART1 ready at 256000 baud (RX GPIO4, TX GPIO5)
Use browser to enter https://<board-ip>/webrtc/test for test
```

- [ ] **步骤2: 验证五种物理情景**

打开浏览器测试页面并连接信令， 执行这些顺序：

1. 不到1秒就穿过这个区域: 没有来电 没有响声。
2. 站在区内至少1秒钟: 一个来电和一个板环音。
3. 再待10秒钟: 没有第二次来电。
4. 离开不到2秒返回: 没有第二次来电。
5. 离开至少2秒， 在10秒冷却时重新进入， 冷却后用新鲜的 leave/re-entry: 冷却访问是静默的，后来的访问响一次。

预期序列证据包括一项 `ENTER_CONFIRMED`, 一个接受的雷达环 合格访问， `LEAVE_CONFIRMED` 2秒后， 并在适用的情况下明确冷却或繁忙压制. 不能重设， 监督， 或崩溃输出。

- [ ] **步骤3: 重复三次正常访问**

每轮， 等待冷却， 输入超过1秒， 回答或挂断， 离开超过2秒。

预期: 每轮接通1个电话和1个铃声; 联合国 WebRTC 页和板保持响应。

- [ ] **步骤4: 记录核实的结果**

将本节添加为 `docs/bringup/radar-test-matrix.md`, 只用观察到的结果来取代每个结果：

```markdown
## 任务 8.2：区域去抖与响铃

- 固件日期：2026-09-05
- 区域：x=-1500..1500 毫米，y=300..3000 毫米
- 进入确认时间：1000 毫秒
- 离开确认时间：2000 毫秒
- 冷却时间：确认离开后 10000 毫秒
- 快速经过（<1 秒）：通过/失败
- 持续进入（>=1 秒）：通过/失败
- 持续存在时不重复响铃：通过/失败
- 短暂丢失（<2 秒）时不重复响铃：通过/失败
- 冷却期间抑制再次进入：通过/失败
- 冷却结束后重新进入时响铃一次：通过/失败
- 连续三次符合条件的进入：通过/失败
- 是否观察到复位、看门狗或崩溃：是/否
```

- [ ] **步骤5: 标记任务 8.2 在所有物理结果通过后才完成**

内 `C:/Users/dell/Documents/Codex/2026-07-24/new-chat/docs/superpowers/plans/2026-08-09-webrtc-doorbell-bringup.md`, 变动：

```markdown
- [ ] 8.2 建立空间区域与去抖
```

改为：

```markdown
- [x] 8.2 建立空间区域与去抖
```

如果任何物理案例失败， 打开复选框， 保留序列日志， 在重运行失败的个案和三轮回归之前， 修正最小的显示原因。

- [ ] **步骤6: 提交已核实的任务 8.2 记录**

```powershell
Set-Location 'C:\Users\dell\wt99-webrtc-native'
git add docs/bringup/radar-test-matrix.md
git commit -m "docs: record radar ringing acceptance results"
```
