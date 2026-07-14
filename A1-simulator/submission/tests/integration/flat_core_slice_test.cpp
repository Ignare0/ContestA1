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
using a1::runtime::SimOptions;

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

std::optional<SignalId> find_signal_suffix(const ModelIR& model, std::string_view suffix) {
    for (std::uint32_t index = 0; index < model.signals().size(); ++index) {
        const auto& path = model.signals()[index].canonical_path;
        if (path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
            return SignalId{index};
    }
    return std::nullopt;
}

std::string signal_binary(const a1::runtime::SimResult& result, SignalId signal) {
    return result.final_store ? result.final_store->value(signal).to_binary()
                              : std::string("<no final store>");
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
        const auto q = find_signal_suffix(model, ".q");
        A1_EXPECT(q.has_value());
        A1_EXPECT(signal_binary(result, *q) == "1");
    }

    {
        // Blocking vs NBA 同槽：#0 后 NBA 未提交，$finish 收尾提交，最终 a==0。
        const auto spec = fixture(
            "flat-core-slice-order-slot-zero",
            "module order_slot;\n"
            "  reg a;\n"
            "  initial begin\n"
            "    a = 1'b1;\n"
            "    a <= 1'b0;\n"
            "    #0;\n"
            "    $finish;\n"
            "  end\n"
            "endmodule\n",
            "order_slot");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto result = run_model(*compiled.model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        const auto a = find_signal(*compiled.model, "order_slot.a");
        A1_EXPECT(a.has_value());
        A1_EXPECT(signal_binary(result, *a) == "0");
    }

    {
        // Blocking vs NBA 同槽：无 #0，$finish 立即收尾提交 NBA，最终 a==0。
        const auto spec = fixture(
            "flat-core-slice-order-slot-finish",
            "module order_slot;\n"
            "  reg a;\n"
            "  initial begin\n"
            "    a = 1'b1;\n"
            "    a <= 1'b0;\n"
            "    $finish;\n"
            "  end\n"
            "endmodule\n",
            "order_slot");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto result = run_model(*compiled.model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        const auto a = find_signal(*compiled.model, "order_slot.a");
        A1_EXPECT(a.has_value());
        A1_EXPECT(signal_binary(result, *a) == "0");
    }

    {
        // 组合连续赋值 + #delay 采样：sample == a + b。
        const auto spec = fixture(
            "flat-core-slice-combo",
            "module combo_slice;\n"
            "  reg [3:0] a, b;\n"
            "  wire [3:0] sum;\n"
            "  reg [3:0] sample;\n"
            "  assign sum = a + b;\n"
            "  initial begin\n"
            "    a = 4'h1; b = 4'h2;\n"
            "    #1 sample = sum;\n"
            "    $finish;\n"
            "  end\n"
            "endmodule\n",
            "combo_slice");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto result = run_model(*compiled.model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(result.time == 1);
        const auto sample = find_signal(*compiled.model, "combo_slice.sample");
        A1_EXPECT(sample.has_value());
        A1_EXPECT(signal_binary(result, *sample) == "0011");
    }

    {
        // #0 后表达式 wire 得新值：a=1 后立刻读 y=~a 为旧值，#0 后再读为新值。
        const auto spec = fixture(
            "flat-core-slice-wire-zero",
            "module wire_zero;\n"
            "  reg a;\n"
            "  wire y;\n"
            "  reg t1, t2;\n"
            "  assign y = ~a;\n"
            "  initial begin\n"
            "    a = 1'b0;\n"
            "    #1 begin\n"
            "      a = 1'b1;\n"
            "      t1 = y;\n"
            "      #0 begin\n"
            "        t2 = y;\n"
            "        $finish;\n"
            "      end\n"
            "    end\n"
            "  end\n"
            "endmodule\n",
            "wire_zero");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto result = run_model(*compiled.model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        const auto& model = *compiled.model;
        const auto t1 = find_signal(model, "wire_zero.t1");
        const auto t2 = find_signal(model, "wire_zero.t2");
        A1_EXPECT(t1.has_value() && t2.has_value());
        A1_EXPECT(signal_binary(result, *t1) == "1");  // 旧值 ~0
        A1_EXPECT(signal_binary(result, *t2) == "0");  // #0 后新值 ~1
    }

    {
        // 不收敛环：2-state bit + assign a = ~a 触发 delta cycle limit。
        const auto spec = fixture(
            "flat-core-slice-osc",
            "module ring_slice;\n"
            "  bit a;\n"
            "  assign a = ~a;\n"
            "  initial begin\n"
            "    #1;\n"
            "    $finish;\n"
            "  end\n"
            "endmodule\n",
            "ring_slice");
        const auto compiled = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(compiled.diagnostics.empty());
        A1_EXPECT(compiled.model.has_value());
        const auto a_sig = find_signal(*compiled.model, "ring_slice.a");
        A1_EXPECT(a_sig.has_value());
        A1_EXPECT(!compiled.model->signals()[a_sig->value].type.is_four_state);
        SimOptions options;
        options.delta_limit = 4;
        const auto result = run_model(*compiled.model, options);
        A1_EXPECT(result.error.has_value());
        A1_EXPECT(result.error->find("delta cycle limit exceeded") != std::string::npos);
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
