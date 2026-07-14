# A1 Simulator Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在复用 Phase 1 ModelIR / LValue / LogicValue / ContinuousEvaluator 的前提下，实现 Active→NBA→Timed 事件调度、四态边沿检测，以及 flat-top 过程块 lowering 与解释执行垂直切片。

**Architecture:** `runtime/edge` 只负责单 bit 的 0/1/X/Z 边沿判定；多位信号的显式边沿按 IEEE 1364-2005 §9.7.2 只看 LSB。`runtime/scheduler` 拥有仿真时间、Active 队列、`#0` pending 队列、NBA 暂存、Timed 队列与 per-process 状态机（仅 `waiting_at_sensitivity` 可被边沿/`@*` 唤醒）。连续赋值更新不抢占运行中的 process：Active 排空后调用扩展后的 `ContinuousEvaluator`（DAG 一次 settle；环则确定性 delta 迭代），再做边沿/`@*` 唤醒；`#0` 续体在 settle/唤醒稳定后才回到 Active；NBA 仅在 Active 与 `#0` pending 皆空时提交。`ModelIR` 增加 Process / Statement；`src/frontend` 在 flat-top 上 lower `initial` / `always @(*)` / `always @(posedge|negedge|AnyChange 事件或)` 与 blocking/NBA/`#delay`/`$finish`。垂直切片通过冻结 API `run_model(ModelIR, SimOptions) → SimResult` 验证，不引入 codegen、`compile_sim`/`run`、hierarchy 或文件系统任务。

**Tech Stack:** C++20、CMake 3.28+、GNU Make、Slang 11.0（commit `8acc660a20b70de48ecec1c7471863e6f4b3ae6f`）、Python 3.10+、CTest。

## Resolved Decisions (grilling)

| 主题 | 决议 |
|---|---|
| `$finish` | 置位后不再取新 Active；提交**已入队** NBA；成功返回 |
| 多位边沿 | IEEE §9.7.2：只看信号 **LSB**；`detect_edge` 仍为 per-bit |
| `if` / X/Z | IEEE §9.4：`truth_value` 已知非零→then；0/x/z 侧→else |
| 连续赋值时机 | 不抢占 process；Active 排空后 settle；再唤醒 |
| `#0` | 进同槽 **pending**；Active+settle+唤醒稳定后才回 Active；有 pending 时不提交 NBA |
| `@*` | IEEE §9.7.5 填 `read_signals`；display stub 实参为已知差异 |
| 唤醒资格 | 仅 `waiting_at_sensitivity`；非「同 ProcessId 去重」措辞 |
| `always_comb/ff/latch` | 一律 unsupported |
| Initial 顺序 | `ProcessId` 升序（=声明序） |
| EventList | 事件或；`BothEdges` 拒 |
| `run_model` API | 冻结；`SimOptions` 可扩展 |
| identity `assign y=a` | Phase 2 不做 alias → `docs/adr/2026-07-14-identity-assign-alias-deferred.md` |

## Global Constraints

- 目标环境是离线 Linux x86_64；构建不得下载依赖。
- Slang 类型/头文件只能出现在 `A1-simulator/submission/src/frontend/`。`src/ir/`、`runtime/` 及其单元测试禁止 `#include` Slang。
- 复用 Phase 1 的表达式求值与 net resolution；不得另写一套连续赋值语义。`ContinuousEvaluator::settle` 对无环模型行为不变；环依赖改由带 delta 上限的确定性迭代收敛，超限报错。
- Phase 2 仍只接受一个 elaborated flat top：top 或 generate 内出现 child module/interface instance 必须 fail-closed，不返回部分 ModelIR。不实现 port binding。全宽 identity `assign y = a` 的 net-alias 语义 Phase 2 不做（读侧可观测为旧值直到 Active 排空后 settle）；见 `docs/adr/2026-07-14-identity-assign-alias-deferred.md`，Phase 3 port lowering 再统一为 alias。
- Phase 2 过程语义仅覆盖：`initial`、带显式 `SignalEvent`（posedge/negedge/AnyChange）或 `ImplicitEvent`（`@*` / `@(*)`）的 **`always`**、seq block、blocking / non-blocking 赋值、常量 `#delay`（含 `#0`）、`if/else`、`$finish`。`always_comb` / `always_ff` / `always_latch`、`edge`（BothEdges）、`case` / `for` / `while` / `wait` / `repeat`、文件 I/O、memory 过程目标一律 source-located unsupported。
- `$display` / `$error` 在 Phase 2 允许作为无参 `DisplayStubStmt` lower（便于 fixture），但不得假装完成文件 I/O。已知差异：仅出现在 display 实参中的信号不进入 `@*` 的 `read_signals`（stub 丢弃实参；多唤醒一次对 `output.mem` 不可观测）。
- 调度分层（同槽，对齐 iverilog 可观测行为；不实现完整 IEEE 五区名字）：(1) 排空 Active（process run-to-completion；连续赋值更新事件不抢占运行中 process）；(2) `settle_with_limit`；(3) 边沿 / `@*` 唤醒（仅 `waiting_at_sensitivity` 的 process）——循环直到无新唤醒；(4) 将 `#0` pending 续体移回 Active，回到 (1)；(5) Active 与 `#0` pending 皆空后才提交 NBA；若 NBA 导致变更则再 settle/唤醒并可能回到 Active。`$finish`：置位后不再取出新的 Active process；提交**已入队** NBA 后成功返回；不推进后续 Timed。
- 多个 `initial` 按 `ProcessId` 升序入 Active（= lowering 序 = 源声明序，对齐 iverilog）；同槽多 initial 的可观测次序依赖此确定性顺序，不保证与任意其它仿真器 bit 级一致。
- Timed（`#n` 且 `n>0`）按仿真时间升序；同一时间槽内按入队顺序确定性执行。时间单位固定为 1 tick = 1 延迟字面量；不解析 `` `timescale`` 换算。
- 设置 `delta_limit`（默认 `10000`）；组合环不收敛时返回 `delta cycle limit exceeded`（优先于继续推进时间）。`time_limit` 默认 `1000000`。
- 公开 API 冻结：`run_model(const ir::ModelIR&, const SimOptions&) → SimResult`。`SimOptions` 为后续扩展点（如 P4 `THREADS`）；P3+ 只扩展 ModelIR/前端，不改调度主语义。
- 不实现 `compile_sim` / `run` / `parallel_run`、codegen、cache、memory、真正并行。公开 `basic01`/`basic02` 含 child instance，不是本阶段 golden 门禁；门禁是专用 flat-top fixture + 单元测试。
- TOP / filelist / includes / defines 仍是调用方输入；禁止 case 名特判。

---

## File Map

```text
A1-simulator/submission/
├── CMakeLists.txt
├── Makefile
├── runtime/
│   ├── value.h / value.cpp                    # 复用
│   ├── continuous_evaluator.h / .cpp          # 扩展：propagate_delta / settle_with_limit
│   ├── edge.h / edge.cpp                      # 新建
│   └── scheduler.h / scheduler.cpp            # 新建
├── src/
│   ├── frontend/
│   │   ├── frontend.h                         # 不变公开 API
│   │   └── frontend.cpp                       # 扩展过程块 lowering
│   └── ir/
│       ├── model_ir.h / model_ir.cpp          # 扩展 Process / Statement
└── tests/
    ├── unit/
    │   ├── edge_test.cpp
    │   ├── scheduler_test.cpp
    │   └── ...                                # Phase 1 测试保持通过
    └── integration/
        └── flat_core_slice_test.cpp           # Verilog fixture → compile_to_ir → run_model
