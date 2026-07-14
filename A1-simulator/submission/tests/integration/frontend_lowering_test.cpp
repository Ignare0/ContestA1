#include "../unit/test_support.h"
#include "frontend/frontend.h"
#include "runtime/continuous_evaluator.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using a1::frontend::CompileResult;
using a1::frontend::ProjectSpec;
using a1::ir::ModelIR;
using a1::ir::SignalKind;
using a1::runtime::ContinuousEvaluator;
using a1::runtime::LogicValue;
using a1::runtime::SignalStore;

ProjectSpec fixture(std::string_view name, std::string_view source,
                    std::string_view top = "top") {
    const auto directory = a1::test::make_temp_dir(name);
    const auto source_path = directory / "top with spaces.sv";
    const auto filelist = directory / "file list.txt";
    a1::test::write_text(source_path, source);
    a1::test::write_text(filelist, "\"" + source_path.string() + "\"\n");
    return ProjectSpec{filelist, std::string(top), {}, {}};
}

const a1::ir::Signal* find_signal(const ModelIR& model, std::string_view path) {
    const auto found = std::find_if(
        model.signals().begin(), model.signals().end(),
        [path](const auto& signal) { return signal.canonical_path == path; });
    return found == model.signals().end() ? nullptr : &*found;
}

bool has_message(const CompileResult& result, std::string_view prefix) {
    return result.diagnostics.size() == 1 &&
           result.diagnostics.front().message.starts_with(prefix);
}

}  // namespace

