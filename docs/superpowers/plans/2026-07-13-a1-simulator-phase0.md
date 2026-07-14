# A1 Simulator Phase 0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a self-contained, offline-capable Slang frontend probe that accepts evaluator-style filelists and inventories the elaborated AST of all 12 public cases.

**Architecture:** All new implementation lives under `A1-simulator/submission/`; Slang remains a pinned third-party frontend and no Slang type crosses the future `src/frontend/` boundary. Phase 0 produces an `ir_probe` executable, a stable JSON report, and evaluator-style integration tests, but does not implement ModelIR, scheduling, code generation, simulation, caching, or parallel execution.

**Tech Stack:** C++20, CMake 3.28+, GNU Make, Slang 11.0 at commit `8acc660a20b70de48ecec1c7471863e6f4b3ae6f`, Python 3.10+, CTest.

## Global Constraints

- Target environment is offline Linux x86_64; evaluation builds must never access the network.
- Build all source and transitive dependencies from files committed under `A1-simulator/submission/`.
- Accept evaluator-style filelists containing absolute source paths and `-I` options while running from an isolated working directory.
- Treat `TOP` as an input. Do not hardcode `tb`, `tb2`, a case name, filename pattern, or input hash.
- All current public testbenches declare `module tb2`; the public Python harness defaults to `--top tb2`, even though the prose README shows `TOP=tb`.
- `$fscanf` is a required runtime system function for later phases; it is used by every public testbench and must be added to the design requirements before runtime implementation.
- The checked-in `GEMM/filelist_bug.txt` currently names missing `tb/tb2.v`. Treat this as a fixture defect to resolve with the test owner; never compensate for it in simulator code.
- `parallel_run` will eventually share the same model and scheduler as `run`; Phase 0 must not introduce a second frontend path.
- Preserve the user's existing `.gitignore` modification and perform implementation in an isolated worktree created with `superpowers:using-git-worktrees`.
- Do not begin four-state values, ModelIR, scheduling, code generation, caching, or parallel execution until every Phase 0 exit gate passes.

---

## Scope and File Map

This design spans several independently reviewable milestones. Implement only Phase 0 with this plan. Write separate plans later for:

1. four-state values, net resolution, and ModelIR primitives;
2. the `basic01` end-to-end vertical slice;
3. Active/NBA/timed scheduling and `basic02`–`basic03`;
4. hierarchy, memories, file tasks, and `basic04`–`axis_fifo`;
5. I2C, SHA-256, GEMM correctness;
6. fingerprint caching, single-core optimization, and deterministic parallelism.

Phase 0 creates or modifies these files:

```text
docs/superpowers/specs/2026-07-13-a1-simulator-design.md
A1-simulator/submission/Makefile
A1-simulator/submission/CMakeLists.txt
A1-simulator/submission/THIRD_PARTY.md
A1-simulator/submission/third_party/slang/...
A1-simulator/submission/third_party/fmt/...
A1-simulator/submission/third_party/boost-regex/...
A1-simulator/submission/third_party/tomlplusplus/...
A1-simulator/submission/src/frontend/probe_main.cpp
A1-simulator/submission/src/frontend/probe_report.h
A1-simulator/submission/src/frontend/probe_report.cpp
A1-simulator/submission/src/frontend/ast_probe.h
A1-simulator/submission/src/frontend/ast_probe.cpp
A1-simulator/submission/tests/unit/probe_report_test.cpp
A1-simulator/submission/tests/integration/probe_smoke.py
A1-simulator/submission/tests/integration/probe_public.py
A1-simulator/submission/tests/public/ast-inventory.json
```

Responsibilities:

- `Makefile`: evaluator-facing location-independent entry points.
- `CMakeLists.txt`: offline dependency wiring and Phase 0 targets.
- `THIRD_PARTY.md`: exact dependency versions, licenses, and reuse boundary.
- `probe_report.*`: Slang-independent stable report model and JSON serialization.
- `ast_probe.*`: the only code that traverses Slang's elaborated AST.
- `probe_main.cpp`: CLI lifecycle, diagnostics, and file output.
- `probe_smoke.py`: a minimal compile/elaboration/CLI contract test.
- `probe_public.py`: stages evaluator-style filelists and inventories all public cases.
- `ast-inventory.json`: checked-in baseline used to order later lowering work.

---

### Task 1: Record the Evaluator Contract Errata

**Files:**
- Modify: `docs/superpowers/specs/2026-07-13-a1-simulator-design.md:242`
- Modify: `docs/superpowers/specs/2026-07-13-a1-simulator-design.md:275`
- Modify: `docs/superpowers/specs/2026-07-13-a1-simulator-design.md:301`

