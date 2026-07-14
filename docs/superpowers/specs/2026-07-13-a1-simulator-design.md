# A1 轻量级 RTL 仿真器设计

日期：2026-07-13
状态：已批准
目标实现语言：C++20

## 1. 背景

A1 要求实现一个轻量级、事件驱动的 Verilog RTL 仿真器。评测通过统一 Makefile 调用 `build`、`compile_sim`、`run` 和 `parallel_run`，并在隔离工作目录中使用绝对路径 filelist。功能正确性是编译性能、仿真性能和多核性能评分的前提。

公开测试包含 12 个 case：`basic01` 至 `basic05`、`alu`、`priority_encoder`、`i2c`、`ip`、`axis_fifo`、`sha256` 和 `GEMM`。公开语料不只使用题面列出的最小语法，还包含 `generate/genvar`、`localparam`、`function`、`wait`、`repeat`、`tri/inout`、双边沿触发以及多种文件系统任务。

团队开发时间紧张，因此本设计优先保证功能分，之后再处理增量编译和并行性能。

## 2. 目标与非目标

### 2.1 目标

- 通过全部 12 个公开 case 的单线程 golden 比对。
- 让 `parallel_run` 从第一版开始保持与 `run` cycle-identical；真正并行可后补。
- 使用成熟开源前端，避免自研 Verilog parser。
- 自研 Simulator IR、四态值系统、事件调度、代码生成、系统任务和并行 runtime。
- 在无网络的 Linux x86_64 评测环境中，从源码可复现构建。
- 不依赖 case 名、文件名、输入 hash 或预计算结果。

### 2.2 非目标

- 不实现完整 IEEE SystemVerilog 仿真标准。
- 不在功能回归通过之前实现通用并行事件调度。
- 不在第一版实现模块级增量链接器。
- 不在第一版实现完整 VCD 波形系统；`$dumpvars` 可以安全 no-op。
- 不直接包装 Verilator 作为最终提交的仿真引擎。

## 3. 技术决策

### 3.1 前端选择

