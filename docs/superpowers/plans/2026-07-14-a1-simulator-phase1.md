# A1 Simulator Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** 实现不依赖 Slang 类型的 ModelIR、定宽四态值、等强度多驱动 net resolution，以及零延迟连续赋值的 lowering 与确定性求值。

**Architecture:** src/frontend/ 是唯一接触 Slang AST 的模块，公开接口仅返回自研的 ModelIR 与诊断。runtime/value 实现 aval/bval 和显式的 two-state coercion；src/ir 保存信号、表达式、LValue 和连续赋值；runtime/continuous_evaluator 只走无环图的确定性快速路径，不能引入 Active、NBA、timed、delta 或线程调度。ModelIR 可以包含连续赋值环；Phase 2 的 scheduler 再以 delta cycle 判断它们是否收敛。

**Tech Stack:** C++20、CMake 3.28+、GNU Make、Slang 11.0（commit 8acc660a20b70de48ecec1c7471863e6f4b3ae6f）、Python 3.10+、CTest。

## Global Constraints

- 目标环境是离线 Linux x86_64；构建不得下载依赖。
- Slang 的类型、指针、头文件和 AST 遍历只能位于 A1-simulator/submission/src/frontend/。src/ir/、runtime/ 和其单元测试禁止包含 Slang 头文件。
- TOP、filelist、include path 和 defines 都是调用方输入；不得依赖 case 名、文件名模式、测试 ID 或输入 hash。
- filelist 中的绝对路径和 -I 选项必须在隔离 cwd 下可用。
- 除了显式转换到 two-state 类型、或写入 two-state signal / LValue payload，不得把 X/Z 转为 0。1–64 位使用单 word fast path，超过 64 位使用 aval/bval 双数组；每次写入均清除无效高位。
- net 按位做等强度解析：全 Z 为 Z；一致的 0/1 为该值；任意 X 或 0/1 冲突为 X。
- Phase 1 只接受一个 elaborated flat top：允许其自身的 elaborated generate scope，但其中或 top 直接包含任意 child module/interface instance 时，必须失败且不返回部分 ModelIR。P1 不实现 port binding。
- Phase 1 只支持 packed integral signal / constant、普通 resolved net、无 delay / drive-strength 的 continuous driver。连续赋值环是合法 ModelIR；P1 evaluator 遇到它返回 `cyclic continuous assignments unsupported by phase-1 evaluator`。
- `wand`、`wor`、`trireg`、`supply`、非默认 net resolution、drive strength 和 assignment delay 都必须精确报 unsupported，不能按普通 `wire` 静默处理。
- 仅 lower IntegerLiteral、NamedValue、Conversion、UnaryOp、BinaryOp、ConditionalOp、Concatenation、Replication、ElementSelect、RangeSelect、连续赋值的 Assignment 包装节点，以及 `NetSymbol::getInitializer()` 表示的 net declaration assignment。parameter / localparam 的 NamedValue 必须 lower 为已 elaboration 的 ConstantExpr；variable declaration initializer 必须报 unsupported。
- select index/range 与 replication count 必须在 Slang elaboration 后为常量。
- 支持 unary ~、!、&、|、^；binary +、-、&、|、^、==、!=、===、!==、>=、<<、>>、>>>；以及 `?:`。条件的任意非零宽 packed-integral predicate 按四态 truthiness 求值。其余节点必须报出 `unsupported <node-kind> at <file>:<line>:<column>`。
- ContinuousAssign target 是 LValueId。P1 支持 whole packed signal、elaboration-constant bit select、elaboration-constant simple range select；不支持 dynamic select、memory element、concatenation / streaming LValue、force / release。
- Slang parse、semantic 或 elaboration error 一律使 compile_to_ir 失败且不返回 model；P1 也必须拒绝任意 procedural block，不能忽略 `always_comb`、`always_ff` 或 `initial` 后返回语义不完整模型。unsupported lowering / hierarchy 错误只返回一个 source-located diagnostic。
- 不实现 compile_sim、run、parallel_run、process、memory、port binding、cache、codegen、NBA、timed event 或真正并行。GEMM/filelist_bug.txt 不得被特判。

---

## File Map

    A1-simulator/submission/
    ├── CMakeLists.txt
    ├── Makefile
    ├── runtime/
    │   ├── value.h
    │   ├── value.cpp
    │   ├── continuous_evaluator.h
    │   └── continuous_evaluator.cpp
    ├── src/
    │   ├── frontend/
    │   │   ├── frontend.h
    │   │   └── frontend.cpp
    │   └── ir/
    │       ├── model_ir.h
    │       └── model_ir.cpp
    └── tests/
        ├── unit/
        │   ├── test_support.h
        │   ├── value_test.cpp
        │   ├── net_resolution_test.cpp
        │   ├── model_ir_test.cpp
        │   └── continuous_evaluator_test.cpp
        └── integration/
            └── frontend_lowering_test.cpp

依赖方向固定为：

    a1_value <- a1_ir <- a1_continuous_evaluator
                   ^
                   |
             a1_frontend + slang::slang

---

### Task 1: 建立构建图和测试脚手架

**Files:**

- Create: A1-simulator/submission/tests/unit/test_support.h
- Create: A1-simulator/submission/tests/unit/value_test.cpp
- Create: A1-simulator/submission/tests/unit/net_resolution_test.cpp
- Create: A1-simulator/submission/tests/unit/model_ir_test.cpp
- Create: A1-simulator/submission/tests/unit/continuous_evaluator_test.cpp
- Create: A1-simulator/submission/tests/integration/frontend_lowering_test.cpp
- Modify: A1-simulator/submission/CMakeLists.txt
- Modify: A1-simulator/submission/Makefile

**Interfaces:**