**Interfaces:**
- Consumes: public testbench and harness facts already committed under `A1-simulator/testcases/`.
- Produces: corrected requirements used by every later implementation plan.

- [ ] **Step 1: Prove `$fscanf` is required by the public corpus**

Run from the repository root:

```bash
grep -R '\$fscanf' A1-simulator/testcases/sim_public/benchmark/*/tb/*.v
```

Expected: matches in all 12 public case testbenches, including `basic01/tb/tb.v`.

- [ ] **Step 2: Add `$fscanf` and the TOP rule to the design**

Replace the system-task list in section 11 with this exact list:

```markdown
- `$display`、`$finish`、`$time`、`$error`；
- `$readmemh`；
- `$fopen`、`$fgets`、`$fscanf`、`$fdisplay`、`$fclose`；
- `$clog2` 和 `$unsigned`，优先在 Slang 常量求值阶段消化；
- `$dumpvars` 的安全 no-op。
```

Add this paragraph immediately after the Makefile requirements in section 12:

```markdown
`TOP` 是评测输入，不得硬编码。当前公开 testbench 的模块名实际为 `tb2`，公开 Python harness 也默认传入 `--top tb2`，尽管题目 README 的示例使用 `TOP=tb`；模拟器必须只以调用方传入值为准。
```

- [ ] **Step 3: Record the malformed incremental fixture without changing simulator semantics**

Add this paragraph after the fingerprint description in section 13:

```markdown
当前仓库中的公开 `GEMM/filelist_bug.txt` 引用了不存在的 `tb/tb2.v`，而正常 filelist 使用文件名 `tb/tb.v`、其中模块名为 `tb2`。这是测试 fixture 一致性问题，必须在启用增量门禁前向测试提供方确认或修复；模拟器不得按该文件名缺失做特判。
```

- [ ] **Step 4: Verify the requirement text**

Run:

```bash
grep -n '\$fscanf\|不得硬编码\|fixture 一致性问题' docs/superpowers/specs/2026-07-13-a1-simulator-design.md
git diff --check
```

Expected: three requirement areas are printed and `git diff --check` exits 0.

- [ ] **Step 5: Commit the contract correction**

```bash
git add docs/superpowers/specs/2026-07-13-a1-simulator-design.md
git commit -m "docs: clarify simulator evaluation contract"
```

---

### Task 2: Vendor Slang and Its Offline Build Dependencies

**Files:**
- Create: `A1-simulator/submission/third_party/slang/`
- Create: `A1-simulator/submission/third_party/fmt/`
- Create: `A1-simulator/submission/third_party/boost-regex/`
- Create: `A1-simulator/submission/third_party/tomlplusplus/`
- Create: `A1-simulator/submission/CMakeLists.txt`
- Create: `A1-simulator/submission/Makefile`
- Create: `A1-simulator/submission/THIRD_PARTY.md`
- Create: `A1-simulator/submission/src/frontend/probe_main.cpp`

**Interfaces:**
- Consumes: Slang library target `slang::slang`; vendored dependency source trees.
- Produces: `make build`, `.build/bin/ir_probe`, and a build graph that cannot download dependencies.

- [ ] **Step 1: Export the pinned Slang source subset**

Run from the repository root in the isolated implementation worktree:

```bash
mkdir -p A1-simulator/submission/third_party/slang
git archive --format=tar 8acc660a20b70de48ecec1c7471863e6f4b3ae6f \
  CMakeLists.txt cmake external include source scripts LICENSE LICENSES REUSE.toml .git_archival.txt \
  | tar -xf - -C A1-simulator/submission/third_party/slang
```

Expected: `third_party/slang/include/slang/ast/Compilation.h` and `third_party/slang/source/CMakeLists.txt` exist.

- [ ] **Step 2: Fetch source-only dependency archives into committed directories**

These commands require network access only during development. Evaluation never runs them.

```bash
mkdir -p A1-simulator/submission/third_party/fmt
git fetch --depth 1 https://github.com/fmtlib/fmt.git refs/tags/12.2.0
git archive --format=tar FETCH_HEAD | tar -xf - -C A1-simulator/submission/third_party/fmt

mkdir -p A1-simulator/submission/third_party/boost-regex
git fetch --depth 1 https://github.com/MikePopoloski/regex.git refs/tags/boost-1.91.0
git archive --format=tar FETCH_HEAD | tar -xf - -C A1-simulator/submission/third_party/boost-regex

mkdir -p A1-simulator/submission/third_party/tomlplusplus
git fetch --depth 1 https://github.com/marzer/tomlplusplus.git refs/tags/v3.4.0
git archive --format=tar FETCH_HEAD | tar -xf - -C A1-simulator/submission/third_party/tomlplusplus
```

