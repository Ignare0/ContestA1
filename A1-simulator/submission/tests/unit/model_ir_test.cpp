#include "test_support.h"
#include "ir/model_ir.h"

#include <algorithm>
#include <string_view>
#include <variant>

namespace {

using a1::ir::BinaryOp;
using a1::ir::ContinuousAssignId;
using a1::ir::ExprId;
using a1::ir::ModelIR;
using a1::ir::PackedType;
using a1::ir::RangeSelectLValue;
using a1::ir::SignalKind;
using a1::ir::SourceSpan;
using a1::ir::UnaryOp;

constexpr PackedType kLogic2{2, false, true};
constexpr PackedType kLogic4{4, false, true};
const SourceSpan kSource{"model_ir_test.sv", 1, 1};

bool contains(const std::vector<std::string>& diagnostics, std::string_view needle) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [needle](const std::string& diagnostic) {
                           return diagnostic.find(needle) != std::string::npos;
                       });
}

std::size_t order_index(const std::vector<ContinuousAssignId>& order,
                        ContinuousAssignId assignment) {
    return static_cast<std::size_t>(
        std::find(order.begin(), order.end(), assignment) - order.begin());
}

}  // namespace

int main() {
    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic4, SignalKind::Variable, kSource);
        const auto mid = model.add_signal("top.mid", kLogic4, SignalKind::Net, kSource);
        const auto y = model.add_signal("top.y", kLogic4, SignalKind::Net, kSource);

        const auto a_ref = model.add_signal_ref(a, kLogic4, kSource);
        const auto not_a = model.add_unary(UnaryOp::BitwiseNot, a_ref, kLogic4, kSource);
        const auto mid_target = model.add_whole_signal_lvalue(mid, kLogic4, kSource);
        const auto mid_assign = model.add_continuous_assign(mid_target, not_a, kSource);
        const auto mid_ref = model.add_signal_ref(mid, kLogic4, kSource);
        const auto y_target = model.add_whole_signal_lvalue(y, kLogic4, kSource);
        const auto y_assign = model.add_continuous_assign(y_target, mid_ref, kSource);

        A1_EXPECT(model.validate().empty());
        const auto order = model.continuous_order();
        A1_EXPECT(order.has_value());
        A1_EXPECT(*order == std::vector<ContinuousAssignId>({mid_assign, y_assign}));
    }

    {
        ModelIR model;
        const auto x = model.add_signal("top.x", kLogic2, SignalKind::Net, kSource);
        const auto z = model.add_signal("top.z", kLogic2, SignalKind::Net, kSource);
        const auto x_target = model.add_whole_signal_lvalue(x, kLogic2, kSource);
        const auto z_target = model.add_whole_signal_lvalue(z, kLogic2, kSource);
        static_cast<void>(model.add_continuous_assign(
            x_target, model.add_signal_ref(z, kLogic2, kSource), kSource));
        static_cast<void>(model.add_continuous_assign(
            z_target, model.add_signal_ref(x, kLogic2, kSource), kSource));

        A1_EXPECT(model.validate().empty());
        A1_EXPECT(!model.continuous_order().has_value());
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic2, SignalKind::Variable, kSource);
        const auto b = model.add_signal("top.b", kLogic2, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic2, SignalKind::Net, kSource);
        const auto z = model.add_signal("top.z", kLogic2, SignalKind::Net, kSource);

        const auto first_y = model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic2, kSource),
            model.add_signal_ref(a, kLogic2, kSource), kSource);
        const auto z_from_y = model.add_continuous_assign(
            model.add_whole_signal_lvalue(z, kLogic2, kSource),
            model.add_signal_ref(y, kLogic2, kSource), kSource);
        const auto second_y = model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic2, kSource),
            model.add_signal_ref(b, kLogic2, kSource), kSource);

        A1_EXPECT(model.continuous_assigns().size() == 3);
        A1_EXPECT(std::get<a1::ir::WholeSignalLValue>(
                      model.lvalues().at(
                          model.continuous_assigns().at(first_y.value).target.value)
                          .payload)
                      .signal == y);
        A1_EXPECT(std::get<a1::ir::WholeSignalLValue>(
                      model.lvalues().at(
                          model.continuous_assigns().at(second_y.value).target.value)
                          .payload)
                      .signal == y);

        const auto order = model.continuous_order();
        A1_EXPECT(order.has_value());
        A1_EXPECT(order_index(*order, first_y) < order_index(*order, z_from_y));
        A1_EXPECT(order_index(*order, second_y) < order_index(*order, z_from_y));
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic2, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic2, SignalKind::Net, kSource);
        const auto z = model.add_signal("top.z", kLogic2, SignalKind::Net, kSource);

        const auto z_assign = model.add_continuous_assign(
            model.add_whole_signal_lvalue(z, kLogic2, kSource), ExprId{0}, kSource);
        const auto y_ref = model.add_signal_ref(y, kLogic2, kSource);
        const auto y_assign = model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic2, kSource),
            model.add_signal_ref(a, kLogic2, kSource), kSource);

        A1_EXPECT(model.validate().empty());
        const auto order = model.continuous_order();
        A1_EXPECT(order.has_value());
        A1_EXPECT(order_index(*order, y_assign) < order_index(*order, z_assign));
        A1_EXPECT(model.continuous_assigns().at(z_assign.value).read_signals ==
                  std::vector<a1::ir::SignalId>({y}));
        A1_EXPECT(y_ref.value == 0);
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic2, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic4, SignalKind::Net, kSource);
        const auto a_ref = model.add_signal_ref(a, kLogic2, kSource);
        const auto nested = model.add_concat(
            {a_ref, model.add_unary(UnaryOp::BitwiseNot, a_ref, kLogic2, kSource)},
            kLogic4, kSource);
        const auto assignment = model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic4, kSource), nested, kSource);

        A1_EXPECT(model.continuous_assigns().at(assignment.value).read_signals ==
                  std::vector<a1::ir::SignalId>({a}));
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic2, SignalKind::Variable, kSource);
        const auto b = model.add_signal("top.b", kLogic2, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic4, SignalKind::Net, kSource);
        const auto upper = model.add_range_select_lvalue(y, 2, 2, kLogic2, kSource);
        const auto lower = model.add_range_select_lvalue(y, 0, 2, kLogic2, kSource);
        static_cast<void>(model.add_continuous_assign(
            upper, model.add_signal_ref(a, kLogic2, kSource), kSource));
        static_cast<void>(model.add_continuous_assign(
            lower, model.add_signal_ref(b, kLogic2, kSource), kSource));

        const auto& upper_lvalue = model.lvalues().at(upper.value);
        A1_EXPECT(upper_lvalue.type.width == 2);
        A1_EXPECT(std::get<RangeSelectLValue>(upper_lvalue.payload).bit_offset == 2);
        A1_EXPECT(model.validate().empty());
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic2, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic4, SignalKind::Net, kSource);
        const auto valid_target = model.add_range_select_lvalue(y, 0, 2, kLogic2, kSource);
        static_cast<void>(model.add_range_select_lvalue(y, 3, 2, kLogic2, kSource));
        static_cast<void>(model.add_bit_select_lvalue(y, 0, kLogic2, kSource));
        static_cast<void>(model.add_whole_signal_lvalue(a, kLogic2, kSource));
        static_cast<void>(model.add_continuous_assign(
            valid_target, model.add_signal_ref(y, kLogic4, kSource), kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic4, kSource),
            model.add_signal_ref(a, kLogic2, kSource), kSource));

        const auto diagnostics = model.validate();
        A1_EXPECT(contains(diagnostics, "lvalue select is out of range"));
        A1_EXPECT(contains(diagnostics, "lvalue type does not match select width"));
        A1_EXPECT(contains(diagnostics, "lvalue base is not a net"));
        A1_EXPECT(contains(diagnostics, "target/value width mismatch"));
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic4, SignalKind::Variable, kSource);
        const auto a_ref = model.add_signal_ref(a, kLogic4, kSource);
        static_cast<void>(model.add_bit_select(a_ref, 4, PackedType{1, false, true}, kSource));
        static_cast<void>(model.add_range_select(a_ref, 3, 2,
                                                 PackedType{3, false, true}, kSource));
        static_cast<void>(model.add_range_select(a_ref, 0, 2,
                                                 PackedType{2, false, false}, kSource));

        const auto diagnostics = model.validate();
        A1_EXPECT(contains(diagnostics, "expression select is out of range"));
        A1_EXPECT(contains(diagnostics, "expression type does not match select width"));
        A1_EXPECT(contains(diagnostics,
                           "expression four-state domain does not match selected value"));
    }

    return EXIT_SUCCESS;
}