int main() {
    {
        const auto spec = fixture(
            "frontend-lowering-happy",
            "module top(input wire [15:0] a, input wire [15:0] b, input wire sel,\n"
            "           output wire [31:0] y, output wire cmp, output wire red);\n"
            "  wire [15:0] sumv, diffv;\n"
            "  wire low_all_ones;\n"
            "  assign sumv = a + b;\n"
            "  assign diffv = a - b;\n"
            "  assign cmp = ({1'b0, a} + {1'b0, b}) >= 17'h08000;\n"
            "  assign red = ^{a[3:0], b[3:0]};\n"
            "  assign low_all_ones = &a[7:0];\n"
            "  assign y = sel ? {sumv, diffv} : {a[15:8], b[7:0], 7'b0, a[0], 7'b0, low_all_ones};\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        auto& model = *result.model;
        A1_EXPECT(model.validate().empty());
        A1_EXPECT(model.continuous_assigns().size() == 6);
        const auto* a = find_signal(model, "top.a");
        const auto* b = find_signal(model, "top.b");
        const auto* sel = find_signal(model, "top.sel");
        const auto* y = find_signal(model, "top.y");
        const auto* cmp = find_signal(model, "top.cmp");
        const auto* red = find_signal(model, "top.red");
        A1_EXPECT(a != nullptr && a->kind == SignalKind::Net);
        A1_EXPECT(b != nullptr && b->kind == SignalKind::Net);
        A1_EXPECT(sel != nullptr && sel->kind == SignalKind::Net);
        A1_EXPECT(y != nullptr && y->kind == SignalKind::Net);
        A1_EXPECT(cmp != nullptr && red != nullptr);
        SignalStore store(model);
        store.set_external_driver({static_cast<std::uint32_t>(a - &model.signals().front())},
                                  LogicValue::from_binary("0000000011111111"));
        store.set_external_driver({static_cast<std::uint32_t>(b - &model.signals().front())},
                                  LogicValue::from_binary("0000000000000001"));
        store.set_external_driver({static_cast<std::uint32_t>(sel - &model.signals().front())},
                                  LogicValue::ones(1));
        A1_EXPECT(!ContinuousEvaluator::settle(model, store).has_value());
        A1_EXPECT(store.value({static_cast<std::uint32_t>(y - &model.signals().front())})
                      .to_binary() == "00000001000000000000000011111110");
        A1_EXPECT(store.value({static_cast<std::uint32_t>(cmp - &model.signals().front())})
                      .to_binary() == "0");
        A1_EXPECT(store.value({static_cast<std::uint32_t>(red - &model.signals().front())})
                      .to_binary() == "1");
    }

    {
        const auto spec = fixture(
            "frontend-lowering-ports",
            "module top(input var logic v, input logic n, output wire y);\n"
            "  assign y = v & n;\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        const auto& model = *result.model;
        A1_EXPECT(model.signals().size() == 3);
        A1_EXPECT(find_signal(model, "top.v") != nullptr);
        A1_EXPECT(find_signal(model, "top.n") != nullptr);
        A1_EXPECT(find_signal(model, "top.y") != nullptr);
        A1_EXPECT(find_signal(model, "top.v")->kind == SignalKind::Variable);
        A1_EXPECT(find_signal(model, "top.n")->kind == SignalKind::Net);
        A1_EXPECT(find_signal(model, "top.y")->kind == SignalKind::Net);
    }

    {
        const auto spec = fixture(
            "frontend-lowering-drivers",
            "module top(input wire a, input wire b, output wire y, output wire z);\n"
            "  assign y = a; assign z = y; assign y = b;\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        const auto& model = *result.model;
        A1_EXPECT(model.continuous_assigns().size() == 3);
        const auto order = model.continuous_order();
        A1_EXPECT(order.has_value());
        const auto* a = find_signal(model, "top.a");
        const auto* b = find_signal(model, "top.b");
        const auto* y = find_signal(model, "top.y");
        const auto* z = find_signal(model, "top.z");
        A1_EXPECT(a != nullptr && b != nullptr && y != nullptr && z != nullptr);
        const auto a_id =
            a1::ir::SignalId{static_cast<std::uint32_t>(a - &model.signals().front())};
        const auto b_id =
            a1::ir::SignalId{static_cast<std::uint32_t>(b - &model.signals().front())};
        const auto y_id =
            a1::ir::SignalId{static_cast<std::uint32_t>(y - &model.signals().front())};
        const auto z_id =
            a1::ir::SignalId{static_cast<std::uint32_t>(z - &model.signals().front())};

        std::vector<a1::ir::ContinuousAssignId> y_producers;
        std::optional<a1::ir::ContinuousAssignId> z_consumer;
        for (std::uint32_t i = 0; i < model.continuous_assigns().size(); ++i) {
            const auto& assignment = model.continuous_assigns()[i];
            const auto& target = model.lvalues().at(assignment.target.value);
            const auto* whole = std::get_if<a1::ir::WholeSignalLValue>(&target.payload);
            A1_EXPECT(whole != nullptr);
            if (whole->signal == y_id)
                y_producers.push_back({i});
            if (whole->signal == z_id)
                z_consumer = a1::ir::ContinuousAssignId{i};
        }
        A1_EXPECT(y_producers.size() == 2);
        A1_EXPECT(z_consumer.has_value());
        const auto z_pos =
            std::find(order->begin(), order->end(), *z_consumer) - order->begin();
        for (const auto producer : y_producers) {
            const auto producer_pos =
                std::find(order->begin(), order->end(), producer) - order->begin();
            A1_EXPECT(producer_pos >= 0 && producer_pos < z_pos);
        }

        const struct {
            std::string_view a;
            std::string_view b;
            std::string_view y;
            std::string_view z;
        } cases[] = {
            {"0", "z", "0", "0"},
            {"z", "1", "1", "1"},
            {"z", "z", "z", "z"},
            {"0", "1", "x", "x"},
        };
        for (const auto& test_case : cases) {
            SignalStore store(model);
            store.set_external_driver(a_id, LogicValue::from_binary(test_case.a));
            store.set_external_driver(b_id, LogicValue::from_binary(test_case.b));
            A1_EXPECT(!ContinuousEvaluator::settle(model, store).has_value());
            A1_EXPECT(store.value(y_id).to_binary() == test_case.y);
            A1_EXPECT(store.value(z_id).to_binary() == test_case.z);
        }
    }

    {
        const auto spec = fixture(
            "frontend-lowering-cycle",
            "module top(output wire x, output wire z);\n"
            "  assign x = z; assign z = x;\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        A1_EXPECT(!result.model->continuous_order().has_value());
        SignalStore store(*result.model);
        const auto error = ContinuousEvaluator::settle(*result.model, store);
        A1_EXPECT(error == "cyclic continuous assignments unsupported by phase-1 evaluator");
    }

    {
        const auto spec = fixture(
            "frontend-lowering-net-initializer",
            "module top(input wire [3:0] a, output wire [3:0] y);\n"
            "  wire [3:0] mid = a;\n"
            "  localparam logic [3:0] MASK = 4'b0101;\n"
            "  assign y = mid & MASK;\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        const auto& model = *result.model;
        A1_EXPECT(model.continuous_assigns().size() == 2);
        const auto* mid = find_signal(model, "top.mid");
        A1_EXPECT(mid != nullptr);
        const auto mid_id =
            a1::ir::SignalId{static_cast<std::uint32_t>(mid - &model.signals().front())};
        bool found_mid_driver = false;
        bool found_mask_constant = false;
        for (const auto& assignment : model.continuous_assigns()) {
            const auto& target = model.lvalues().at(assignment.target.value);
            if (const auto* whole = std::get_if<a1::ir::WholeSignalLValue>(&target.payload);
                whole != nullptr && whole->signal == mid_id) {
                found_mid_driver = true;
            }
            const auto& value = model.expressions().at(assignment.value.value);
            if (std::holds_alternative<a1::ir::BinaryExpr>(value.payload)) {
                const auto& binary = std::get<a1::ir::BinaryExpr>(value.payload);
                const auto& rhs = model.expressions().at(binary.rhs.value);
                if (std::holds_alternative<a1::ir::ConstantExpr>(rhs.payload)) {
                    found_mask_constant = true;
                    A1_EXPECT(std::get<a1::ir::ConstantExpr>(rhs.payload).value.to_binary() ==
                              "0101");
                }
            }
        }
        A1_EXPECT(found_mid_driver);
        A1_EXPECT(found_mask_constant);
    }

    {
        const auto spec = fixture(
            "frontend-lowering-generate",
            "module top(input wire [3:0] a, output wire [3:0] y);\n"
            "  genvar i;\n"
            "  for (i = 0; i < 4; i = i + 1) begin : g\n"
            "    assign y[i] = ~a[i];\n"
            "  end\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        A1_EXPECT(result.model->continuous_assigns().size() == 4);
    }

    {
        const auto spec = fixture(
            "frontend-lowering-two-state",
            "module top(input var bit [3:0] a, input logic [3:0] f,\n"
            "           output wire [3:0] two_image, output wire [3:0] four);\n"
            "  typedef bit [3:0] bit4_t;\n"
            "  assign two_image = bit4_t'(f);\n"
            "  assign four = f;\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        const auto& model = *result.model;
        const auto* a = find_signal(model, "top.a");
        const auto* f = find_signal(model, "top.f");
        const auto* two = find_signal(model, "top.two_image");
        const auto* four = find_signal(model, "top.four");
        A1_EXPECT(a != nullptr && f != nullptr && two != nullptr && four != nullptr);
        A1_EXPECT(a->kind == SignalKind::Variable);
        A1_EXPECT(!a->type.is_four_state);
        A1_EXPECT(f->type.is_four_state);
        A1_EXPECT(two->type.is_four_state);
        A1_EXPECT(four->type.is_four_state);
        SignalStore store(model);
        store.set_variable({static_cast<std::uint32_t>(a - &model.signals().front())},
                           LogicValue::from_binary("10xz"));
        A1_EXPECT(store.value({static_cast<std::uint32_t>(a - &model.signals().front())})
                      .to_binary() == "1000");
        store.set_external_driver({static_cast<std::uint32_t>(f - &model.signals().front())},
                                  LogicValue::from_binary("10xz"));
        A1_EXPECT(!ContinuousEvaluator::settle(model, store).has_value());
        A1_EXPECT(store.value({static_cast<std::uint32_t>(two - &model.signals().front())})
                      .to_binary() == "1000");
        A1_EXPECT(store.value({static_cast<std::uint32_t>(four - &model.signals().front())})
                      .to_binary() == "10xz");
    }

    {
        const auto spec = fixture(
            "frontend-lowering-conditional",
            "module top(input logic [3:0] cond, output wire [3:0] y);\n"
            "  assign y = cond ? 4'ha : 4'h5;\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        const auto& model = *result.model;
        const auto* cond = find_signal(model, "top.cond");
        const auto* y = find_signal(model, "top.y");
        A1_EXPECT(cond != nullptr && y != nullptr);
        SignalStore store(model);
        store.set_external_driver({static_cast<std::uint32_t>(cond - &model.signals().front())},
                                  LogicValue::from_binary("x1z0"));
        A1_EXPECT(!ContinuousEvaluator::settle(model, store).has_value());
        A1_EXPECT(store.value({static_cast<std::uint32_t>(y - &model.signals().front())})
                      .to_binary() == "1010");
        store.set_external_driver({static_cast<std::uint32_t>(cond - &model.signals().front())},
                                  LogicValue::from_binary("00xz"));
        A1_EXPECT(!ContinuousEvaluator::settle(model, store).has_value());
        A1_EXPECT(store.value({static_cast<std::uint32_t>(y - &model.signals().front())})
                      .to_binary() == "xxxx");
    }

    {
        const auto spec = fixture(
            "frontend-lowering-ascending-select",
            "module top(input wire [0:3] a, output wire [0:3] y);\n"
            "  assign y[0:1] = a[0:1];\n"
            "endmodule\n");
        auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(result.model.has_value());
        A1_EXPECT(result.diagnostics.empty());
        const auto& model = *result.model;
        A1_EXPECT(model.lvalues().size() == 1);
        const auto& target = std::get<a1::ir::RangeSelectLValue>(model.lvalues().front().payload);
        A1_EXPECT(target.bit_offset == 2);
        A1_EXPECT(target.width == 2);
        A1_EXPECT(model.continuous_assigns().size() == 1);
        const auto& rhs =
            model.expressions().at(model.continuous_assigns().front().value.value);
        const auto& select = std::get<a1::ir::RangeSelectExpr>(rhs.payload);
        A1_EXPECT(select.bit_offset == 2);
        A1_EXPECT(select.width == 2);
    }

    {
        const auto spec = fixture(
            "frontend-lowering-special-net",
            "module top(input wire a, output wand y); assign y = a; endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(result.diagnostics.size() == 1);
        A1_EXPECT(result.diagnostics.front().message.starts_with("unsupported special net"));
    }

    {
        const auto spec = fixture(
            "frontend-lowering-unsupported-op",
            "module top(input wire a, input wire b, output wire y);\n"
            "  assign y = a * b;\n"
            "endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(has_message(result, "unsupported BinaryOp at "));
        A1_EXPECT(result.diagnostics.front().source.line == 2);
        A1_EXPECT(result.diagnostics.front().source.column == 16);
        A1_EXPECT(result.diagnostics.front().message ==
                  "unsupported BinaryOp at " + result.diagnostics.front().source.file +
                      ":2:16");
    }

    {
        const auto spec = fixture(
            "frontend-lowering-child",
            "module child(input wire a, output wire y); assign y = a; endmodule\n"
            "module top(input wire a, output wire y);\n"
            "  child u_child(.a(a), .y(y));\n"
            "endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(result.diagnostics.size() == 1);
        A1_EXPECT(result.diagnostics.front().message ==
                  "unsupported child instance top.u_child: port binding not implemented");
    }

    {
        const auto spec = fixture(
            "frontend-lowering-procedural",
            "module top(input wire a, output logic y);\n"
            "  always_comb y = a;\n"
            "endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(result.diagnostics.size() == 1);
        A1_EXPECT(result.diagnostics.front().message.starts_with("unsupported procedural block"));
    }

    {
        const auto spec = fixture(
            "frontend-lowering-variable-initializer",
            "module top(output logic y);\n"
            "  logic x = 1'b0; assign y = x;\n"
            "endmodule\n");
        const auto result = a1::frontend::compile_to_ir(spec);
        A1_EXPECT(!result.model.has_value());
        A1_EXPECT(result.diagnostics.size() == 1);
        A1_EXPECT(
            result.diagnostics.front().message.starts_with("unsupported variable initializer"));
    }

    return EXIT_SUCCESS;
}