Expected: each directory contains its license file and build metadata. Do not vendor mimalloc or Catch2: Phase 0 disables both.

- [ ] **Step 3: Write the initial probe executable**

Create `A1-simulator/submission/src/frontend/probe_main.cpp`:

```cpp
#include <cstdio>

#include "slang/util/VersionInfo.h"

int main() {
    std::printf("a1 ir_probe using slang %s\n",
                slang::VersionInfo::getVersionString().c_str());
    return 0;
}
```

- [ ] **Step 4: Write the offline CMake graph**

Create `A1-simulator/submission/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(a1_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_FMT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_BOOST_REGEX
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost-regex" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus" CACHE PATH "" FORCE)

set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYTHON_DOCS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_THREADTEST OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_UVM_TEST OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/slang third_party/slang-build EXCLUDE_FROM_ALL)

add_executable(ir_probe src/frontend/probe_main.cpp)
target_link_libraries(ir_probe PRIVATE slang::slang)

enable_testing()
```

- [ ] **Step 5: Write the evaluator-facing Makefile**

Create `A1-simulator/submission/Makefile` with tabs before recipe lines:

```makefile
FILELIST ?= filelist.txt
TOP ?= tb
THREADS ?= 4
SIM_EXE ?= sim.out

SUBMISSION_DIR ?= $(abspath $(dir $(firstword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(SUBMISSION_DIR)/.build
BUILD_JOBS ?= 4
CMAKE ?= cmake

IR_PROBE := $(BUILD_DIR)/bin/ir_probe

.PHONY: build compile_sim run parallel_run clean test
.DELETE_ON_ERROR:

build:
	$(CMAKE) -S "$(SUBMISSION_DIR)" -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build "$(BUILD_DIR)" --target ir_probe --parallel "$(BUILD_JOBS)"
	@test -x "$(IR_PROBE)"

compile_sim: build
	@echo "ERROR: compile_sim is not implemented in Phase 0" >&2
	@exit 2

run:
	@echo "ERROR: run is not implemented in Phase 0" >&2
	@exit 2

parallel_run:
	@echo "ERROR: parallel_run is not implemented in Phase 0" >&2
	@exit 2

test: build
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure

clean:
	$(CMAKE) -E remove_directory "$(BUILD_DIR)"
```

- [ ] **Step 6: Disclose third-party code and reuse boundaries**

Create `A1-simulator/submission/THIRD_PARTY.md`:

```markdown
# Third-Party Software

## Slang

- Project: https://github.com/MikePopoloski/slang
- Version: 11.0 development snapshot
- Commit: `8acc660a20b70de48ecec1c7471863e6f4b3ae6f`
- License: MIT; see `third_party/slang/LICENSE` and `third_party/slang/LICENSES/`.
- Reused for: preprocessing, parsing, name and type resolution, constant evaluation,
  diagnostics, and elaboration.
- Not reused for: ModelIR, four-state runtime values, net resolution, event scheduling,
  system-task runtime behavior, C++ model generation, compile caching, or parallel simulation.

## fmt

- Project: https://github.com/fmtlib/fmt
- Version: 12.2.0
- License: MIT; see `third_party/fmt/LICENSE`.
- Used transitively by Slang.

## Boost.Regex standalone fork

- Project: https://github.com/MikePopoloski/regex
- Version: boost-1.91.0
- License: Boost Software License 1.0; see `third_party/boost-regex/LICENSE_1_0.txt`.
- Used transitively by Slang.

## tomlplusplus

- Project: https://github.com/marzer/tomlplusplus
- Version: 3.4.0
- License: MIT; see `third_party/tomlplusplus/LICENSE`.
- Used transitively by Slang.

Slang's optional mimalloc, Catch2, tools, examples, documentation, installation,
and Python-binding targets are disabled and are not part of the submitted build.
```

- [ ] **Step 7: Build the pinned frontend**

Run:

```bash
make -f A1-simulator/submission/Makefile build \
  SUBMISSION_DIR="$PWD/A1-simulator/submission"
A1-simulator/submission/.build/bin/ir_probe
```

Expected: configuration performs no download, build succeeds, and the executable prints an `a1 ir_probe using slang 11.0...` line.

- [ ] **Step 8: Commit the offline frontend skeleton**

