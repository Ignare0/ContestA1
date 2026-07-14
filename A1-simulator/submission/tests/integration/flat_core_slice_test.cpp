#include "../unit/test_support.h"
#include "frontend/frontend.h"
#include "runtime/scheduler.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using a1::frontend::CompileResult;
using a1::frontend::ProjectSpec;
using a1::ir::EdgeSense;
using a1::ir::ModelIR;
using a1::ir::Process;
using a1::ir::ProcessKind;
using a1::ir::SignalId;
using a1::runtime::run_model;

ProjectSpec fixture(std::string_view name, std::string_view source,
                    std::string_view top = "top") {
    const auto directory = a1::test::make_temp_dir(name);
    const auto source_path = directory / "top.sv";
    const auto filelist = directory / "filelist.txt";
    a1::test::write_text(source_path, source);
    a1::test::write_text(filelist, source_path.string() + "\n");
    return ProjectSpec{filelist, std::string(top), {}, {}};
}

std::optional<SignalId> find_signal(const ModelIR& model, std::string_view path) {
    for (std::uint32_t index = 0; index < model.signals().size(); ++index) {
        if (model.signals()[index].canonical_path == path)
            return SignalId{index};
    }
    return std::nullopt;
}

const Process* find_process(const ModelIR& model, ProcessKind kind) {
    const auto found = std::find_if(
        model.processes().begin(), model.processes().end(),
        [kind](const auto& process) { return process.kind == kind; });
    return found == model.processes().end() ? nullptr : &*found;
}

bool reads_contain(const Process& process, SignalId signal) {
    return std::find(process.read_signals.begin(), process.read_signals.end(), signal) !=
           process.read_signals.end();
}

}  // namespace