- Consumes: 现有 a1_frontend、slang::slang 与 CTest。
- Produces: a1_value、a1_ir、a1_continuous_evaluator，五个新 CTest executable，以及会先构建全部测试二进制的 make test。

- [ ] **Step 1: 在 CMake 中注册库和测试**

在现有 probe target 后增加以下逻辑：a1_value 编译 runtime/value.cpp；a1_ir 编译 src/ir/model_ir.cpp 并 PUBLIC 链接 a1_value；a1_continuous_evaluator 编译 runtime/continuous_evaluator.cpp 并 PUBLIC 链接 a1_ir；把 src/frontend/frontend.cpp 加入现有 a1_frontend 并让它 PUBLIC 链接 a1_ir 与 slang::slang。

注册 value_test、net_resolution_test、model_ir_test、continuous_evaluator_test、frontend_lowering_test。每个 target 都有提交目录与 src/ 的 include path；其依赖分别是 a1_value、a1_value、a1_ir、a1_continuous_evaluator、a1_frontend + a1_continuous_evaluator。增加名为 a1_phase1_tests 的 custom target，依赖现有 probe_report_test 与这五个新测试。

- [ ] **Step 2: 更新 Makefile 的 test recipe**

把 test: build 的 recipe 改为：

~~~makefile
test: build
	$(CMAKE) --build "$(BUILD_DIR)" --target a1_phase1_tests --parallel "$(BUILD_JOBS)"
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure
~~~

- [ ] **Step 3: 添加最小测试辅助头**

创建 tests/unit/test_support.h：

~~~cpp
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace a1::test {
inline bool expect(bool condition, std::string_view expression,
                   std::string_view file, int line) {
    if (condition)
        return true;
    std::cerr << file << ':' << line << ": expectation failed: " << expression << '\n';
    return false;
}

inline std::filesystem::path make_temp_dir(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() /
                      (std::string("a1-") + std::string(name));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

inline void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) std::exit(EXIT_FAILURE);
    out << text;
    if (!out) std::exit(EXIT_FAILURE);
}
} // namespace a1::test