```bash
git add A1-simulator/submission
git commit -m "build: vendor offline slang frontend"
```

---

### Task 3: Add the Stable Probe Report Model

**Files:**
- Create: `A1-simulator/submission/src/frontend/probe_report.h`
- Create: `A1-simulator/submission/src/frontend/probe_report.cpp`
- Create: `A1-simulator/submission/tests/unit/probe_report_test.cpp`
- Modify: `A1-simulator/submission/CMakeLists.txt`

**Interfaces:**
- Consumes: only the C++ standard library; this component must not include Slang headers.
- Produces: `a1::frontend::ProbeReport`, `ProbeReport::increment`, and `ProbeReport::to_json()`.

- [ ] **Step 1: Write the failing report serialization test**

Create `A1-simulator/submission/tests/unit/probe_report_test.cpp`:

```cpp
#include <cstdlib>
#include <iostream>
#include <string>

#include "frontend/probe_report.h"

int main() {
    a1::frontend::ProbeReport report;
    report.top_instances = {"tb2"};
    report.increment(report.symbols, "Instance");
    report.increment(report.symbols, "Instance");
    report.increment(report.statements, "Timed");
    report.increment(report.expressions, "Call");
    report.increment(report.timing_controls, "Delay");
    report.increment(report.data_types, "PackedArrayType");
    report.increment(report.system_calls, "$fscanf");

    const std::string expected =
        "{\n"
        "  \"top_instances\": [\"tb2\"],\n"
        "  \"symbols\": {\"Instance\": 2},\n"
        "  \"statements\": {\"Timed\": 1},\n"
        "  \"expressions\": {\"Call\": 1},\n"
        "  \"timing_controls\": {\"Delay\": 1},\n"
        "  \"data_types\": {\"PackedArrayType\": 1},\n"
        "  \"system_calls\": {\"$fscanf\": 1}\n"
        "}\n";

    if (report.to_json() != expected) {
        std::cerr << "unexpected JSON:\n" << report.to_json();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

- [ ] **Step 2: Register the failing unit-test target**

Replace the target section after `add_subdirectory(...)` in `A1-simulator/submission/CMakeLists.txt` with:

```cmake
add_library(a1_probe_report STATIC src/frontend/probe_report.cpp)
target_include_directories(a1_probe_report PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(ir_probe src/frontend/probe_main.cpp)
target_link_libraries(ir_probe PRIVATE slang::slang a1_probe_report)

enable_testing()
add_executable(probe_report_test tests/unit/probe_report_test.cpp)
target_link_libraries(probe_report_test PRIVATE a1_probe_report)
add_test(NAME probe_report_test COMMAND probe_report_test)
```

- [ ] **Step 3: Run the test and verify it fails**

Run:

```bash
cmake -S A1-simulator/submission -B A1-simulator/submission/.build -DCMAKE_BUILD_TYPE=Debug
cmake --build A1-simulator/submission/.build --target probe_report_test --parallel 4
```

Expected: compilation fails because `frontend/probe_report.h` does not exist.

- [ ] **Step 4: Implement the report interface**

Create `A1-simulator/submission/src/frontend/probe_report.h`:

```cpp
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace a1::frontend {

struct ProbeReport {
    using Counts = std::map<std::string, std::uint64_t, std::less<>>;

    std::vector<std::string> top_instances;
    Counts symbols;
    Counts statements;
    Counts expressions;
    Counts timing_controls;
    Counts data_types;
    Counts system_calls;

    void increment(Counts& counts, std::string_view name);
    [[nodiscard]] std::string to_json() const;
};

} // namespace a1::frontend
```

Create `A1-simulator/submission/src/frontend/probe_report.cpp`:

```cpp
#include "frontend/probe_report.h"

#include <sstream>

namespace a1::frontend {
namespace {

std::string escape_json(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += ch; break;
        }
    }
    return result;
}

void append_counts(std::ostringstream& out, const ProbeReport::Counts& counts) {
    out << '{';
    bool first = true;
    for (const auto& [name, count] : counts) {
        if (!first)
            out << ", ";
        first = false;
        out << '"' << escape_json(name) << "\": " << count;
    }
    out << '}';
}

} // namespace

void ProbeReport::increment(Counts& counts, std::string_view name) {
    ++counts[std::string(name)];
}