int main() {
    {
        // Phase 1 冒烟：空 flat top 仍可编译并运行。
        const auto spec = fixture("flat-core-slice", "module top; endmodule\n");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.model.has_value());
        const auto result = run_model(*compiled.model);
        A1_EXPECT(!result.error);
    }

    {
        const auto spec = fixture(
            "flat-core-slice-core",
            "module flat_core;\n"
            "  reg clk;\n"
            "  reg d;\n"
            "  reg q;\n"
            "  initial begin\n"
            "    clk = 1'b0;\n"
            "    d = 1'b1;\n"
            "    #1 clk = 1'b1;\n"
            "    #1;\n"
            "    $finish;\n"
            "  end\n"
            "  always @(posedge clk) begin\n"
            "    q <= d;\n"
            "  end\n"
            "endmodule\n",
            "flat_core");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto& model = *compiled.model;
        A1_EXPECT(model.validate().empty());
        A1_EXPECT(model.processes().size() == 2);

        const auto* initial = find_process(model, ProcessKind::Initial);
        A1_EXPECT(initial != nullptr);
        A1_EXPECT(initial->sensitivity.empty());
        A1_EXPECT(initial->read_signals.empty());

        const auto* always = find_process(model, ProcessKind::Always);
        A1_EXPECT(always != nullptr);
        A1_EXPECT(always->sensitivity.size() == 1);
        const auto clk = find_signal(model, "flat_core.clk");
        A1_EXPECT(clk.has_value());
        A1_EXPECT(always->sensitivity.front().edge == EdgeSense::Posedge);
        A1_EXPECT(always->sensitivity.front().signal == *clk);

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(result.final_store != nullptr);
        const auto q = find_signal(model, "flat_core.q");
        A1_EXPECT(q.has_value());
        A1_EXPECT(result.final_store->value(*q).to_binary() == "1");
    }

    {
        // 层级仍 fail-closed。
        const auto spec = fixture(
            "flat-core-slice-child",
            "module child(input wire a, output wire y); assign y = a; endmodule\n"
            "module top(input wire a, output wire y);\n"
            "  child dut(.a(a), .y(y));\n"
            "endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(result.diagnostics.size() == 1);
        A1_EXPECT(result.diagnostics.front().message.find("unsupported child instance") !=
                  std::string::npos);
    }

    {
        // @* read_signals：if 条件 + 两分支 RHS；目标 q 因 RHS 出现而收集。
        const auto spec = fixture(
            "flat-core-slice-implicit-if",
            "module top;\n"
            "  reg en, d, q;\n"
            "  always @(*) if (en) q = d; else q = q;\n"
            "endmodule\n");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto& model = *compiled.model;
        const auto* always = find_process(model, ProcessKind::Always);
        A1_EXPECT(always != nullptr);
        A1_EXPECT(always->sensitivity.empty());
        A1_EXPECT(always->read_signals.size() == 3);
        for (const auto name : {"top.en", "top.d", "top.q"}) {
            const auto signal = find_signal(model, name);
            A1_EXPECT(signal.has_value());
            A1_EXPECT(reads_contain(*always, *signal));
        }
    }

    {
        // @* read_signals：q = a + b 收集 {a, b}，不含目标 q。
        const auto spec = fixture(
            "flat-core-slice-implicit-add",
            "module top;\n"
            "  reg [3:0] a, b, q;\n"
            "  always @(*) q = a + b;\n"
            "endmodule\n");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto& model = *compiled.model;
        const auto* always = find_process(model, ProcessKind::Always);
        A1_EXPECT(always != nullptr);
        A1_EXPECT(always->read_signals.size() == 2);
        const auto a = find_signal(model, "top.a");
        const auto b = find_signal(model, "top.b");
        const auto q = find_signal(model, "top.q");
        A1_EXPECT(a.has_value() && b.has_value() && q.has_value());
        A1_EXPECT(reads_contain(*always, *a));
        A1_EXPECT(reads_contain(*always, *b));
        A1_EXPECT(!reads_contain(*always, *q));
    }

    {
        // Phase 1 表达式 lowering 只接受常量下标：RHS a[i]（i 非常量）fail-closed。
        const auto spec = fixture(
            "flat-core-slice-dynamic-rhs",
            "module top;\n"
            "  reg [3:0] a;\n"
            "  reg [1:0] i;\n"
            "  reg y;\n"
            "  always @(*) y = a[i];\n"
            "endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(result.diagnostics.size() == 1);
        A1_EXPECT(result.diagnostics.front().message.starts_with("unsupported"));
    }

    {
        // 动态下标 lvalue a[i] = d 同样 fail-closed。
        const auto spec = fixture(
            "flat-core-slice-dynamic-lhs",
            "module top;\n"
            "  reg [3:0] a;\n"
            "  reg [1:0] i;\n"
            "  reg d;\n"
            "  always @(*) a[i] = d;\n"
            "endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(result.diagnostics.size() == 1);
        A1_EXPECT(result.diagnostics.front().message.starts_with(
            "unsupported procedural assignment target"));
    }

    {
        // 常量下标 lvalue：目标基信号 a 不进入 read_signals。
        const auto spec = fixture(
            "flat-core-slice-const-select-target",
            "module top;\n"
            "  reg [1:0] a;\n"
            "  reg d;\n"
            "  always @(*) a[0] = d;\n"
            "endmodule\n");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto& model = *compiled.model;
        const auto* always = find_process(model, ProcessKind::Always);
        A1_EXPECT(always != nullptr);
        A1_EXPECT(always->read_signals.size() == 1);
        const auto d = find_signal(model, "top.d");
        A1_EXPECT(d.has_value());
        A1_EXPECT(reads_contain(*always, *d));
    }

    {
        // $display/$error 降级为无实参 DisplayStubStmt。
        const auto spec = fixture(
            "flat-core-slice-display",
            "module top;\n"
            "  reg v;\n"
            "  initial begin\n"
            "    $display(\"v=%b\", v);\n"
            "    $error(\"bad %b\", v);\n"
            "    $finish;\n"
            "  end\n"
            "endmodule\n");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto& model = *compiled.model;
        std::size_t display_stubs = 0;
        for (const auto& statement : model.statements()) {
            if (std::holds_alternative<a1::ir::DisplayStubStmt>(statement.payload))
                ++display_stubs;
        }
        A1_EXPECT(display_stubs == 2);
        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
    }

    {
        // always_comb 一律 fail-closed，不按 @* 降级。
        const auto spec = fixture(
            "flat-core-slice-always-comb",
            "module top;\n"
            "  reg a;\n"
            "  logic y;\n"
            "  always_comb y = a;\n"
            "endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(result.diagnostics.size() == 1);
        A1_EXPECT(
            result.diagnostics.front().message.starts_with("unsupported procedural block"));
    }

    return EXIT_SUCCESS;
}