```

依赖方向：

```text
a1_value <- a1_ir <- a1_continuous_evaluator <- a1_scheduler
                ^                              ^
                |                              |
          a1_frontend + slang            a1_edge (← a1_value)
```

---

### Task 1: 注册 Phase 2 库与失败优先测试脚手架

**Files:**

- Create: `A1-simulator/submission/runtime/edge.h`
- Create: `A1-simulator/submission/runtime/edge.cpp`
- Create: `A1-simulator/submission/runtime/scheduler.h`
- Create: `A1-simulator/submission/runtime/scheduler.cpp`
- Create: `A1-simulator/submission/tests/unit/edge_test.cpp`
- Create: `A1-simulator/submission/tests/unit/scheduler_test.cpp`
- Create: `A1-simulator/submission/tests/integration/flat_core_slice_test.cpp`
- Modify: `A1-simulator/submission/CMakeLists.txt`
- Modify: `A1-simulator/submission/Makefile`

**Interfaces:**

- Consumes: 现有 `a1_value`、`a1_ir`、`a1_continuous_evaluator`、`a1_frontend`。
- Produces: `a1_edge`、`a1_scheduler`；`edge_test`、`scheduler_test`、`flat_core_slice_test`；`a1_phase2_tests` aggregate。

- [ ] **Step 1: 扩展 CMake**

在 `a1_continuous_evaluator` 之后加入：

```cmake
add_library(a1_edge STATIC runtime/edge.cpp)
target_include_directories(a1_edge PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(a1_edge PUBLIC a1_value)

add_library(a1_scheduler STATIC runtime/scheduler.cpp)
target_include_directories(a1_scheduler PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(a1_scheduler PUBLIC a1_continuous_evaluator a1_edge)

add_executable(edge_test tests/unit/edge_test.cpp)
target_include_directories(edge_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(edge_test PRIVATE a1_edge)
add_test(NAME edge_test COMMAND edge_test)

add_executable(scheduler_test tests/unit/scheduler_test.cpp)
target_include_directories(scheduler_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(scheduler_test PRIVATE a1_scheduler)
add_test(NAME scheduler_test COMMAND scheduler_test)

add_executable(flat_core_slice_test tests/integration/flat_core_slice_test.cpp)
target_include_directories(flat_core_slice_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(flat_core_slice_test PRIVATE a1_frontend a1_scheduler)
add_test(NAME flat_core_slice_test COMMAND flat_core_slice_test)

add_custom_target(a1_phase2_tests
  DEPENDS a1_phase1_tests edge_test scheduler_test flat_core_slice_test)
```

把 Makefile `test` recipe 的 build target 从 `a1_phase1_tests` 改为 `a1_phase2_tests`。

- [ ] **Step 2: 添加最小头/源与失败优先测试**

`runtime/edge.h`：

```cpp
#pragma once

#include "runtime/value.h"

namespace a1::runtime {

enum class EdgeKind : std::uint8_t { None, Posedge, Negedge };

[[nodiscard]] EdgeKind detect_edge(LogicValue::Bit from, LogicValue::Bit to);

}  // namespace a1::runtime
```

`runtime/edge.cpp` 先抛 `std::logic_error("unimplemented")`。

`runtime/scheduler.h`：

```cpp
#pragma once

#include "ir/model_ir.h"
#include "runtime/continuous_evaluator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace a1::runtime {

struct SimOptions {
    std::uint64_t delta_limit = 10000;
    std::uint64_t time_limit = 1000000;
};

struct SimResult {
    std::optional<std::string> error;
    std::uint64_t time = 0;
    bool finished = false;
    // 成功结束（含 $finish）时非空，供垂直切片断言最终信号值。
    std::shared_ptr<SignalStore> final_store;
};

[[nodiscard]] SimResult run_model(const ir::ModelIR& model, const SimOptions& options = {});

}  // namespace a1::runtime
```

`runtime/scheduler.cpp` 先返回 `{ "unimplemented", 0, false, nullptr }`。

三个测试文件各自 `#include` 对应头并调用 `detect_edge` / `run_model` / `compile_to_ir`，使链接通过但行为断言失败或返回 unimplemented。`edge_test`：

```cpp
#include "test_support.h"
#include "runtime/edge.h"

int main() {
    using a1::runtime::EdgeKind;
    using a1::runtime::LogicValue;
    using a1::runtime::detect_edge;
    A1_EXPECT(detect_edge(LogicValue::Bit::Zero, LogicValue::Bit::One) == EdgeKind::Posedge);
    return EXIT_SUCCESS;
}
```

- [ ] **Step 3: 配置并确认 edge_test 失败于 unimplemented / 错误结果**

Run:

```bash
cmake -S A1-simulator/submission -B A1-simulator/submission/.build-phase2
cmake --build A1-simulator/submission/.build-phase2 --target edge_test scheduler_test --parallel 4
ctest --test-dir A1-simulator/submission/.build-phase2 -R '^(edge_test|scheduler_test)$' --output-on-failure
```

Expected: 配置成功；`edge_test` 因抛异常或断言失败而 FAIL。

- [ ] **Step 4: Commit**

```bash
git add A1-simulator/submission/CMakeLists.txt A1-simulator/submission/Makefile \
  A1-simulator/submission/runtime/edge.h A1-simulator/submission/runtime/edge.cpp \
  A1-simulator/submission/runtime/scheduler.h A1-simulator/submission/runtime/scheduler.cpp \
  A1-simulator/submission/tests/unit/edge_test.cpp \
  A1-simulator/submission/tests/unit/scheduler_test.cpp \
  A1-simulator/submission/tests/integration/flat_core_slice_test.cpp
git commit -m "$(cat <<'EOF'
chore: scaffold Phase 2 edge and scheduler targets

EOF
)"
```

---

### Task 2: 四态边沿检测

**Files:**

- Modify: `A1-simulator/submission/runtime/edge.cpp`
- Modify: `A1-simulator/submission/tests/unit/edge_test.cpp`

**Contract:**

- `detect_edge(from, to)` 定义在**单个**四态位上（不感知向量宽度）：
  - `posedge`：从非 1 变为 1（含 `0→1`、`x→1`、`z→1`）。
  - `negedge`：从非 0 变为 0（含 `1→0`、`x→0`、`z→0`）。
  - `x↔z`、同值、`0→x`、`1→x`、`0→z`、`1→z` 均为 `None`。
- 多位信号的显式边沿敏感由 **scheduler** 只取该 signal 的 **bit0（LSB）** 调用 `detect_edge`（IEEE 1364-2005 §9.7.2 / 1800-2017 §9.4.2）；其它位变化不产生边沿事件。

- [ ] **Step 1: 写完整 edge_test**

```cpp
#include "test_support.h"
#include "runtime/edge.h"

#include <array>

int main() {
    using a1::runtime::EdgeKind;
    using a1::runtime::LogicValue;
    using a1::runtime::detect_edge;
    using Bit = LogicValue::Bit;

    A1_EXPECT(detect_edge(Bit::Zero, Bit::One) == EdgeKind::Posedge);
    A1_EXPECT(detect_edge(Bit::X, Bit::One) == EdgeKind::Posedge);
    A1_EXPECT(detect_edge(Bit::Z, Bit::One) == EdgeKind::Posedge);
    A1_EXPECT(detect_edge(Bit::One, Bit::Zero) == EdgeKind::Negedge);
    A1_EXPECT(detect_edge(Bit::X, Bit::Zero) == EdgeKind::Negedge);
    A1_EXPECT(detect_edge(Bit::Z, Bit::Zero) == EdgeKind::Negedge);

    const std::array<Bit, 4> bits{Bit::Zero, Bit::One, Bit::X, Bit::Z};
    for (Bit from : bits) {
        A1_EXPECT(detect_edge(from, from) == EdgeKind::None);
    }
    A1_EXPECT(detect_edge(Bit::Zero, Bit::X) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::One, Bit::X) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::Zero, Bit::Z) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::One, Bit::Z) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::X, Bit::Z) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::Z, Bit::X) == EdgeKind::None);
    return EXIT_SUCCESS;
}
```

（多位 LSB 边沿语义在 `scheduler_test` 钉死，见 Task 5。）

- [ ] **Step 2: 运行确认失败**

Run: `ctest --test-dir A1-simulator/submission/.build-phase2 -R '^edge_test$' --output-on-failure`

Expected: FAIL。

- [ ] **Step 3: 实现 detect_edge**

```cpp
#include "runtime/edge.h"

namespace a1::runtime {

EdgeKind detect_edge(LogicValue::Bit from, LogicValue::Bit to) {
    if (from == to) return EdgeKind::None;
    if (to == LogicValue::Bit::One && from != LogicValue::Bit::One) return EdgeKind::Posedge;
    if (to == LogicValue::Bit::Zero && from != LogicValue::Bit::Zero) return EdgeKind::Negedge;
    return EdgeKind::None;
}

}  // namespace a1::runtime
```

- [ ] **Step 4: 运行确认通过并 Commit**

```bash
cmake --build A1-simulator/submission/.build-phase2 --target edge_test
ctest --test-dir A1-simulator/submission/.build-phase2 -R '^edge_test$' --output-on-failure
git add A1-simulator/submission/runtime/edge.cpp A1-simulator/submission/tests/unit/edge_test.cpp
git commit -m "$(cat <<'EOF'
feat: add four-state posedge/negedge detection

EOF
)"
```

---

### Task 3: 扩展 ModelIR — Process / Statement

**Files:**

- Modify: `A1-simulator/submission/src/ir/model_ir.h`
- Modify: `A1-simulator/submission/src/ir/model_ir.cpp`
- Modify: `A1-simulator/submission/tests/unit/model_ir_test.cpp`

**Contract:**

新增 ID 与节点（保持现有 ContinuousAssign API 不变）：

```cpp
struct StmtId { std::uint32_t value; auto operator<=>(const StmtId&) const = default; };
struct ProcessId { std::uint32_t value; auto operator<=>(const ProcessId&) const = default; };

enum class EdgeSense { AnyChange, Posedge, Negedge };
enum class ProcessKind { Initial, Always };

struct TimingSense {
    EdgeSense edge;
    SignalId signal;  // 边沿按 IEEE 1364-2005 §9.7.2 在该信号 LSB 上检测
};

struct BlockingAssignStmt { LValueId target; ExprId value; };
struct NonBlockingAssignStmt { LValueId target; ExprId value; };
struct SeqBlockStmt { std::vector<StmtId> statements; };
struct DelayStmt { std::uint64_t ticks; StmtId next; };  // 执行完 delay 后继续 next；也可用 Delay 包住后续 block
struct IfStmt { ExprId condition; StmtId then_stmt; std::optional<StmtId> else_stmt; };
struct FinishStmt {};
struct DisplayStubStmt {};  // $display / $error no-op
struct EmptyStmt {};

struct Statement {
    std::variant<BlockingAssignStmt, NonBlockingAssignStmt, SeqBlockStmt, DelayStmt, IfStmt,
                 FinishStmt, DisplayStubStmt, EmptyStmt>
        payload;
    SourceSpan source;
};

struct Process {
    ProcessKind kind;
    std::vector<TimingSense> sensitivity;  // Initial 必须为空；Always 的空 vector = @*
    std::vector<SignalId> read_signals;    // @* 唤醒用；显式 edge 敏感时可为空
    StmtId body;
    SourceSpan source;
};
```

`ModelIR` 增加 `add_*` 工厂、`statements()` / `processes()`、校验：

- Initial 的 sensitivity 必须为空。
- Always 允许空 sensitivity（ImplicitEvent `@*`）或非空 `TimingSense` 列表（事件或：任一 sense 满足即触发）。
- Always 且 sensitivity 为空时，`read_signals` 必须由 lowering 按 IEEE 1364-2005 §9.7.5 填入（见 Task 6）；手写 IR 测试必须显式填 `read_signals`，否则 `@*` process 不会被唤醒。空 `read_signals` 合法（永不因 `@*` 唤醒），不报错。
- Statement 图无悬空 StmtId；`DelayStmt::ticks == 0` 表示 `#0`（进入 pending，见 Task 5）；`ticks > 0` 进入 Timed。
- `validate()` 继续校验连续赋值；新增过程节点校验错误合并进同一向量。

- [ ] **Step 1: 在 model_ir_test 增加过程 IR 用例（先编译失败或 validate 失败）**

追加测试：构建 `initial` 内 `a = 1; #1; a <= 0; $finish` 与 `always @(posedge clk) q <= d`，调用 `validate()` 期望空；故意构造 Always 带空 body 悬空 ID 期望非空。

- [ ] **Step 2: 实现 IR 扩展与 validate**

按上面的结构补齐 `model_ir.h/.cpp`。`add_process` / `add_statement` 变体与 ContinuousAssign 风格一致。`collect_reads` 不需要为 statement 重建 continuous read set。

- [ ] **Step 3: 跑 model_ir_test + Phase 1 回归**

```bash
cmake --build A1-simulator/submission/.build-phase2 --target model_ir_test value_test continuous_evaluator_test --parallel 4
ctest --test-dir A1-simulator/submission/.build-phase2 -R '^(model_ir_test|value_test|continuous_evaluator_test)$' --output-on-failure
```

Expected: PASS。

- [ ] **Step 4: Commit**

```bash
git add A1-simulator/submission/src/ir/model_ir.h A1-simulator/submission/src/ir/model_ir.cpp \
  A1-simulator/submission/tests/unit/model_ir_test.cpp
git commit -m "$(cat <<'EOF'
feat: extend ModelIR with processes and statements

EOF
)"
```

---

### Task 4: ContinuousEvaluator 支持环上的 delta 迭代

**Files:**

- Modify: `A1-simulator/submission/runtime/continuous_evaluator.h`
- Modify: `A1-simulator/submission/runtime/continuous_evaluator.cpp`
- Modify: `A1-simulator/submission/tests/unit/continuous_evaluator_test.cpp`

**Contract:**

保留 `settle`：无环 → 拓扑 settle；有环 → 仍返回 Phase-1 字符串 `cyclic continuous assignments unsupported by phase-1 evaluator`（保证旧测试语义）。

新增：

```cpp
// 对全部 continuous assign 按 id 升序各求值一次并 resolve 受影响 net。
// 返回是否有任何 signal 的可见值发生变化。
[[nodiscard]] static bool propagate_once(const ir::ModelIR& model, SignalStore& store);

// 若 continuous_order() 有值则等价 settle；否则循环 propagate_once 直到稳定或超过 delta_limit。
[[nodiscard]] static std::optional<std::string>
settle_with_limit(const ir::ModelIR& model, SignalStore& store, std::uint64_t delta_limit);
```

环收敛判定：`propagate_once` 前后对所有 signal 做 `exactly_equals`。超限返回 `delta cycle limit exceeded`。

- [ ] **Step 1: 写失败测试**

- 无环模型：`settle_with_limit(..., 10)` 与 `settle` 结果一致。
- 互相 `assign a = b; assign b = a;` 且外部把某一侧驱动拉开后，在 limit≥2 时收敛到一致值；`limit==0` 时若仍变化则报超限。
- 自引用 `assign a = ~a;` 在小 limit 下超限。

- [ ] **Step 2: 实现 propagate_once / settle_with_limit**

复用现有 `evaluate` 与 net resolve lambda；禁止复制 aval/bval 运算逻辑。

- [ ] **Step 3: 验证**

```bash
cmake --build A1-simulator/submission/.build-phase2 --target continuous_evaluator_test
ctest --test-dir A1-simulator/submission/.build-phase2 -R '^continuous_evaluator_test$' --output-on-failure
```

Expected: PASS（含旧用例）。

- [ ] **Step 4: Commit**

```bash
git add A1-simulator/submission/runtime/continuous_evaluator.h \
  A1-simulator/submission/runtime/continuous_evaluator.cpp \
  A1-simulator/submission/tests/unit/continuous_evaluator_test.cpp
git commit -m "$(cat <<'EOF'
feat: delta-iterate cyclic continuous assignments

EOF
)"
```

---

### Task 5: Scheduler 核心 — Active / NBA / Timed / $finish

**Files:**

- Modify: `A1-simulator/submission/runtime/scheduler.h`
- Modify: `A1-simulator/submission/runtime/scheduler.cpp`
- Modify: `A1-simulator/submission/tests/unit/scheduler_test.cpp`

**Contract:**

`run_model` 同槽算法（`finished` 未置位时）：

```text
SignalStore store(model)
prev_values[signal] = store 快照（整信号；边沿用 bit0，@* 用 exactly_equals）
将所有 Initial 按 ProcessId 升序入 Active；Always 初始为 waiting_at_sensitivity
call settle_with_limit
while true:
  // --- Active + settle + wake 循环 ---
  loop:
    while Active 非空 and not finished:
      取出 front process（该 process 必为 running）
      解释执行至 Delay / Finish / 语句结束（body 跑完）
        BlockingAssign → write_variable → 记 dirty（不立即 settle，不抢占）
        NonBlockingAssign → 推入 NBA 队列
        Delay(0) → 将 resume 点放入 #0 pending；process 态 = timed_suspended（#0）
        Delay(t>0) → 入 Timed(time+t)；process 态 = timed_suspended
        Finish → finished=true；停止取新的 Active；跳出后走 Finish 收尾
        If → truth_value(cond)：True→then；False 或 Unknown→else（若有）否则跳过
            truth_value（IEEE 1364-2005 §9.4）：
              任一位为确定 1 → True
              所有位为确定 0 → False
              其余（无确定 1 且含 x/z）→ Unknown（归入 false 侧）
        DisplayStub / Empty → 忽略
      body 正常结束：
        Initial → live=false
        Always → 回到 waiting_at_sensitivity
    if finished: break 出 Active 循环
    if 有 dirty: settle_with_limit；边沿/@* 唤醒（见下）；更新 prev；delta++；超限报错；continue
    if #0 pending 非空:
      将 pending 全部移入 Active（入队序），清空 pending；continue
    break
  if finished:
    提交已入队 NBA（若有）；返回成功（带 final_store）
  // --- NBA（仅当 Active 与 #0 pending 皆空）---
  if NBA 非空:
    按入队序提交；若变更则 settle + 唤醒；清空 NBA；若有新 Active 则回到上方 Active 循环
  else:
    // 推进时间
    若 Timed 空：若仍有 live Always 但无事件 → "no pending events"；若全死 → 成功返回
    取出最早 time；推进 simulation time；该 time 的 Timed resume 入 Active
    若 time > time_limit：报错
```

唤醒规则（只对 `waiting_at_sensitivity` 的 live process）：

- sensitivity 为空（`@*`）：`read_signals` 中任一信号 `!exactly_equals` 旧值 → 入 Active 并转 running。`read_signals` 为空则永不因此唤醒（合法）。
- 非空 `TimingSense` 列表（事件或）：任一 sense 满足即触发。`Posedge`/`Negedge`：对该 signal 的 **bit0** 做 `detect_edge`；`AnyChange`：整信号 `!exactly_equals`。
- `running` / `timed_suspended` 的 process **不**响应敏感（body 中途的边沿丢弃，与真实仿真器一致）。NBA/settle 后回到 `waiting_at_sensitivity` 的 Always 可在新一轮再次入队。

Process 运行时状态（scheduler 私有）：

```cpp
enum class ProcessRunState : std::uint8_t {
    WaitingAtSensitivity,  // 可被边沿/@* 唤醒
    Running,               // 在 Active 中或正在解释执行
    TimedSuspended,        // 等待 #0 pending 或 Timed
    Done                   // Initial 结束或 $finish 后
};

struct ProcessState {
    ir::ProcessId id;
    ir::StmtId pc;
    ProcessRunState state = ProcessRunState::WaitingAtSensitivity;
    bool live = true;
};
```

`SeqBlockStmt` 用显式栈或线性化 pc；推荐执行时维护 `std::vector<StmtId> stack`。

`$finish` 收尾：不再取出新的 Active；**提交已入队 NBA**；成功返回。不执行仍停在 Active/`#0`/Timed 中的其它 process 续体。

- [ ] **Step 1: 手写 ModelIR 的 scheduler_test（不经 frontend）**

覆盖：

1. **NBA 同槽 + `$finish`**：`BlockingAssign a=1` → `NonBlockingAssign a<=0` → `FinishStmt`。结束后 `a==0`（Finish 前提交已入队 NBA；不跑其它后续 Active process）。
2. **posedge DFF**：`initial`：`d=1; clk=0; Delay(1); clk=1; Delay(1); Finish`；`always` `{Posedge, clk}` body `q <= d`。结束后 `q==1`，`result.time == 2`。
3. **#delay 推进时间**：`initial Delay(5) → Finish`；`result.time == 5` 且 `finished == true`。
4. **delta 上限**：环 `assign a = ~a` + `initial Delay(1) → Finish`，`SimOptions{delta_limit: 4}`；`error` 含 `delta cycle limit exceeded`。
5. **多位边沿只看 LSB**：手写 2-bit `bus`；`always @(posedge bus)` 计数。`bus: 2'b00→2'b10`（仅 MSB）不触发；`2'b00→2'b01` 触发。
6. **`if` truth_value**：`if (1'bx)` 走 else；`if (4'b1x00)` 走 then；`if (2'b0x)` 走 else。
7. **`#0` 与连续赋值**：同 process 内 `a=1` 后立刻读 `wire y = ~a` 得**旧值**；`#0` 后续体读得**新值**（Active 排空→settle→再跑 `#0`）。
8. **`#0` 时 NBA 未提交**：`a=0; a<=1; #0;` 后读 `a` 仍为 `0`；再 `$finish` 后 `final_store` 中 `a==1`。

- [ ] **Step 2: 实现 scheduler.cpp**

按 Contract 实现；`write_variable` 集中变量写入并记录 dirty。LValue 复用 Phase 1 已支持形态；memory 目标本阶段不出现（frontend unsupported）。

- [ ] **Step 3: 运行 scheduler_test**

Expected: PASS。

- [ ] **Step 4: Commit**

```bash
git add A1-simulator/submission/runtime/scheduler.h \
  A1-simulator/submission/runtime/scheduler.cpp \
  A1-simulator/submission/tests/unit/scheduler_test.cpp
git commit -m "$(cat <<'EOF'
feat: implement Active/NBA/timed event scheduler

EOF
)"
```

---

### Task 6: Frontend lowering — flat-top 过程块

**Files:**

- Modify: `A1-simulator/submission/src/frontend/frontend.cpp`
- Modify: `A1-simulator/submission/tests/integration/flat_core_slice_test.cpp`（先加 lowering 断言子集）
- Modify: `A1-simulator/submission/tests/unit/` 如需辅助

**Contract:**

在遍历 top `InstanceBodySymbol` 成员时：

- `ProceduralBlockSymbol`：
  - `ProceduralBlockKind::Initial` → `ProcessKind::Initial`，sensitivity 空。
  - `ProceduralBlockKind::Always`：
    - 若 timing control 为 `ImplicitEvent` → sensitivity 空，并按 §9.7.5 填 `read_signals`（见下）。
    - 若为 `SignalEvent` / `EventList`：每个事件 lower 为 `TimingSense{Posedge|Negedge|AnyChange, signal}`（事件或）；`EdgeKind::BothEdges` / SV `edge` → unsupported。
    - 其他 timing（纯 Delay 作为 always 的顶层 control）→ unsupported。
  - `AlwaysComb` / `AlwaysFf` / `AlwaysLatch` → **一律** source-located unsupported（不按 `@*` 降级；避免与日后 SV `always_comb` 敏感规则纠缠）。
- Statement lowering：
  - `ExpressionStatement` + blocking `Assignment` → `BlockingAssignStmt`
  - non-blocking `Assignment` → `NonBlockingAssignStmt`
  - `Block` / `List` → `SeqBlockStmt`
  - `Timed` + `Delay` + 整数常量 → `DelayStmt`（`ticks==0` 为 `#0`）
  - `Conditional` → `IfStmt`
  - `Call` 到 `$finish` → `FinishStmt`；`$display`/`$error` → `DisplayStubStmt`（丢弃实参）
  - 其他 → `unsupported <kind> at file:line:col`
- `@*` / `read_signals`（IEEE 1364-2005 §9.7.5）：lowering 完成后遍历 process body 的 statement 图，收集所有 rvalue（RHS、`if` 条件；P2 无 case）以及 LValue **下标/段选表达式**中的 `SignalId`，去重；**排除**赋值目标的基信号。复用 Phase 1 `collect_reads(ExprId)`。函数体内读不收集（P2 函数 unsupported，规则记下即可）。已知差异：display stub 无实参，故仅在 display 实参中出现的读不进列表。
- 赋值 LValue 复用 Phase 1；过程赋值允许 **Variable** 目标；对 **Net** 或 **memory** 的过程赋值 → unsupported。
- 变量 declaration initializer 仍 unsupported（保持 Phase 1）。
- child `Instance` 仍 fail-closed。
- 成功时返回完整含 processes 的 ModelIR；失败不返回部分 model。

- [ ] **Step 1: 写 flat_core_slice_test 的 lowering 段**

用临时目录写入：

```verilog
module flat_core;
  reg clk;
  reg d;
  reg q;
  initial begin
    clk = 1'b0;
    d = 1'b1;
    #1 clk = 1'b1;
    #1;
    $finish;
  end
  always @(posedge clk) begin
    q <= d;
  end
endmodule
```

`compile_to_ir` 成功；`model->processes().size() == 2`；存在一个 Initial 与一个 Always。

再写一个含 `top dut(...);` 的文件，期望失败且 diagnostic 含 `unsupported child instance`。

另加 `@*` `read_signals` 断言（手写 IR 或 Verilog）：`always @(*) if (en) q = d; else q = q;` 收集含 `en,d,q`；`q = a + b` 收集 `{a,b}`；RHS `a[i]` 收集 `{a,i}`；`a[i] = d` 收集 `{i,d}` 不含基信号 `a`。

含 `always_comb` 的文件 → unsupported。

- [ ] **Step 2: 实现 frontend 过程 lowering**

从现有 `fail("unsupported procedural block")` 分支改为调用 `lower_procedural_block`。注意 Slang `AssignmentExpression` 的 `isNonBlocking()`。

- [ ] **Step 3: 构建并跑 flat_core_slice_test 中已实现的断言 + frontend_lowering_test**

Expected: 旧 continuous-only 用例仍通过；新过程用例通过；hierarchy 仍失败。

- [ ] **Step 4: Commit**

```bash
git add A1-simulator/submission/src/frontend/frontend.cpp \
  A1-simulator/submission/src/ir/model_ir.h \
  A1-simulator/submission/src/ir/model_ir.cpp \
  A1-simulator/submission/tests/integration/flat_core_slice_test.cpp
git commit -m "$(cat <<'EOF'
feat: lower flat-top initial/always into ModelIR processes

EOF
)"
```

---

### Task 7: Flat-core 垂直切片 — compile_to_ir + run_model

**Files:**

- Modify: `A1-simulator/submission/tests/integration/flat_core_slice_test.cpp`

**Fixtures（全部 flat，无 child instance）：**

1. **NBA DFF**：Task 6 模块；`run_model` 成功后用 `result.final_store` 查找 canonical_path 后缀为 `.q` 的 signal，断言最终为 `1`。

2. **Blocking vs NBA 同槽**：

```verilog
module order_slot;
  reg a;
  initial begin
    a = 1'b1;
    a <= 1'b0;
    #0;
    $finish;
  end
endmodule
```

`#0` 后 NBA 仍未提交，但 `$finish` 收尾会提交已入队 NBA，最终 `a==0`。另测无 `#0`、在 blocking/NBA 之后立即 `$finish`：同样最终 `a==0`。

3. **组合连续赋值 + #delay 采样**：

```verilog
module combo_slice;
  reg [3:0] a, b;
  wire [3:0] sum;
  reg [3:0] sample;
  assign sum = a + b;
  initial begin
    a = 4'h1; b = 4'h2;
    #1 sample = sum;
    $finish;
  end
endmodule
```

最终 `sample == 4'h3`。

4. **`#0` 后表达式 wire 得新值**：同 process 内 `a=1` 后立刻读 `assign y = ~a` 驱动的 `y` 为旧值；`#0` 后再读为新值。

5. **不收敛环**：`assign a = ~a;` + `initial #1 $finish;` 且 `delta_limit` 很小 → error。

- [ ] **Step 1: 实现上述集成断言（TDD：先 FAIL）**

- [ ] **Step 2: 修补 scheduler/frontend 直到 PASS**

- [ ] **Step 3: 全量 Phase 2 测试**

```bash
cmake --build A1-simulator/submission/.build-phase2 --target a1_phase2_tests --parallel 4
ctest --test-dir A1-simulator/submission/.build-phase2 --output-on-failure
```

Expected: 全部 PASS（含 Phase 1 + edge + scheduler + flat_core_slice）。

- [ ] **Step 4: Commit**

```bash
git add A1-simulator/submission/runtime/scheduler.h \
  A1-simulator/submission/runtime/scheduler.cpp \
  A1-simulator/submission/tests/integration/flat_core_slice_test.cpp
git commit -m "$(cat <<'EOF'
test: add flat-core vertical slice for scheduler

EOF
)"
```

---

### Task 8: Phase 2 Exit Gates

**Files:**

- Verify: Makefile、runtime、ir、frontend、ast-inventory

- [ ] **Step 1: 隔离 cwd 跑 make test**

```bash
work_dir="$(mktemp -d)"
make -f "$PWD/A1-simulator/submission/Makefile" test \
  SUBMISSION_DIR="$PWD/A1-simulator/submission" \
  BUILD_DIR="$PWD/A1-simulator/submission/.build-phase2" \
  -C "$work_dir"
```

Expected: Phase 1 全部测试 + `edge_test` + `scheduler_test` + `flat_core_slice_test` + `probe_*` 通过。

- [ ] **Step 2: Phase 0 inventory 无回归**

```bash
python3 A1-simulator/submission/tests/integration/probe_public.py \
  --probe A1-simulator/submission/.build-phase2/bin/ir_probe \
  --cases-root A1-simulator/testcases/sim_public/benchmark \
  --top tb2 \
  --output A1-simulator/submission/tests/public/ast-inventory.json
git diff --exit-code -- A1-simulator/submission/tests/public/ast-inventory.json
```

Expected: 12 case elaborate 成功，inventory 无 diff。

- [ ] **Step 3: Scope 检查**

```bash
! rg -n '#include "slang/|#include <slang/' \
  A1-simulator/submission/src/ir A1-simulator/submission/runtime
rg -n -A2 '^(compile_sim|run|parallel_run):' A1-simulator/submission/Makefile
# 三个目标仍应 exit 2 / not-implemented
make -f A1-simulator/submission/Makefile compile_sim; echo exit:$?
make -f A1-simulator/submission/Makefile run; echo exit:$?
make -f A1-simulator/submission/Makefile parallel_run; echo exit:$?
test -f docs/adr/2026-07-14-identity-assign-alias-deferred.md
git diff --check
git status --short
```

Expected：无 Slang 泄漏；评测入口仍为 Phase 0 stub；identity-alias ADR 存在；无 whitespace 问题；无未提交 Phase 2 代码。

- [ ] **Step 4: 确认公开 basic01 仍非本阶段门禁**

```bash
# compile_to_ir against staged basic01 filelist should fail-closed on child instance
```

用 integration 小测试或手动临时 filelist 调用 `compile_to_ir`，diagnostic 含 `unsupported child instance`。不要为了 basic01 偷偷 flatten hierarchy。

## Phase 2 Completion Criteria

- `detect_edge` 覆盖单 bit 0/1/X/Z 转换矩阵；多位显式边沿只看 LSB（§9.7.2）；单元/调度测试通过。
- ModelIR 可表达 Initial/Always、blocking/NBA、delay/`#0`、if、`$finish`、display stub；`validate` 覆盖悬空引用；空 `@*` `read_signals` 合法。
- `truth_value` 按 §9.4（已知非零 / 全 0 / 其余 Unknown→else 侧）；测试钉死 `1'bx`、`4'b1x00`、`2'b0x`。
- `ContinuousEvaluator::settle` 对 DAG 行为与 Phase 1 一致；`settle_with_limit` 对可收敛环收敛、对振荡环报 `delta cycle limit exceeded`；连续赋值不抢占运行中 process。
- `run_model`：Active → settle → 唤醒 → `#0` pending 回 Active →（Active 与 `#0` 皆空后）NBA；仅 `waiting_at_sensitivity` 可唤醒；`$finish` 提交已入队 NBA 后停。
- `@*` `read_signals` 按 §9.7.5；`always_comb`/`ff`/`latch` unsupported。
- flat-top fixture：posedge NBA 寄存器、同槽 blocking/NBA+`$finish`、`#0`/wire 次序、连续赋值 + `#delay` 采样。
- child instance 仍 fail-closed；`compile_sim`/`run`/`parallel_run` 未实现；identity assign alias 见 ADR，本阶段不做。
- 隔离 `make test` 与 AST inventory 回归通过；`run_model` API 冻结。

完成后，单独编写 Phase 3 实施计划：hierarchy + 单向 port binding（含 identity alias），并冲刺公开 `basic01`–`basic02`（及 M1 其余门禁）；文件系统任务与 codegen/`compile_sim`/`run` 按设计里程碑衔接，不得在本阶段提前实现。

---

## Spec Coverage Self-Check

| 设计/收尾要求 | 对应任务 |
|---|---|
| 设计 §9 Active→NBA→delta→time | Task 5（含 `#0` pending） |
| 设计 §9 边沿含四态 + IEEE LSB | Task 2–5 |
| 设计 §9 delta 上限 | Task 4–5 |
| 设计 §9 `$finish` | Task 5–6 |
| IEEE §9.4 `if` / §9.7.5 `@*` | Task 5–6 |
| 设计 §17 Phase 2：Active/NBA/时钟/延时 | Task 5–7 |
| 设计 M1 flat fixture：边沿、blocking/NBA、同槽顺序 | Task 7 |
| Phase 1 收尾：复用 ModelIR/LValue/LogicValue，不第二套连续赋值语义 | Task 4–5 |
| Phase 1 收尾：环用 delta；hierarchy 之后 | Task 4；Exit Gate Step 4；ADR identity alias |
| 设计 §2 非目标：暂不并行/增量/完整 VCD | Global Constraints |
| Grilling 决议（Finish/LSB/`#0`/状态机/API 冻结） | Global Constraints + Task 5 |