std::string ProbeReport::to_json() const {
    std::ostringstream out;
    out << "{\n  \"top_instances\": [";
    for (std::size_t index = 0; index < top_instances.size(); ++index) {
        if (index != 0)
            out << ", ";
        out << '"' << escape_json(top_instances[index]) << '"';
    }
    out << "],\n  \"symbols\": ";
    append_counts(out, symbols);
    out << ",\n  \"statements\": ";
    append_counts(out, statements);
    out << ",\n  \"expressions\": ";
    append_counts(out, expressions);
    out << ",\n  \"timing_controls\": ";
    append_counts(out, timing_controls);
    out << ",\n  \"data_types\": ";
    append_counts(out, data_types);
    out << ",\n  \"system_calls\": ";
    append_counts(out, system_calls);
    out << "\n}\n";
    return out.str();
}

} // namespace a1::frontend
```

- [ ] **Step 5: Run the report test**

Run:

```bash
cmake --build A1-simulator/submission/.build --target probe_report_test --parallel 4
ctest --test-dir A1-simulator/submission/.build -R probe_report_test --output-on-failure
```

Expected: `probe_report_test` passes.

- [ ] **Step 6: Commit the stable report model**

```bash
git add A1-simulator/submission/CMakeLists.txt \
  A1-simulator/submission/src/frontend/probe_report.h \
  A1-simulator/submission/src/frontend/probe_report.cpp \
  A1-simulator/submission/tests/unit/probe_report_test.cpp
git commit -m "test: define stable ast probe report"
```

---

### Task 4: Traverse the Elaborated Slang AST

**Files:**
- Create: `A1-simulator/submission/src/frontend/ast_probe.h`
- Create: `A1-simulator/submission/src/frontend/ast_probe.cpp`
- Modify: `A1-simulator/submission/src/frontend/probe_main.cpp`
- Create: `A1-simulator/submission/tests/integration/probe_smoke.py`
- Modify: `A1-simulator/submission/CMakeLists.txt`

**Interfaces:**
- Consumes: `const slang::ast::Compilation&` and `ProbeReport`.
- Produces: `a1::frontend::collect_probe(const slang::ast::Compilation&)` and CLI `ir_probe -f FILELIST --top TOP --probe-output FILE`.

- [ ] **Step 1: Write the failing CLI smoke test**

Create `A1-simulator/submission/tests/integration/probe_smoke.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    probe = args.probe.resolve()

    with tempfile.TemporaryDirectory(prefix="a1-probe-smoke-") as raw:
        work = Path(raw)
        source = work / "tiny.v"
        source.write_text(
            "module tb2;\n"
            "  reg a;\n"
            "  wire y;\n"
            "  assign y = ~a;\n"
            "  initial begin a = 0; #1; $display(\"%b\", y); $finish; end\n"
            "endmodule\n",
            encoding="utf-8",
        )
        filelist = work / "filelist.txt"
        filelist.write_text(str(source.resolve()) + "\n", encoding="utf-8")
        output = work / "report.json"

        subprocess.run(
            [
                str(probe),
                "-f",
                str(filelist),
                "--top",
                "tb2",
                "--probe-output",
                str(output),
            ],
            cwd=work,
            check=True,
        )
        report = json.loads(output.read_text(encoding="utf-8"))
        assert report["top_instances"] == ["tb2"]
        assert report["symbols"]["ContinuousAssign"] == 1
        assert report["statements"]["Timed"] == 1
        assert report["timing_controls"]["Delay"] == 1
        assert report["system_calls"]["$display"] == 1
        assert report["system_calls"]["$finish"] == 1

        bad = subprocess.run(
            [
                str(probe),
                "-f",
                str(filelist),
                "--top",
                "missing_top",
                "--probe-output",
                str(work / "bad.json"),
            ],
            cwd=work,
            check=False,
        )
        assert bad.returncode != 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Register the smoke test and verify it fails**

Add these lines to `A1-simulator/submission/CMakeLists.txt` after the report test:

```cmake
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
add_test(
  NAME probe_smoke
  COMMAND "${Python_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/probe_smoke.py"
          --probe "$<TARGET_FILE:ir_probe>")
```

Run:

```bash
cmake --build A1-simulator/submission/.build --target ir_probe --parallel 4
ctest --test-dir A1-simulator/submission/.build -R probe_smoke --output-on-failure
```

Expected: failure because the version-only executable rejects `-f` and `--probe-output`.

- [ ] **Step 3: Declare the frontend boundary**

Create `A1-simulator/submission/src/frontend/ast_probe.h`:

```cpp
#pragma once

#include "frontend/probe_report.h"

namespace slang::ast {
class Compilation;
}

namespace a1::frontend {

[[nodiscard]] ProbeReport collect_probe(const slang::ast::Compilation& compilation);

} // namespace a1::frontend
```