选择 [Slang](https://github.com/MikePopoloski/slang) 作为完整前端，固定并披露具体版本。Slang 负责：

- filelist 对应的源文件加载；
- `include`、宏和其他预处理；
- lexing 和 parsing；
- 名称解析、类型检查和常量计算；
- parameter、generate 和实例层次 elaboration。

Slang 使用 MIT 许可证，原生提供 C++ library API，并明确支持作为 simulator 前端嵌入。所有源码和传递依赖必须随提交包 vendor；评测构建不得触发 CMake `FetchContent` 网络下载。

### 3.2 备选方案与取舍

- Surelog/UHDM：前端和持久化模型完整，也支持增量与多线程解析，但依赖和 UHDM/VPI 对象模型更重，赶工集成风险较高。
- Verible：适合 CST、格式化和静态工具，缺少适合本项目的完整独立预处理和 elaboration 流程。
- Yosys Verilog 前端：面向综合语义，不适合 testbench、延时和系统任务。
- Verilator 内部前端：能力强，但 AST 与优化、调度和 codegen 管线紧耦合；最终直接调用 Verilator 也会扩大原创性与合规风险。
- Slang + AST 解释器：实现功能较快，但 GEMM 含大规模连续赋值网络，解释执行难以获得合理单核性能。

最终选择 Slang 完整前端、自研小型 IR、C++ codegen 和自研 runtime。

## 4. 总体架构

```text
ProjectSpec
  filelist / TOP / include dirs / defines
             |
             v
+---------------------------+
| SlangFrontend             |
| parse + elaborate + lower |
+-------------+-------------+
              | ModelIR
              v
+---------------------------+
| Optimizer                 |
| constants / deps / levels |
+-------------+-------------+
              v
+---------------------------+
| CppCodegen                |
| model.cpp / model.hpp     |
+-------------+-------------+
              |
              v
           sim.out
              |
              v
+---------------------------+
| Runtime                   |
| Value / Scheduler / I/O   |
+---------------------------+
```

关键边界：Slang 类型、指针和头文件不能越过 `frontend/`。`SlangFrontend` 是一个深模块，对外只返回稳定、自研、可序列化的 `ModelIR` 和诊断信息。

## 5. 建议目录

```text
A1-simulator/submission/
|-- Makefile
|-- CMakeLists.txt
|-- THIRD_PARTY.md
|-- third_party/
|   `-- slang/
|-- src/
|   |-- driver/          # CLI、filelist、缓存和编译器调用
|   |-- frontend/        # Slang adapter 和 lowering
|   |-- ir/              # 自研 ModelIR
|   |-- optimizer/       # 常量、依赖图和组合逻辑分层
|   `-- codegen/         # ModelIR -> C++
|-- runtime/
|   |-- value.*          # 四态定宽值
|   |-- scheduler.*      # Active、NBA、Timed、Delta
|   |-- process.*        # 可挂起 testbench 进程
|   |-- system_tasks.*   # 文件与仿真系统任务
|   `-- thread_pool.*    # 确定性并行
`-- tests/
    |-- unit/
    |-- syntax/
    `-- public/
```

## 6. 核心接口

```cpp
struct ProjectSpec {
    std::filesystem::path filelist;
    std::string top;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<std::string> defines;
};

struct CompileResult {
    std::optional<ModelIR> model;
    std::vector<Diagnostic> diagnostics;
};

CompileResult compile_to_ir(const ProjectSpec& spec);

CodegenResult emit_cpp(const ModelIR& model,
                       const CodegenOptions& options);

int run_simulation(GeneratedModel& model,
                   const RuntimeOptions& options);
```

单线程和并行模式必须使用同一个生成模型与同一调度入口，只通过 `RuntimeOptions::threads` 选择执行策略。

## 7. ModelIR

`ModelIR` 保存仿真所需信息，不保存 Verilog 语法细节：

- 展平、唯一化后的实例和信号 ID；
- 信号位宽、signed、four-state 和 net/variable 属性；
- memory 的元素位宽和深度；
- 常量、位选、片选、拼接、复制、算术、逻辑、比较和三目表达式；
- 连续赋值；
- initial、组合和时序 process；
- blocking 与 non-blocking 赋值；
- any-change、posedge 和 negedge trigger；
- delay、wait、repeat 和系统任务；
- 用户 function 调用及其参数、局部变量和返回值；
- 统一换算后的时间单位和时间精度；
- process 的读集合与写集合；
- 原始源码范围，用于诊断。

Lowering 阶段消化 parameter、generate、常量表达式、层次引用和实例唯一化。遇到前端能够解析但后端不支持的节点，必须报出节点类型和源码位置，不能静默跳过。

## 8. 四态值与表达式

四态值使用 `aval/bval` 双掩码表示：

| aval | bval | 值 |
|---:|---:|:---|
| 0 | 0 | 0 |
| 1 | 0 | 1 |
| 1 | 1 | X |
| 0 | 1 | Z |

位宽不超过 64 的值使用单 word 快速路径；更宽的值使用多 word 存储。所有运算必须统一处理位宽扩展、截断和 signed 规则。不得为了通过公开测试把 X/Z 全局强制转换为 0。

`wire` 需要支持多驱动解析。I2C 所需的 open-drain 行为依赖 Z 与多个 driver 的正确合并，因此四态和 net resolution 属于功能门禁项。

## 9. 事件调度

第一版实现以下调度区域：

```text
取最早 timed event
        |
        v
执行 Active 队列直到为空
        |
        v
统一提交 NBA
        |
        v
信号变化触发新的 Active 事件
        |
        v
继续 delta cycle，直到稳定
        |
        v
进入下一个仿真时间
```

要求：

- blocking 赋值立即更新，并调度敏感 process；
- NBA 先记录，Active 为空后统一提交；
- 同一轮传播中避免同一 process 重复入队；
- 正确判断 posedge/negedge，包括四态转换；
- timed queue 使用仿真时间排序；同一时间内保持确定性；
- 设置 delta-cycle 上限，组合环不收敛时带诊断退出；
- `$finish` 以正常仿真终止处理。

## 10. 代码生成策略

使用混合代码生成：

- 连续赋值、组合 always 和时序 always 生成原生 C++ 函数；
- 过程内的普通控制流直接生成 C++ `if`、`switch` 和循环；
- 包含 delay、wait 和 repeat 的 testbench 进程生成带程序计数器的可挂起状态机；
- 不采用复杂 C++ coroutine；
- 性能关键 RTL 不经过通用 AST/bytecode 解释器。

生成模型示意：

```cpp
struct Model {
    SignalStorage signals;
    MemoryStorage memories;

    void eval_comb_group_0();
    void eval_comb_group_1();
    void eval_posedge_clock();
    void process_tb_initial_0(ProcessState& state);
};
```

组合逻辑根据依赖图进行拓扑分层。第一版按确定性顺序执行；功能通过后，同一 level 内无写冲突的 group 才能交给线程池并行执行，每个 level 之间设置屏障。

## 11. 系统任务

基础功能必须支持：

- `$display`、`$finish`、`$time`、`$error`；
- `$readmemh`；
- `$fopen`、`$fgets`、`$fscanf`、`$fdisplay`、`$fclose`；
- `$clog2` 和 `$unsigned`，优先在 Slang 常量求值阶段消化；
- `$dumpvars` 的安全 no-op。

所有相对文件路径以仿真进程当前工作目录解析，确保 `tb/input.mem` 和 `tb/output.mem` 能在隔离工作目录中正确访问。文件打开失败、无效描述符或格式错误必须返回明确 runtime 错误。

源文件中的 `` `timescale`` 在 frontend 阶段统一换算为模型时间单位与精度，runtime 只处理整数时间刻度。

## 12. Makefile 与离线构建

```text
make build
  `-- 构建 simc：SlangFrontend + codegen + runtime

make compile_sim FILELIST=... TOP=...
  |-- 运行 simc
  |-- 生成 ModelIR 和模型 C++
  `-- g++ -O2 生成 SIM_EXE

make run
  `-- ./SIM_EXE --threads 1

make parallel_run THREADS=N
  `-- ./SIM_EXE --threads N
```

Makefile 必须：

- 通过 `SUBMISSION_DIR` 或自身绝对路径定位提交源码；
- 把 FILELIST 视为相对当前隔离工作目录的输入；
- 接受 filelist 中的绝对源文件路径和 `-I` 等选项；
- 在当前工作目录生成 `SIM_EXE` 和 `tb/output.mem`；
- 不硬编码本机 Verilator 或其他工具路径；
- 不执行网络访问。

`TOP` 是评测输入，不得硬编码。当前公开 testbench 的模块名实际为 `tb2`，公开 Python harness 也默认传入 `--top tb2`，尽管题目 README 的示例使用 `TOP=tb`；模拟器必须只以调用方传入值为准。

Slang 和 runtime 工具构建缓存放在提交目录的 `.build/`，以锁保护并发构建，避免每个 case 重复编译依赖。所有缓存均在评测期间从源码生成，不随提交携带预编译结果。

## 13. 增量编译

第一版只实现完整设计指纹缓存：

```text
hash(
  simulator version,
  TOP,
  defines,
  include dirs,
  filelist options,
  every source path and content
)
```

当前仓库中的公开 `GEMM/filelist_bug.txt` 引用了不存在的 `tb/tb2.v`，而正常 filelist 使用文件名 `tb/tb.v`、其中模块名为 `tb2`。这是测试 fixture 一致性问题，必须在启用增量门禁前向测试提供方确认或修复；模拟器不得按该文件名缺失做特判。

指纹命中时复用已生成模型。功能正确性通过后，再按 elaborated module specialization 拆分 IR 和 C++ translation unit，以支持单文件变化后的模块级重编译。

缓存不能以 case 名、文件名模式或测试 ID 选择预生成答案；缓存键必须只描述实际编译输入和工具版本。

## 14. 并行策略

第一版 `parallel_run` 允许使用单线程执行路径，以保证与 `run` 完全一致。真正并行仅在全部公开 case 单线程正确后实现。

首个并行目标是 GEMM 的组合依赖图：

- 按拓扑 level 划分组合 group；
- 同一 level 只并行无写冲突的 group；
- 每个 level 后设置 barrier；
- Active/NBA/time region 的全局推进仍由单一调度线程控制；
- 多线程不得改变同一时间和 delta 内的可见提交顺序。

若单核性能评分不足，多核评分会被限制或清零，因此优化顺序固定为正确性、单核性能、多核加速。

## 15. 错误处理

- 前端错误：保留并输出 Slang 的文件、行列和诊断文本。
- Lowering 错误：输出 `unsupported <node-kind> at <file:line:column>`。
- Codegen 错误：指出 IR 节点、生成文件和失败阶段。
- Runtime 错误：覆盖文件 I/O、无效句柄、越界 memory、delta 不收敛和内部一致性失败。
- Makefile 目标失败必须返回非零状态，不得留下看似有效的旧 `SIM_EXE` 或 `tb/output.mem`。

## 16. 测试策略

### 16.1 AST 语法盘点

第一项开发产物是 `ir_probe`。它遍历全部公开设计的 Slang elaborated AST，统计实际出现的：

- statement kind；
- expression kind；
- 数据类型；
- trigger；
- system task/function；
- net 和 memory 形式。

该清单决定 lowering 的实现顺序，并确保未知节点尽早暴露。

### 16.2 单元测试

```text
tests/unit/
|-- value_test          # 四态、位宽和 signed
|-- resolution_test     # 多 driver 与 Z
|-- edge_test           # posedge/negedge
`-- scheduler_test      # Active/NBA/delta/time
```

### 16.3 小型语法测试

每种支持的语法和系统任务都有一个最小 Verilog 测试。预期行为使用 Icarus 或 Verilator 作为开发期 differential oracle，但最终提交不依赖它们。

### 16.4 公开回归

每完成一个里程碑，都通过官方 `test_makefile_interface.py` 在隔离工作目录运行当前里程碑及之前的全部 case。每次修改至少检查：

1. C++ 单元测试；
2. 当前所有已通过公开 case；
3. `run` 与 `parallel_run` 输出一致；
4. Makefile 绝对路径和隔离 cwd 行为。

## 17. 实施顺序

| 阶段 | 目标 | 时间上限 |
|---|---|---:|
| 0 | vendor Slang、离线 build、Makefile、`ir_probe` | 0.5–1 天 |
| 1 | ModelIR、四态值、连续赋值 | 1 天 |
| 2 | Active/NBA、时钟、延时 | 1 天 |
| 3 | basic01–05、alu、priority_encoder | 1 天 |
| 4 | 层次、memory、文件系统任务 | 1 天 |
| 5 | ip、axis_fifo、i2c、sha256 正确性 | 1–2 天 |
| 6 | GEMM 正确性 | 1 天 |
| 7 | 缓存、单核优化和真正并行 | 剩余时间 |

功能裁剪顺序固定为：

1. 先放弃真正并行；
2. 再放弃模块级增量编译；
3. 再放弃波形输出；
4. 不裁剪四态、多驱动、NBA、文件系统任务和隔离 Makefile 接口。

## 18. 里程碑门禁

- M1（Flat Core）：`basic01–02`，再加专用 flat-top fixture 覆盖时钟边沿、blocking / NBA 和同一时间槽的提交顺序。任何含 child instance 的设计必须明确报 hierarchy unsupported，不能 lower 成断开的模型。
- M2（Minimal Hierarchy）：`basic03`，实现 elaborated child instance、参数化 generate 和单向 input / output port binding，包括 bit-select port connection。inout 仍留到 I2C 阶段。
- M3：`basic04–05`、`alu`、`priority_encoder`，覆盖参数、generate、循环和 case。
- M4：`ip`、`axis_fifo`，覆盖 memory 和握手。
- M5：`i2c`，覆盖四态、多驱动、双边沿和高阻态。
- M6：`sha256`、`GEMM` 单线程正确。
- M7：完整指纹缓存、模块级增量和性能优化。

任何里程碑未通过时，不进入依赖它的性能工作。

## 19. 合规与披露

`THIRD_PARTY.md` 至少记录：

- Slang 的固定版本、源码地址和 MIT 许可证；
- 所有传递依赖的版本和许可证；
- 复用边界：预处理、解析、类型检查、常量计算与 elaboration；
- 自研边界：ModelIR、lowering 映射、四态值、事件调度、系统任务、codegen、缓存和并行 runtime；
- 开发期间使用 Verilator/Icarus 仅作为 differential oracle，不属于提交运行时。

## 20. 完成标准

设计实现完成必须同时满足：

- 公开 12 case 的 `run` 输出全部与 golden 完全一致；
- `parallel_run` 对全部公开 case 输出一致；
- `filelist_bug` 到正常 filelist 的增量流程能产生正确结果；
- 干净、无网络环境能从源码执行 `make build`；
- 官方隔离工作目录接口脚本通过；
- 不存在 case 特判、预计算产物或未披露依赖；
- 单元测试覆盖四态值、net resolution、边沿和调度区域顺序；
- `THIRD_PARTY.md` 与许可证文件完整。

## 21. 主要风险与缓解

| 风险 | 缓解措施 |
|---|---|
| Slang semantic AST 学习成本 | 先完成 `ir_probe`，只按公开语料出现频率实现 lowering |
| Slang 离线构建较重 | vendor 依赖，构建共享缓存，加锁，禁止每 case 全量重建 |
| testbench 可挂起语义复杂 | 使用显式程序计数器状态机，不使用通用 coroutine 框架 |
| I2C 四态和多驱动错误 | 在接入 I2C 前先通过独立 resolution 与 edge 单元测试 |
| GEMM 解释执行过慢 | RTL 直接生成 C++，不进入通用解释器 |
| 并行造成不确定结果 | 全局调度串行推进，仅并行无写冲突的同层组合 group |
| 工期不足 | 严格按里程碑门禁，按既定顺序裁剪性能特性 |