#define A1_EXPECT(expression) \
    do { \
        if (!::a1::test::expect((expression), #expression, __FILE__, __LINE__)) \
            return EXIT_FAILURE; \
    } while (false)
~~~

- [ ] **Step 4: 添加失败优先的空测试**

各测试先只 include 将来所属 API，并分别引用 LogicValue::from_binary、resolve_net、ModelIR、SignalStore、ProjectSpec。此时编译 value_test：

~~~bash
cmake -S A1-simulator/submission -B A1-simulator/submission/.build -DCMAKE_BUILD_TYPE=Debug
cmake --build A1-simulator/submission/.build --target value_test --parallel 4
~~~

Expected: 因 runtime/value.h 不存在而失败，不发生网络访问。

- [ ] **Step 5: Commit**

~~~bash
git add A1-simulator/submission/CMakeLists.txt A1-simulator/submission/Makefile \
  A1-simulator/submission/tests
git commit -m "test: scaffold phase one simulator tests"
~~~

---

### Task 2: 实现四态 LogicValue

**Files:**

- Create: A1-simulator/submission/runtime/value.h
- Create: A1-simulator/submission/runtime/value.cpp
- Modify: A1-simulator/submission/tests/unit/value_test.cpp

**Interfaces:**

- Consumes: C++ standard library only。
- Produces: 逻辑值、位操作、显式 StateDomain coercion、统一 truthiness、缩放、切片、拼接、复制、算术、比较、移位、reduction、三目合并与 resolve_net 声明。

- [ ] **Step 1: 写完整的行为测试**

测试必须覆盖下列精确输出：

~~~cpp
using a1::runtime::LogicValue;

A1_EXPECT(LogicValue::from_binary("10xz").to_binary() == "10xz");
A1_EXPECT(LogicValue::zeros(65).to_binary() == std::string(65, '0'));
A1_EXPECT(LogicValue::ones(65).to_binary() == std::string(65, '1'));
A1_EXPECT(LogicValue::x(65).to_binary() == std::string(65, 'x'));
A1_EXPECT(LogicValue::z(65).to_binary() == std::string(65, 'z'));
A1_EXPECT(LogicValue::from_binary("101").resize(5, false).to_binary() == "00101");
A1_EXPECT(LogicValue::from_binary("101").resize(5, true).to_binary() == "11101");
A1_EXPECT(LogicValue::from_binary("101101").slice(4, 2).to_binary() == "011");
A1_EXPECT(LogicValue::concat({LogicValue::from_binary("10"),
                              LogicValue::from_binary("xz")}).to_binary() == "10xz");
A1_EXPECT(LogicValue::from_binary("10").replicate(3).to_binary() == "101010");
A1_EXPECT((~LogicValue::from_binary("0z1x")).to_binary() == "1x0x");
A1_EXPECT((LogicValue::from_binary("z0x1") & LogicValue::from_binary("0101"))
              .to_binary() == "0001");
A1_EXPECT((LogicValue::from_binary("z0x1") | LogicValue::from_binary("0100"))
              .to_binary() == "x1x1");
A1_EXPECT(LogicValue::from_binary("1111").reduce_and().to_binary() == "1");
A1_EXPECT(LogicValue::from_binary("1010").reduce_xor().to_binary() == "0");
A1_EXPECT(LogicValue::add(LogicValue::from_binary("1111"),
                          LogicValue::from_binary("0001"), 4, false)
              .to_binary() == "0000");
A1_EXPECT(LogicValue::logical_equal(LogicValue::from_binary("1x"),
                                    LogicValue::from_binary("10")).to_binary() == "x");
A1_EXPECT(LogicValue::case_equal(LogicValue::from_binary("1x"),
                                 LogicValue::from_binary("1x")).to_binary() == "1");
A1_EXPECT(LogicValue::conditional(LogicValue::from_binary("x"),
                                  LogicValue::from_binary("1010"),
                                  LogicValue::from_binary("1001")).to_binary() == "10xx");
A1_EXPECT(LogicValue::from_binary("0000").truth_value() == TruthValue::Zero);
A1_EXPECT(LogicValue::from_binary("00xz").truth_value() == TruthValue::Unknown);
A1_EXPECT(LogicValue::from_binary("x1z0").truth_value() == TruthValue::One);
A1_EXPECT(LogicValue::from_binary("10xz").coerce(4, false, StateDomain::TwoState)
              .to_binary() == "1000");
A1_EXPECT(LogicValue::from_binary("10xz").coerce(4, false, StateDomain::FourState)
              .to_binary() == "10xz");
A1_EXPECT(LogicValue::z(4).with_slice(1, LogicValue::from_binary("10")).to_binary()
              == "z10z");
A1_EXPECT(LogicValue::greater_equal(LogicValue::from_binary("1000"),
                                    LogicValue::from_binary("0001"), true)
              .to_binary() == "0");
A1_EXPECT(LogicValue::greater_equal(LogicValue::from_binary("1000"),
                                    LogicValue::from_binary("0001"), false)
              .to_binary() == "1");
A1_EXPECT(LogicValue::from_binary("1000").shift_right(
              LogicValue::from_binary("01"), 4, true).to_binary() == "1100");
~~~

- [ ] **Step 2: 确认失败**

Run:

~~~bash
cmake --build A1-simulator/submission/.build --target value_test --parallel 4
~~~

Expected: 因 LogicValue API 缺失而失败。

- [ ] **Step 3: 定义稳定 API**

在 runtime/value.h 定义以下成员：

~~~cpp
enum class StateDomain : std::uint8_t { TwoState, FourState };
enum class TruthValue : std::uint8_t { Zero, One, Unknown };

class LogicValue {
public:
    enum class Bit : std::uint8_t { Zero, One, X, Z };

    static LogicValue zeros(std::uint32_t width);
    static LogicValue ones(std::uint32_t width);
    static LogicValue x(std::uint32_t width);
    static LogicValue z(std::uint32_t width);
    static LogicValue from_binary(std::string_view text);
    static LogicValue concat(std::initializer_list<LogicValue> values);
    static LogicValue add(const LogicValue&, const LogicValue&, std::uint32_t, bool);
    static LogicValue subtract(const LogicValue&, const LogicValue&, std::uint32_t, bool);
    static LogicValue logical_equal(const LogicValue&, const LogicValue&);
    static LogicValue case_equal(const LogicValue&, const LogicValue&);
    static LogicValue greater_equal(const LogicValue&, const LogicValue&, bool);
    static LogicValue conditional(const LogicValue&, const LogicValue&, const LogicValue&);

    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] bool is_signed() const;
    [[nodiscard]] Bit bit(std::uint32_t index) const;
    [[nodiscard]] std::string to_binary() const;
    [[nodiscard]] LogicValue resize(std::uint32_t width, bool is_signed) const;
    [[nodiscard]] LogicValue coerce(std::uint32_t width, bool is_signed,
                                    StateDomain domain) const;
    [[nodiscard]] LogicValue slice(std::uint32_t msb, std::uint32_t lsb) const;
    [[nodiscard]] LogicValue with_slice(std::uint32_t bit_offset,
                                        const LogicValue& payload) const;
    [[nodiscard]] LogicValue replicate(std::uint32_t count) const;
    [[nodiscard]] LogicValue reduce_and() const;
    [[nodiscard]] LogicValue reduce_or() const;
    [[nodiscard]] LogicValue reduce_xor() const;
    [[nodiscard]] LogicValue shift_left(const LogicValue&, std::uint32_t) const;
    [[nodiscard]] LogicValue shift_right(const LogicValue&, std::uint32_t, bool) const;
    [[nodiscard]] TruthValue truth_value() const;
    [[nodiscard]] bool exactly_equals(const LogicValue&) const;

    friend LogicValue operator~(const LogicValue&);
    friend LogicValue operator&(const LogicValue&, const LogicValue&);
    friend LogicValue operator|(const LogicValue&, const LogicValue&);
    friend LogicValue operator^(const LogicValue&, const LogicValue&);
    friend LogicValue resolve_net(std::span<const LogicValue>);

private:
    std::uint32_t width_;
    bool is_signed_;
    std::vector<std::uint64_t> aval_;
    std::vector<std::uint64_t> bval_;
};
~~~

在类外声明 LogicValue resolve_net(std::span<const LogicValue> drivers)。

- [ ] **Step 4: 实现 aval/bval 语义**

编码必须严格为 Zero=00、One=10、X=11、Z=01。bit zero 是 word zero 的 LSB，from_binary 的最右字符映射 bit zero，to_binary 从 MSB 输出。

第一版以逐 bit 循环实现全部操作，从而同时覆盖单 word 与多 word。已知控制值规则是 0 & unknown = 0、1 | unknown = 1；其余含 X/Z 的 bitwise 结果为 X；~X 与 ~Z 为 X。算术和顺序比较的任一操作数含 X/Z 时返回目标宽度的 X。logical equality 在无法确定时返回 X，case equality 按四态精确比较。truth_value 对任意非零宽值执行四态布尔化：存在已知 1 为 One；全为已知 0 为 Zero；否则为 Unknown。`!` 与 `?:` 必须复用它。未知条件的 ?: 对相同结果 bit 保留该 bit，对不同结果 bit 产出 X。

resize 截断高位，只有源与目标都为 signed 时做 sign extend。coerce 先 resize，目标为 TwoState 时把每个 X/Z 变成 0 并清空所有 bval；目标为 FourState 时保留 X/Z。`with_slice` 只覆盖指定物理 LSB offset 范围并保留其余 bit，越界抛出 `std::invalid_argument`。未知 shift count 产生 X；过宽 shift 对逻辑移位产生 0，对算术右移产生 sign fill。每个改写操作都调用清除无效高位的内部函数。

- [ ] **Step 5: 验证并提交**

Run:

~~~bash
cmake --build A1-simulator/submission/.build --target value_test --parallel 4
ctest --test-dir A1-simulator/submission/.build -R '^value_test$' --output-on-failure
git add A1-simulator/submission/runtime/value.h \
  A1-simulator/submission/runtime/value.cpp \
  A1-simulator/submission/tests/unit/value_test.cpp
git commit -m "feat: add four state logic values"
~~~

Expected: value_test 通过，包含 65 位路径。

---

### Task 3: 实现多驱动 Net Resolution

**Files:**

- Modify: A1-simulator/submission/runtime/value.cpp
- Modify: A1-simulator/submission/tests/unit/net_resolution_test.cpp

**Interfaces:**

- Consumes: 等宽 LogicValue driver span。
- Produces: 每一 bit 已解析的 LogicValue。

- [ ] **Step 1: 写 resolution truth-table 测试**

~~~cpp
using a1::runtime::LogicValue;

A1_EXPECT(a1::runtime::resolve_net({LogicValue::from_binary("z")}).to_binary() == "z");
A1_EXPECT(a1::runtime::resolve_net({LogicValue::from_binary("0"),
                                    LogicValue::from_binary("z")}).to_binary() == "0");
A1_EXPECT(a1::runtime::resolve_net({LogicValue::from_binary("1"),
                                    LogicValue::from_binary("1")}).to_binary() == "1");
A1_EXPECT(a1::runtime::resolve_net({LogicValue::from_binary("0"),
                                    LogicValue::from_binary("1")}).to_binary() == "x");
A1_EXPECT(a1::runtime::resolve_net({LogicValue::from_binary("x"),
                                    LogicValue::from_binary("1")}).to_binary() == "x");
A1_EXPECT(a1::runtime::resolve_net({LogicValue::from_binary("z0x1"),
                                    LogicValue::from_binary("z1z1"),
                                    LogicValue::from_binary("zzzz")}).to_binary() == "zxx1");
~~~

- [ ] **Step 2: 确认失败后实现**

每个 bit 统计 saw_zero、saw_one、saw_x，忽略 Z：saw_x 或 saw_zero && saw_one 为 X；否则 One、Zero 或初始 Z。宽度不一致抛出 std::invalid_argument；空 span 的直接调用结果定义为一位 Z，evaluator 不会对声明 net 使用空 span。

- [ ] **Step 3: 验证并提交**

Run:

~~~bash
cmake --build A1-simulator/submission/.build \
  --target value_test net_resolution_test --parallel 4
ctest --test-dir A1-simulator/submission/.build \
  -R '^(value_test|net_resolution_test)$' --output-on-failure
git add A1-simulator/submission/runtime/value.cpp \
  A1-simulator/submission/tests/unit/net_resolution_test.cpp
git commit -m "feat: resolve four state net drivers"
~~~

Expected: 两个测试均通过。

---

### Task 4: 定义 ModelIR 与确定性拓扑序

**Files:**

- Create: A1-simulator/submission/src/ir/model_ir.h
- Create: A1-simulator/submission/src/ir/model_ir.cpp
- Modify: A1-simulator/submission/tests/unit/model_ir_test.cpp

**Interfaces:**

- Consumes: LogicValue 与标准库。
- Produces: typed ID、PackedType、SourceSpan、signal/expression/continuous-assign record、validation、read set 与拓扑排序。

- [ ] **Step 1: 写 IR 测试**

构建 top.a variable、top.mid net、top.y net；加入 mid=~a 和 y=mid 两条连续赋值。验证 validate 为空、continuous_order 为 [0, 1]。再构建 x=z、z=x，验证 validate 为空而 continuous_order 没有值。

再构建交错多 driver 图：`y=a`、`z=y`、`y=b`。断言三个 ContinuousAssign 独立存在、两个 y target 指向同一个 SignalId，且在连续序列中两个 producer 都早于 `z=y` consumer；不得断言具体 ID 顺序。加入 `y[3:2]=a` 与 `y[1:0]=b` 的 RangeSelectLValue，验证 LValue 宽度、物理 bit offset、越界和 RHS/LHS reverse declaration range 都由 validation 覆盖。

- [ ] **Step 2: 确认测试因缺少 ir/model_ir.h 失败**

Run:

~~~bash
cmake --build A1-simulator/submission/.build --target model_ir_test --parallel 4
~~~

- [ ] **Step 3: 定义数据模型**

定义：

~~~cpp
struct SignalId { std::uint32_t value; };
struct ExprId { std::uint32_t value; };
struct LValueId { std::uint32_t value; };
struct ContinuousAssignId { std::uint32_t value; };

struct SourceSpan {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct PackedType {
    std::uint32_t width;
    bool is_signed;
    bool is_four_state;
};

enum class SignalKind { Variable, Net };
enum class UnaryOp { BitwiseNot, LogicalNot, ReduceAnd, ReduceOr, ReduceXor };
enum class BinaryOp {
    Add, Subtract, BitwiseAnd, BitwiseOr, BitwiseXor,
    LogicalEqual, LogicalNotEqual, CaseEqual, CaseNotEqual,
    GreaterEqual, ShiftLeft, LogicalShiftRight, ArithmeticShiftRight
};

struct WholeSignalLValue { SignalId signal; };
struct BitSelectLValue { SignalId signal; std::uint32_t bit_offset; };
struct RangeSelectLValue {
    SignalId signal;
    std::uint32_t bit_offset;
    std::uint32_t width;
};
struct LValue {
    std::variant<WholeSignalLValue, BitSelectLValue, RangeSelectLValue> payload;
    PackedType type;
    SourceSpan source;
};
~~~

Expression payload 使用 std::variant，包含 ConstantExpr(LogicValue)、SignalRefExpr、UnaryExpr、BinaryExpr、ConditionalExpr、ConcatExpr、ReplicateExpr、BitSelectExpr、RangeSelectExpr、CastExpr。BinaryExpr 还持有 `PackedType operation_type`：对算术、bitwise、equality 和 comparison，它是 Slang elaboration 确定的共同操作数类型；对 shift，它是左操作数的 semantic type，右操作数保留自身 type 作为 shift count。它与一位 comparison result 的 Expression::type 分开。Expression 持有 PackedType 与 SourceSpan。Signal 持有 canonical hierarchical path、PackedType、SignalKind 与 SourceSpan。LValue 只描述覆盖的 base signal 和物理 LSB offset；运行时不再需要 SV 的 declared range direction。ContinuousAssign 持有 `LValueId target`、value、source、去重后的 read signal IDs。

ModelIR 公开 `add_whole_signal_lvalue`、`add_bit_select_lvalue`、`add_range_select_lvalue`，以及 add_signal、add_constant、add_signal_ref、add_unary、add_binary、add_conditional、add_concat、add_replicate、add_bit_select、add_range_select、add_cast、add_continuous_assign、accessor、validate 和 continuous_order。

- [ ] **Step 4: 实现 validation 与依赖图**

所有 add 函数添加一个节点并返回零基 typed ID。collect_reads 递归访问所有 expression payload，排序并去重。

validate 的顺序固定为：zero width、duplicate signal path、out-of-range expression child、out-of-range signal reference、out-of-range LValue base / select、out-of-range assignment target/value、LValue base is not a net、target/value width mismatch。LValue 的 `bit_offset + width` 必须落在 base signal 内；bit/range select type 的 width 必须分别为 1 / range width，four-state domain 继承 base signal。连续赋值环不是 validation error。

continuous_order 对 consumer 读取的每个 signal，向**所有**以该 signal 为 target base 的 continuous assignment 建立 producer -> consumer 边；边去重，不能合并多个 producer。使用 Kahn 算法与最小 ID ready set，若未输出所有 assignment 则返回 std::nullopt。它只是 DAG 的快速路径 / 分析结果，绝不是 ModelIR 合法性或 frontend 成功的门禁。

- [ ] **Step 5: 验证与提交**

Run:

~~~bash
cmake --build A1-simulator/submission/.build --target model_ir_test --parallel 4
ctest --test-dir A1-simulator/submission/.build -R '^model_ir_test$' --output-on-failure
! rg -n '#include "slang/|#include <slang/' \
  A1-simulator/submission/src/ir A1-simulator/submission/runtime
git add A1-simulator/submission/src/ir \
  A1-simulator/submission/tests/unit/model_ir_test.cpp
git commit -m "feat: define simulator model ir"
~~~

Expected: 测试通过，boundary scan 无输出。

---

### Task 5: 从 ModelIR 求值连续赋值

**Files:**

- Create: A1-simulator/submission/runtime/continuous_evaluator.h
- Create: A1-simulator/submission/runtime/continuous_evaluator.cpp
- Modify: A1-simulator/submission/tests/unit/continuous_evaluator_test.cpp

**Interfaces:**

- Consumes: 已验证 ModelIR、类型默认初值、一个稳定的 optional external net driver slot。
- Produces: SignalStore 与 ContinuousEvaluator::settle；该 API 无 scheduler 状态，且只求值 DAG。

- [ ] **Step 1: 写 chained assignment 测试**

手写 8-bit IR：a、b 为 variable，sum、y 为 net，赋值 sum=a+b、y=sum。将 a 设为 11111111、b 设为 00000001，settle 后 sum 与 y 都为 00000000。再把 a 设为 0000x000，settle 后 y 必须为 xxxxxxxx。

加入以下独立测试：

- four-state variable 初值为 X、two-state variable 初值为 0、four-state net 初值为 Z、two-state net 初值为 0；向 two-state variable 写入 X/Z 后读回 0，且 two-state expression 的 bval 始终为零。
- `y[3:2]=2'b10` 与 `y[1:0]=2'b01` 的两个 slot materialize 为 `10zz` 和 `zz01`，resolve 后为 `1001`。two-state payload `xz` 写到两位 LValue 时先变 `00`，materialize 后仍为 `00zz`，不能变成 `0000`。
- `y=a; z=y; y=b` 的四组 end-to-end 值为 `(a,b,y,z)=(0,Z,0,0),(Z,1,1,1),(Z,Z,Z,Z),(0,1,X,X)`；由此证明两个 assignment slot 没有 last-write-wins。
- `set_external_driver` 连续更新同一个 stable slot；写入全 Z 清除该 slot 并释放 net。它和 assignment driver 一起 resolve，不得覆盖 signal 当前值。
- cycle 的 ModelIR 可以构造成功，但 settle 精确返回 `cyclic continuous assignments unsupported by phase-1 evaluator`。
- BinaryExpr 的一位 `>=` result 不得缩窄语义操作数：`4'sb1000 >= 4'sb0001` 为 0，而同一 bits 按 unsigned 为 1；`4'sb1000 >>> 1` 为 `1100`，并覆盖 result-type truncation。

- [ ] **Step 2: 定义 API**

~~~cpp
class SignalStore {
public:
    explicit SignalStore(const ir::ModelIR& model);
    void set_variable(ir::SignalId signal, LogicValue value);
    void set_external_driver(ir::SignalId signal, LogicValue value);
    [[nodiscard]] const LogicValue& value(ir::SignalId signal) const;

private:
    friend class ContinuousEvaluator;
    std::vector<LogicValue> values_;
    std::vector<std::optional<LogicValue>> external_drivers_;
    struct AssignmentDriverSlot {
        ir::LValueId target;
        LogicValue payload;
    };
    std::vector<AssignmentDriverSlot> assignment_drivers_;
};

class ContinuousEvaluator {
public:
    [[nodiscard]] static std::optional<std::string> settle(const ir::ModelIR& model,
                                                            SignalStore& store);

private:
    [[nodiscard]] static LogicValue evaluate(const ir::ModelIR& model,
                                             const SignalStore& store,
                                             ir::ExprId expression);
};
~~~

- [ ] **Step 3: 实现**

SignalStore 按 `PackedType::is_four_state` 初始化：four-state variable 为 X，two-state variable 为 0，four-state net 为 Z，two-state net 为 0。每个 ContinuousAssign 必须有独立的 `AssignmentDriverSlot { target: LValueId, payload }`；payload 宽度等于 LValue type 宽度。set_variable 只接受 Variable，按 signal type `coerce(width, signedness, StateDomain)`。set_external_driver 只接受 Net，维护该 signal 唯一稳定 external slot：全 Z 表示 release；其他值按 base signal type coercion 后替换 slot。

settle 先检查 validate，错误返回 `invalid model: <first validation error>`；随后取 continuous_order，无序时返回 `cyclic continuous assignments unsupported by phase-1 evaluator`。按拓扑序求值：RHS 每次返回都按 Expression::type coercion；BinaryExpr 对算术、bitwise、equality 和 comparison 先把两操作数按 `operation_type` coercion；shift 只把左操作数按 operation_type coercion，右操作数保留自身 type 作为 count。之后按 operation_type 的 signedness 运算，最后按 Expression::type coercion。CastExpr 按目标 Expression::type coercion。assignment 先把 RHS coercion 为 LValue type payload，再用 `LogicValue::z(base_width).with_slice(bit_offset, payload)` materialize 成**始终 four-state**的 full-width driver image；未覆盖位必须保持 Z，绝不可对该 image 做 two-state coercion。将所有 assignment image 和可选 external slot resolve 后，才按 base Signal::type coercion 并提交值。任何变体都不能被跳过。

- [ ] **Step 4: 验证与提交**

Run:

~~~bash
cmake --build A1-simulator/submission/.build \
  --target continuous_evaluator_test model_ir_test value_test net_resolution_test --parallel 4
ctest --test-dir A1-simulator/submission/.build \
  -R '^(continuous_evaluator_test|model_ir_test|value_test|net_resolution_test)$' \
  --output-on-failure
git add A1-simulator/submission/runtime/continuous_evaluator.h \
  A1-simulator/submission/runtime/continuous_evaluator.cpp \
  A1-simulator/submission/tests/unit/continuous_evaluator_test.cpp
git commit -m "feat: evaluate continuous assignment ir"
~~~

Expected: 四个测试都通过。

---

### Task 6: 从 Slang Elaborated AST Lower 到 ModelIR

**Files:**

- Create: A1-simulator/submission/src/frontend/frontend.h
- Create: A1-simulator/submission/src/frontend/frontend.cpp
- Modify: A1-simulator/submission/tests/integration/frontend_lowering_test.cpp

**Interfaces:**

- Consumes: ProjectSpec 的 filelist、top、include dirs、defines 和 elaborated Slang AST。
- Produces: 成功时 self-owned、语义完整的 flat-top ModelIR；Slang 前端失败时无 model 且保留其 error diagnostics，lowering / hierarchy failure 时无 model 且只有一个 source-located diagnostic。

- [ ] **Step 1: 写隔离 filelist integration test**

在临时目录写入绝对 filelist 和下列 top.sv：

~~~systemverilog
module top(input wire [15:0] a, input wire [15:0] b, input wire sel,
           output wire [31:0] y, output wire cmp, output wire red);
  wire [15:0] sumv, diffv;
  wire low_all_ones;
  assign sumv = a + b;
  assign diffv = a - b;
  assign cmp = ({1'b0, a} + {1'b0, b}) >= 17'h08000;
  assign red = ^{a[3:0], b[3:0]};
  assign low_all_ones = &a[7:0];
  assign y = sel ? {sumv, diffv} : {a[15:8], b[7:0], 7'b0, a[0], 7'b0, low_all_ones};
endmodule
~~~

调用 compile_to_ir，断言有 model、无 diagnostic、6 个 continuous assigns、validate 通过。按 canonical path 找到 a、b、sel、y、cmp、red，以 `set_external_driver` 设置 a=16'h00ff、b=16'h0001、sel=1，settle 后断言 y=32'h010000fe、cmp=0、red=1。

同一测试文件还必须包含下列独立 fixture；每个失败 fixture 断言 `model == nullopt`，每个 lowering failure 断言 diagnostic 恰为一个：

- `input var logic v`、`input logic n`、`output wire y` 的 ANSI-port fixture：port 的 `internalSymbol` 各只产生一个 SignalId；`v` 为 Variable，`n` 和 `y` 为 Net；三者 canonical path 正确，continuous assign 可引用它们。
- `assign y=a; assign z=y; assign y=b;` fixture：保留三个 distinct ContinuousAssign，两个 y producer 都在 order 中先于 z consumer；按 `(a,b)=(0,Z),(Z,1),(Z,Z),(0,1)` end-to-end 验证 `(y,z)=(0,0),(1,1),(Z,Z),(X,X)`。
- `assign x=z; assign z=x;` fixture：有 model、无 frontend diagnostic、`continuous_order()==nullopt`，但 evaluator 返回 P1 cycle error。
- `wire [3:0] mid = a; localparam logic [3:0] MASK=4'b0101; assign y=mid & MASK;` fixture：net initializer 是一个独立 whole-LValue continuous driver，parameter ref 是 ConstantExpr。另以 `[0:3]` declaration 的 bit/range RHS 与 LHS select 验证 source index 到 LSB physical offset 的转换。
- two-state fixture：`input bit [3:0] a` 和 `(bit'(four_state_expr))` cast 的值不保留 X/Z；four-state parallel signal 保留 `10xz`。
- 多 bit predicate fixture：`x1z0 ? 4'ha : 4'h5` 选择 true arm，`00xz ? 4'ha : 4'h5` 按 unknown branch merge。
- `assign y = a * b;` fixture：无 model、一个 diagnostic，message 为 `unsupported BinaryOp at <file>:<line>:<column>`，column 为源中 `*` 的真实 1-based byte column。
- child fixture `child u_child(.a(a), .y(y));`：无 model、一个指向 `u_child` 的 diagnostic，message 为 `unsupported child instance top.u_child: port binding not implemented`；返回模型中不得有 `top.u_child.*`。相对地，只有 generate scope 的 `for` loop `assign y[i]=~a[i]` 必须成功。
- `logic x = 1'b0;` 与 `always_comb y=a;` fixture：分别以 variable initializer / procedural block unsupported 失败，不能静默遗漏 driver。

- [ ] **Step 2: 定义 Slang-free contract**

创建 src/frontend/frontend.h：

~~~cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ir/model_ir.h"

namespace a1::frontend {
struct ProjectSpec {
    std::filesystem::path filelist;
    std::string top;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<std::string> defines;
};

struct Diagnostic {
    std::string message;
    ir::SourceSpan source;
};

struct CompileResult {
    std::optional<ir::ModelIR> model;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] CompileResult compile_to_ir(const ProjectSpec& spec);
} // namespace a1::frontend
~~~

- [ ] **Step 3: 实现 frontend.cpp**

复用 probe_main.cpp 的 Driver 生命周期：addStandardArgs、parseCommandLine、processOptions、parseAllSources、createCompilation、reportCompilation、reportDiagnostics。argv 从 a1-simc、-f、filelist、--top、top、每个 -I 和每个 -D 构建，并用 vector<const char*> 调用 parseCommandLine，以保留路径空格边界。若 Slang parse、semantic 或 elaboration 有任何 error，复制其文件、行、列和文本为 Diagnostic，返回 `model == nullopt`，不进入 lowering。

要求 `spec.top` 在 `compilation->getRoot().topInstances` 中对应恰好一个 elaborated InstanceBodySymbol；缺失或多于一个都返回一个 source-located frontend diagnostic 和无 model。递归遍历该 body 的普通 scope member 与 GenerateBlockSymbol scope，但遇到任意 child module/interface InstanceSymbol（包括 generate 内）立刻返回 `unsupported child instance <hierarchical-path>: port binding not implemented`；不得遍历 child body 或构造部分 model。遇到 ProceduralBlockSymbol 也立刻失败。未实例化的 module definition 不影响结果。

先遍历普通 ANSI PortSymbol：若 `internalSymbol` 是 integral packed VariableSymbol 或 NetSymbol，则以该 **internalSymbol** lower 唯一 Signal；若是 explicit expression、null port 或其他不能直接指向 packed integral ValueSymbol 的形态，返回 unsupported diagnostic。随后递归遍历普通 scope member，lower integral packed VariableSymbol / NetSymbol，并用 `const slang::ast::ValueSymbol*` 到 SignalId 的映射去重；不得用 path 字符串或 PortSymbol 创建第二个 signal。SignalKind 由 internal ValueSymbol 的实际种类确定，不能从 port direction 或 `logic` 拼写推断。拒绝 unpacked、nonintegral、zero-width value、variable declaration initializer、特殊 net 类型与 non-default resolution。

将 SV declared range 转换为 runtime 物理 LSB offset：对 bit/range select 从 elaborated ConstantRange 取得每个 source index 对应的 base bit，再构造 `BitSelectLValue` / `RangeSelectLValue` 或 RHS select Expr。runtime/ir 不保存 source `[msb:lsb]` direction。LHS 可以是 whole named net、constant bit select 或 constant simple range select；它们都创建 LValueId，并执行 base-is-net、width 和 bounds validation。对 ContinuousAssignSymbol 与 NetSymbol::getInitializer() 统一创建独立 driver；拒绝 delay 和 drive strength，不能把 declaration assignment 和普通 assign 合并或覆盖。

RHS 使用下表 lower：

| Slang AST | ModelIR | 规则 |
|---|---|---|
| IntegerLiteral | ConstantExpr | 逐 bit从 SVInt 复制到 LogicValue，再按 elaborated PackedType 的 StateDomain coercion。 |
| NamedValueExpression | SignalRefExpr / ConstantExpr | ValueSymbol 查 identity map；ParameterSymbol / localparam 用 Slang 已 elaboration constant value 创建 ConstantExpr。 |
| ConversionExpression | CastExpr | 保存 elaborated width、signedness 和 StateDomain。 |
| UnaryExpression | UnaryExpr | 只映射 Phase 1 unary operator。 |
| BinaryExpression | BinaryExpr | 只映射 Phase 1 binary operator；对非 shift，从 Slang 已插入转换后的 left/right 取得共同 operation_type；对 shift 保存 left 的 semantic type，不能由 result type 推断。 |
| ConditionalExpression | ConditionalExpr | 只接受无 pattern 的任意非零宽 packed-integral condition，保留其 self-determined type。 |
| ConcatenationExpression | ConcatExpr | 保留 operand 顺序。 |
| ReplicationExpression | ReplicateExpr | 只接受 unsigned constant count。 |
| ElementSelectExpression | BitSelectExpr | selector 必须为 in-range elaboration constant，并转为 physical LSB offset。 |
| RangeSelectExpression | RangeSelectExpr | 只接受 simple in-range elaboration-constant range，并转为 physical LSB offset 与 width。 |

Expression lower 后都保存 elaborated `PackedType { width, is_signed, is_four_state }`；不得忽略 `is_four_state`。SourceSpan 用 SourceManager::getFullyExpandedLoc、getFileName、getLineNumber 和 `getColumnNumber` 填充，column 为真实 1-based byte column。任何不支持的形态返回 `unsupported <node-kind> at <file>:<line>:<column>`，不能返回部分 model。最后只执行 validate；validation failure 转成 `invalid ModelIR: <message>`。不得调用 continuous_order 作为 frontend gate，因包含环的 ModelIR 仍然是成功 lowering 的结果。

- [ ] **Step 4: 验证与提交**

Run:

~~~bash
cmake --build A1-simulator/submission/.build \
  --target frontend_lowering_test continuous_evaluator_test model_ir_test \
  value_test net_resolution_test --parallel 4
ctest --test-dir A1-simulator/submission/.build \
  -R '^(frontend_lowering_test|continuous_evaluator_test|model_ir_test|value_test|net_resolution_test)$' \
  --output-on-failure
! rg -n '#include "slang/|#include <slang/' \
  A1-simulator/submission/src/ir A1-simulator/submission/runtime
git add A1-simulator/submission/src/frontend/frontend.h \
  A1-simulator/submission/src/frontend/frontend.cpp \
  A1-simulator/submission/tests/integration/frontend_lowering_test.cpp
git commit -m "feat: lower continuous assignments to model ir"
~~~

Expected: 五个测试通过，只有 frontend 可以包含 Slang。

---

### Task 7: 验证 Phase 1 Exit Gates

**Files:**

- Verify: A1-simulator/submission/Makefile
- Verify: A1-simulator/submission/runtime/
- Verify: A1-simulator/submission/src/ir/
- Verify: A1-simulator/submission/src/frontend/
- Verify: A1-simulator/submission/tests/public/ast-inventory.json

- [ ] **Step 1: 在隔离 cwd 跑完整测试**

Run:

~~~bash
work_dir="$(mktemp -d)"
make -f "$PWD/A1-simulator/submission/Makefile" test \
  SUBMISSION_DIR="$PWD/A1-simulator/submission" \
  BUILD_DIR="$PWD/A1-simulator/submission/.build-phase1" \
  -C "$work_dir"
~~~

Expected: probe_report_test、probe_smoke、value_test、net_resolution_test、model_ir_test、continuous_evaluator_test、frontend_lowering_test 均通过。

- [ ] **Step 2: 重新生成 Phase 0 inventory**

Run:

~~~bash
python3 A1-simulator/submission/tests/integration/probe_public.py \
  --probe A1-simulator/submission/.build-phase1/bin/ir_probe \
  --cases-root A1-simulator/testcases/sim_public/benchmark \
  --top tb2 \
  --output A1-simulator/submission/tests/public/ast-inventory.json
git diff --exit-code -- A1-simulator/submission/tests/public/ast-inventory.json
~~~

Expected: 12 个 case 均 elaborate 成功，inventory 无 diff。不要把完整公开语料设为 compile_to_ir 成功门禁，因为其 process/timing 已明确超出 Phase 1。

- [ ] **Step 3: 检查 scope 和工作区**

Run:

~~~bash
! rg -n '#include "slang/|#include <slang/' \
  A1-simulator/submission/src/ir A1-simulator/submission/runtime
rg -n -A2 '^(compile_sim|run|parallel_run):' A1-simulator/submission/Makefile
git diff --check
git status --short
~~~

Expected: 前端边界扫描无匹配；三个评测目标仍输出现有 Phase 0 not-implemented error 并返回 2；无 whitespace error、无未提交 Phase 1 改动。

## Phase 1 Completion Criteria

- LogicValue 在小于、等于和大于 64 位时都保存 0/1/X/Z；StateDomain conversion 只在显式 two-state seam 把 X/Z 转为 0，two-state result 的 bval 为零。
- value、X/Z propagation、signed resize、truthiness、select、concat、replication、comparison、shift、unknown conditional 与 multi-bit condition 的显式测试通过。
- net resolution 的 Z-only、known agreement、conflict、X contamination，以及 partial-driver `Z` envelope 测试通过。
- ModelIR 自验证 LValue 的 width/bounds/net target，并对无环连续赋值给出确定性顺序；连续赋值环仍为有效 IR，只有 P1 evaluator 明确拒绝执行。
- 每个 continuous assignment 在 lowering 和 evaluator 内均保留独立 driver slot；同一 net 的所有 producer 都先于 consumer，multiple / partial driver 端到端完成 net resolution。
- evaluator 沿 DAG 快速路径传播逻辑值，使用 stable external driver slot，未引入 scheduler 状态。
- compile_to_ir 通过绝对 filelist fixture 生成可求值的 flat-top ModelIR；ANSI port lower 到唯一 internalSymbol signal、net declaration initializer 和 elaborated parameter constant 正确处理。
- child instance、procedural block、variable initializer、special net、delay / strength 和不支持表达式都精确 fail-closed，不返回部分 model；diagnostic 带真实文件、行和 1-based 列。
- 隔离目录中的 make test 与 Phase 0 AST inventory 回归通过。
- compile_sim、run、parallel_run 仍未实现。

完成后，为 Active/NBA/timed scheduler 和首个 flat-top vertical slice 单独编写 Phase 2 实施计划；必须复用本阶段 ModelIR、LValue 和 LogicValue，不能引入第二套连续赋值语义。Phase 2 的 scheduler 对 cyclic continuous dependency 执行 delta cycle，只有超过上限才报告不收敛；紧随其后的 hierarchy milestone 才实现 child elaboration 与单向 input/output port binding。