- [ ] **Step 4: Implement elaborated AST collection**

Create `A1-simulator/submission/src/frontend/ast_probe.cpp`:

```cpp
#include "frontend/ast_probe.h"

#include <concepts>
#include <type_traits>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/CallExpression.h"

namespace a1::frontend {
namespace {

class ProbeVisitor final
    : public slang::ast::ASTVisitor<ProbeVisitor, slang::ast::VisitFlags::AllGood> {
public:
    explicit ProbeVisitor(ProbeReport& report) : report(report) {}

    template<typename T>
        requires std::derived_from<T, slang::ast::Symbol>
    void handle(const T& node) {
        report.increment(report.symbols, slang::ast::toString(node.kind));
        this->visitDefault(node);
    }

    template<typename T>
        requires std::derived_from<T, slang::ast::Statement>
    void handle(const T& node) {
        report.increment(report.statements, slang::ast::toString(node.kind));
        this->visitDefault(node);
    }

    template<typename T>
        requires std::derived_from<T, slang::ast::Expression>
    void handle(const T& node) {
        report.increment(report.expressions, slang::ast::toString(node.kind));
        report.increment(report.data_types, slang::ast::toString(node.type->kind));
        if constexpr (std::same_as<T, slang::ast::CallExpression>) {
            if (node.isSystemCall())
                report.increment(report.system_calls, node.getSubroutineName());
        }
        this->visitDefault(node);
    }

    template<typename T>
        requires std::derived_from<T, slang::ast::TimingControl>
    void handle(const T& node) {
        report.increment(report.timing_controls, slang::ast::toString(node.kind));
        this->visitDefault(node);
    }

private:
    ProbeReport& report;
};

} // namespace

ProbeReport collect_probe(const slang::ast::Compilation& compilation) {
    ProbeReport report;
    ProbeVisitor visitor(report);
    for (const auto* top : compilation.getRoot().topInstances) {
        report.top_instances.emplace_back(top->getHierarchicalPath());
        top->visit(visitor);
    }
    return report;
}

} // namespace a1::frontend
```

- [ ] **Step 5: Replace the version-only CLI with the real probe lifecycle**

Replace `A1-simulator/submission/src/frontend/probe_main.cpp` with:

```cpp
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

#include "frontend/ast_probe.h"
#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"

int main(int argc, char** argv) {
    slang::driver::Driver driver;
    driver.addStandardArgs();

    std::optional<bool> show_help;
    std::optional<std::string> output_path;
    driver.cmdLine.add("-h,--help", show_help, "Display help and exit");
    driver.cmdLine.add("--probe-output", output_path,
                       "Write the elaborated AST inventory as JSON", "<file>");

    if (!driver.parseCommandLine(argc, argv))
        return 1;
    if (show_help == true) {
        std::puts(driver.cmdLine.getHelpText("A1 elaborated AST probe").c_str());
        return 0;
    }
    if (!output_path) {
        driver.printError("--probe-output is required");
        return 1;
    }
    if (!driver.processOptions())
        return 2;
    if (!driver.parseAllSources()) {
        static_cast<void>(driver.reportDiagnostics(false));
        return 3;
    }

    auto compilation = driver.createCompilation();
    driver.reportCompilation(*compilation, false);
    if (!driver.reportDiagnostics(false))
        return 3;

    const auto report = a1::frontend::collect_probe(*compilation);
    std::ofstream output(*output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        driver.printError("cannot open probe output: " + *output_path);
        return 4;
    }
    output << report.to_json();
    if (!output) {
        driver.printError("cannot write probe output: " + *output_path);
        return 4;
    }
    return 0;
}
```

- [ ] **Step 6: Link the AST collector**

Replace the `a1_probe_report` target declaration with:

```cmake
add_library(a1_frontend STATIC
  src/frontend/probe_report.cpp
  src/frontend/ast_probe.cpp)
target_include_directories(a1_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(a1_frontend PUBLIC slang::slang)
```

Replace both references to `a1_probe_report` with `a1_frontend`.

- [ ] **Step 7: Build and run both Phase 0 tests**

Run:

```bash
cmake -S A1-simulator/submission -B A1-simulator/submission/.build -DCMAKE_BUILD_TYPE=Debug
cmake --build A1-simulator/submission/.build --target ir_probe probe_report_test --parallel 4
ctest --test-dir A1-simulator/submission/.build --output-on-failure
```

Expected: `probe_report_test` and `probe_smoke` both pass; the missing-top invocation emits a Slang diagnostic and returns nonzero.

- [ ] **Step 8: Commit the elaborated AST probe**

