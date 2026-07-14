# A1：轻量 RTL 仿真器 - 赛题说明

## 赛题描述

设计并实现一个轻量级事件驱动 Verilog RTL 仿真器，支持 Verilog-2001 核心语法子集、增量编译和并行仿真能力。

## 1. 核心要求

### 1.1 语言解析

支持 Verilog-2001 核心语法子集：

- `module` / `endmodule` 层次化实例
- `always @(*)` / `always @(posedge clk)`
- `assign` 连续赋值
- `reg` / `wire` 类型声明
- `integer` 变量
- `if-else` / `case` / `for` 过程语句
- `parameter` 参数化
- `` `define`` / `` `include`` 预处理指令

### 1.2 仿真引擎

实现事件驱动调度器：

- 事件队列：Active -> NBA 两阶段调度
- 非阻塞赋值支持
- Delta cycle 控制

附加要求：

- 时序元件建模：带异步复位的 DFF / 寄存器
- 内置系统函数：`$display` / `$readmemh` / `$time` / `$finish` / `$dumpvars` / `$fdisplay` / `$fgets` / `$fopen`

### 1.3 增量编译

- 文件级依赖分析；单文件变更后仅重编译受影响模块

### 1.4 并行仿真

- 多线程仿真加速（利用 CPU 多核）
- 模块间事件同步

## 2. 交付物

- 仿真器源代码
- 包含标准测试接口的 Makefile

## 3. 测试接口规范

**参赛者必须提供统一的 `Makefile` 作为评测入口。评测系统仅调用约定的 make 目标。**

**原始满分：100 分**

### 3.1 必须实现的 Makefile 目标

| 目标 | 用途 |
|------|------|
| `build` | 编译仿真器源代码，生成仿真器可执行文件 |
| `compile_sim` | 读取评测 filelist，解析并编译 RTL 和 testbench，生成可运行的仿真产物 |
| `run` | 执行单线程仿真，生成 `tb/output.mem` |
| `parallel_run` | 以指定线程数执行并行仿真，生成 `tb/output.mem` |

### 3.2 Makefile 参数

| 变量 | 含义 | 默认值 |
|------|------|--------|
| `FILELIST` | Verilog 源文件列表路径 | `filelist.txt` |
| `TOP` | 仿真顶层模块名 | `tb` |
| `THREADS` | 并行仿真线程数 | `4` |
| `SIM_EXE` | `compile_sim` 生成的仿真可执行文件 | `sim.out` |

### 3.3 评测流程

**标准功能测试：**

```sh
make build
make compile_sim FILELIST=filelist.txt TOP=tb
make run
diff -u tb/output_ref.mem tb/output.mem
```

**并行仿真测试：**

```sh
make compile_sim FILELIST=filelist.txt TOP=tb
make parallel_run THREADS=4
diff -u tb/output_ref.mem tb/output.mem
```

**增量编译测试：**

```sh
make compile_sim FILELIST=filelist_bug.txt TOP=tb
make compile_sim FILELIST=filelist.txt TOP=tb
make run
diff -u tb/output_ref.mem tb/output.mem
```

### 3.4 输出要求

- `build` 必须生成仿真器可执行文件或供后续目标使用的构建产物
- 每次 `compile_sim` 必须生成或更新 `run` / `parallel_run` 可直接执行的产物
- `run` 和 `parallel_run` 必须在当前测试用例目录下生成 `tb/output.mem`

### 3.5 Golden 比对规则

仿真器执行 `tb.v`，读取 `tb/input.mem` 并写入 `tb/output.mem`。正确性由外部判定：

```sh
diff -u tb/output_ref.mem tb/output.mem
```

判定规则：

- `make build` 失败：构建失败
- 任何 `make compile_sim` 失败：编译失败
- `make run` 或 `make parallel_run` 失败：仿真失败
- 未生成 `tb/output.mem`：仿真失败
- `diff` 有差异：功能错误
- `diff` 无差异：测试用例通过

### 3.6 参考 Makefile 模板

```makefile
FILELIST ?= filelist.txt
TOP ?= tb
THREADS ?= 4
SIM_EXE ?= sim.out

.PHONY: build compile_sim run parallel_run clean

build:
	# 编译仿真器源代码，生成仿真器可执行文件。
	# 示例：g++ -O2 -o simulator src/*.cc

compile_sim:
	# 使用仿真器读取 $(FILELIST)，elaborate $(TOP)，生成 $(SIM_EXE)。
	# 示例：./simulator --compile -f $(FILELIST) --top $(TOP) -o $(SIM_EXE)

run:
	# 运行单线程仿真，生成 tb/output.mem。
	# 示例：./$(SIM_EXE)

parallel_run:
	# 以 $(THREADS) 线程运行并行仿真，生成 tb/output.mem。
	# 示例：./$(SIM_EXE) --threads $(THREADS)

clean:
	rm -f simulator $(SIM_EXE) tb/output.mem
```

## 4. 环境

- 操作系统：Linux x86_64
- 评测系统提供 filelist
- 评测期间无网络访问