```bash
git add A1-simulator/submission/CMakeLists.txt \
  A1-simulator/submission/src/frontend \
  A1-simulator/submission/tests/integration/probe_smoke.py
git commit -m "feat: inventory elaborated slang ast"
```

---

### Task 5: Inventory All Public Cases with Evaluator-Style Filelists

**Files:**
- Create: `A1-simulator/submission/tests/integration/probe_public.py`
- Create: `A1-simulator/submission/tests/public/ast-inventory.json`

**Interfaces:**
- Consumes: `ir_probe`, the public benchmark directory, absolute source paths, `-I` options, and `TOP=tb2`.
- Produces: one checked-in deterministic aggregate inventory and a repeatable public-corpus gate.

- [ ] **Step 1: Write the public inventory runner**

Create `A1-simulator/submission/tests/integration/probe_public.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import Counter
from pathlib import Path


EXPECTED_CASES = {
    "alu",
    "axis_fifo",
    "basic01",
    "basic02",
    "basic03",
    "basic04",
    "basic05",
    "GEMM",
    "i2c",
    "ip",
    "priority_encoder",
    "sha256",
}
CATEGORIES = (
    "symbols",
    "statements",
    "expressions",
    "timing_controls",
    "data_types",
    "system_calls",
)


def stage_filelist(case_dir: Path, work_dir: Path) -> Path:
    source = case_dir / "filelist.txt"
    output = work_dir / "filelist.txt"
    lines = [f"-I{(case_dir / 'rtl').resolve()}", ""]
    for raw in source.read_text(encoding="utf-8").splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith(("#", "//", "+", "-")):
            lines.append(raw)
        elif Path(stripped).is_absolute():
            lines.append(stripped)
        else:
            lines.append(str((source.parent / stripped).resolve()))
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--cases-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--top", default="tb2")
    args = parser.parse_args()

    probe = args.probe.resolve()
    cases_root = args.cases_root.resolve()
    output = args.output.resolve()
    case_dirs = {path.name: path for path in cases_root.iterdir() if path.is_dir()}
    if set(case_dirs) != EXPECTED_CASES:
        missing = sorted(EXPECTED_CASES - set(case_dirs))
        extra = sorted(set(case_dirs) - EXPECTED_CASES)
        raise SystemExit(f"unexpected public cases: missing={missing} extra={extra}")

    reports: dict[str, object] = {}
    aggregate = {category: Counter() for category in CATEGORIES}
    with tempfile.TemporaryDirectory(prefix="a1-public-probe-") as raw_temp:
        temp_root = Path(raw_temp)
        for case_name in sorted(case_dirs):
            work_dir = temp_root / case_name
            work_dir.mkdir(parents=True)
            filelist = stage_filelist(case_dirs[case_name], work_dir)
            report_path = work_dir / "report.json"
            subprocess.run(
                [
                    str(probe),
                    "-f",
                    str(filelist),
                    "--top",
                    args.top,
                    "--probe-output",
                    str(report_path),
                ],
                cwd=work_dir,
                check=True,
            )
            report = json.loads(report_path.read_text(encoding="utf-8"))
            if report["top_instances"] != [args.top]:
                raise SystemExit(
                    f"{case_name}: top instances are {report['top_instances']}, "
                    f"expected [{args.top!r}]"
                )
            reports[case_name] = report
            for category in CATEGORIES:
                aggregate[category].update(report[category])

    if "$fscanf" not in reports["basic01"]["system_calls"]:
        raise SystemExit("basic01 inventory did not contain $fscanf")

    document = {
        "top": args.top,
        "cases": reports,
        "aggregate": {
            category: dict(sorted(counts.items()))
            for category, counts in aggregate.items()
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Generate the public AST baseline**

Run from the repository root:

```bash
python3 A1-simulator/submission/tests/integration/probe_public.py \
  --probe A1-simulator/submission/.build/bin/ir_probe \
  --cases-root A1-simulator/testcases/sim_public/benchmark \
  --top tb2 \
  --output A1-simulator/submission/tests/public/ast-inventory.json
```

Expected: all 12 cases elaborate successfully and the command writes `ast-inventory.json`.

- [ ] **Step 3: Prove the inventory is deterministic**

Run the same command again, then:

```bash
git diff --exit-code -- A1-simulator/submission/tests/public/ast-inventory.json
```

Expected: exit 0 with no diff after the baseline has been staged once.

- [ ] **Step 4: Inspect feature gates required by later phases**

Run:

```bash
python3 - <<'PY'
import json
from pathlib import Path

path = Path("A1-simulator/submission/tests/public/ast-inventory.json")
data = json.loads(path.read_text(encoding="utf-8"))
for category in (
    "statements",
    "expressions",
    "timing_controls",
    "data_types",
    "system_calls",
):
    print(f"[{category}]")
    for name, count in data["aggregate"][category].items():
        print(f"{name}: {count}")
PY
```

Expected: named, counted node kinds are printed. Use this list—not assumptions from the prose problem statement—to order the next lowering plan.

- [ ] **Step 5: Commit the corpus gate and baseline**

```bash
git add A1-simulator/submission/tests/integration/probe_public.py \
  A1-simulator/submission/tests/public/ast-inventory.json
git commit -m "test: inventory public simulator corpus"
```

---

### Task 6: Prove the Phase 0 Exit Gates

**Files:**
- Verify: `A1-simulator/submission/Makefile`
- Verify: `A1-simulator/submission/.build-offline-phase0/`
- Verify: `A1-simulator/submission/tests/public/ast-inventory.json`

**Interfaces:**
- Consumes: a Linux host with CMake 3.28+, C++20 compiler, GNU Make, Python 3.10+, and `unshare`.
- Produces: evidence that the checked-in submission builds without network and works from an isolated cwd.

- [ ] **Step 1: Verify all vendored inputs are tracked**

Run:

```bash
git ls-files --error-unmatch \
  A1-simulator/submission/third_party/slang/CMakeLists.txt \
  A1-simulator/submission/third_party/fmt/CMakeLists.txt \
  A1-simulator/submission/third_party/boost-regex/include/boost/regex.hpp \
  A1-simulator/submission/third_party/tomlplusplus/CMakeLists.txt
```

Expected: all four paths are printed and the command exits 0.

- [ ] **Step 2: Build in a fresh network namespace**

Run only if `A1-simulator/submission/.build-offline-phase0` does not exist:

```bash
test ! -e A1-simulator/submission/.build-offline-phase0
unshare --user --map-root-user --net \
  make -f "$PWD/A1-simulator/submission/Makefile" build \
  SUBMISSION_DIR="$PWD/A1-simulator/submission" \
  BUILD_DIR="$PWD/A1-simulator/submission/.build-offline-phase0"
```

Expected: configure and build succeed with networking unavailable. Any attempted FetchContent download fails this gate.

- [ ] **Step 3: Run tests from an isolated working directory**

Run:

```bash
work_dir="$(mktemp -d)"
make -f "$PWD/A1-simulator/submission/Makefile" test \
  SUBMISSION_DIR="$PWD/A1-simulator/submission" \
  BUILD_DIR="$PWD/A1-simulator/submission/.build-offline-phase0" \
  -C "$work_dir"
```

Expected: both `probe_report_test` and `probe_smoke` pass even though Make runs outside the submission directory.

- [ ] **Step 4: Re-run the 12-case gate with the offline-built probe**

Run:

```bash
python3 A1-simulator/submission/tests/integration/probe_public.py \
  --probe A1-simulator/submission/.build-offline-phase0/bin/ir_probe \
  --cases-root A1-simulator/testcases/sim_public/benchmark \
  --top tb2 \
  --output A1-simulator/submission/tests/public/ast-inventory.json
git diff --exit-code -- A1-simulator/submission/tests/public/ast-inventory.json
```

Expected: all 12 cases elaborate and the committed baseline remains unchanged.

- [ ] **Step 5: Run final repository checks**

Run:

```bash
git diff --check
git status --short
```

Expected: no whitespace errors. Status contains no Phase 0 source changes; only pre-existing user-owned changes, if any, remain.

## Phase 0 Completion Criteria

Phase 0 is complete only when all are true:

- Slang and every build dependency are present in the submission tree with license disclosure.
- A fresh Linux build succeeds with networking disabled.
- `make build` works when invoked through an absolute `make -f` path from an isolated cwd.
- `ir_probe` accepts evaluator-style absolute filelists, `-I` options, and the caller-provided `--top`.
- Parse or elaboration errors preserve Slang diagnostics and return nonzero.
- All 12 public cases elaborate with `TOP=tb2`.
- The deterministic inventory includes symbols, statements, expressions, timing controls, data types, and system calls.
- The inventory explicitly observes `$fscanf`.
- The malformed public `GEMM/filelist_bug.txt` has not caused any simulator special case.
- ModelIR, runtime, caching, and parallel work have not leaked into this phase.

After these gates pass, create the next plan for four-state values, net resolution, and the smallest ModelIR types. Do not implement the scheduler first.
