#include "test_support.h"
#include "ir/model_ir.h"
#include "runtime/continuous_evaluator.h"

#include <optional>
#include <string>

namespace {

using a1::ir::BinaryOp;
using a1::ir::ModelIR;
using a1::ir::PackedType;
using a1::ir::SignalKind;
using a1::ir::SourceSpan;
using a1::runtime::ContinuousEvaluator;
using a1::runtime::LogicValue;
using a1::runtime::SignalStore;

constexpr PackedType kBit1{1, false, false};
constexpr PackedType kBit2{2, false, false};
constexpr PackedType kLogic1{1, false, true};
constexpr PackedType kLogic2{2, false, true};
constexpr PackedType kLogic4{4, false, true};
constexpr PackedType kLogic8{8, false, true};
constexpr PackedType kSignedLogic4{4, true, true};
const SourceSpan kSource{"continuous_evaluator_test.sv", 1, 1};

bool settles(const ModelIR& model, SignalStore& store) {
    const std::optional<std::string> error = ContinuousEvaluator::settle(model, store);
    return !error.has_value();
}

}  // namespace

int main() {
    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic8, SignalKind::Variable, kSource);
        const auto b = model.add_signal("top.b", kLogic8, SignalKind::Variable, kSource);
        const auto sum = model.add_signal("top.sum", kLogic8, SignalKind::Net, kSource);
        const auto y = model.add_signal("top.y", kLogic8, SignalKind::Net, kSource);
        const auto add = model.add_binary(
            BinaryOp::Add, model.add_signal_ref(a, kLogic8, kSource),
            model.add_signal_ref(b, kLogic8, kSource), kLogic8, kLogic8, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(sum, kLogic8, kSource), add, kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic8, kSource),
            model.add_signal_ref(sum, kLogic8, kSource), kSource));

        SignalStore store(model);
        store.set_variable(a, LogicValue::from_binary("11111111"));
        store.set_variable(b, LogicValue::from_binary("00000001"));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(sum).to_binary() == "00000000");
        A1_EXPECT(store.value(y).to_binary() == "00000000");

        store.set_variable(a, LogicValue::from_binary("0000x000"));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(sum).to_binary() == "xxxxxxxx");
        A1_EXPECT(store.value(y).to_binary() == "xxxxxxxx");
    }

    {
        ModelIR model;
        const auto four_variable =
            model.add_signal("top.four_variable", kLogic4, SignalKind::Variable, kSource);
        const auto two_variable =
            model.add_signal("top.two_variable", kBit2, SignalKind::Variable, kSource);
        const auto four_net = model.add_signal("top.four_net", kLogic4, SignalKind::Net,
                                               kSource);
        const auto two_net =
            model.add_signal("top.two_net", kBit2, SignalKind::Net, kSource);
        const auto four_result =
            model.add_signal("top.four_result", kLogic2, SignalKind::Net, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(four_result, kLogic2, kSource),
            model.add_signal_ref(two_variable, kBit2, kSource), kSource));

        SignalStore store(model);
        A1_EXPECT(store.value(four_variable).to_binary() == "xxxx");
        A1_EXPECT(store.value(two_variable).to_binary() == "00");
        A1_EXPECT(store.value(four_net).to_binary() == "zzzz");
        A1_EXPECT(store.value(two_net).to_binary() == "00");
        store.set_variable(two_variable, LogicValue::from_binary("xz"));
        A1_EXPECT(store.value(two_variable).to_binary() == "00");
        store.set_variable(two_variable, LogicValue::z(2));
        A1_EXPECT(store.value(two_variable).to_binary() == "00");
        store.set_external_driver(two_net, LogicValue::ones(2));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(four_result).to_binary() == "00");
        A1_EXPECT(store.value(two_net).to_binary() == "11");
        store.set_external_driver(two_net, LogicValue::from_binary("xz"));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(two_net).to_binary() == "00");
    }

    {
        ModelIR model;
        const auto high = model.add_signal("top.high", kLogic2, SignalKind::Variable, kSource);
        const auto low = model.add_signal("top.low", kLogic2, SignalKind::Variable, kSource);
        const auto two_state =
            model.add_signal("top.two_state", kBit2, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic4, SignalKind::Net, kSource);
        const auto y_two_state =
            model.add_signal("top.y_two_state", kLogic4, SignalKind::Net, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_range_select_lvalue(y, 2, 2, kLogic2, kSource),
            model.add_signal_ref(high, kLogic2, kSource), kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_range_select_lvalue(y, 0, 2, kLogic2, kSource),
            model.add_signal_ref(low, kLogic2, kSource), kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_range_select_lvalue(y_two_state, 2, 2, kLogic2, kSource),
            model.add_signal_ref(two_state, kBit2, kSource), kSource));

        SignalStore store(model);
        store.set_variable(high, LogicValue::from_binary("10"));
        store.set_variable(low, LogicValue::from_binary("01"));
        store.set_variable(two_state, LogicValue::from_binary("xz"));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(y).to_binary() == "1001");
        A1_EXPECT(store.value(y_two_state).to_binary() == "00zz");
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic1, SignalKind::Variable, kSource);
        const auto b = model.add_signal("top.b", kLogic1, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic1, SignalKind::Net, kSource);
        const auto z = model.add_signal("top.z", kLogic1, SignalKind::Net, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic1, kSource),
            model.add_signal_ref(a, kLogic1, kSource), kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(z, kLogic1, kSource),
            model.add_signal_ref(y, kLogic1, kSource), kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic1, kSource),
            model.add_signal_ref(b, kLogic1, kSource), kSource));

        SignalStore store(model);
        const auto has_values = [&](std::string a_value, std::string b_value,
                                    std::string y_value, std::string z_value) {
            store.set_variable(a, LogicValue::from_binary(a_value));
            store.set_variable(b, LogicValue::from_binary(b_value));
            return settles(model, store) && store.value(y).to_binary() == y_value &&
                   store.value(z).to_binary() == z_value;
        };
        A1_EXPECT(has_values("0", "z", "0", "0"));
        A1_EXPECT(has_values("z", "1", "1", "1"));
        A1_EXPECT(has_values("z", "z", "z", "z"));
        A1_EXPECT(has_values("0", "1", "x", "x"));
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic1, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic1, SignalKind::Net, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic1, kSource),
            model.add_signal_ref(a, kLogic1, kSource), kSource));

        SignalStore store(model);
        store.set_variable(a, LogicValue::zeros(1));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(y).to_binary() == "0");
        store.set_external_driver(y, LogicValue::ones(1));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(y).to_binary() == "x");
        store.set_external_driver(y, LogicValue::zeros(1));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(y).to_binary() == "0");
        store.set_external_driver(y, LogicValue::z(1));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(y).to_binary() == "0");
    }

    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kBit1, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kBit1, SignalKind::Net, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kBit1, kSource),
            model.add_signal_ref(a, kBit1, kSource), kSource));

        SignalStore store(model);
        store.set_variable(a, LogicValue::ones(1));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(y).to_binary() == "1");
        store.set_external_driver(y, LogicValue::z(1));
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(y).to_binary() == "1");
    }

    {
        ModelIR model;
        const auto x = model.add_signal("top.x", kLogic1, SignalKind::Net, kSource);
        const auto y = model.add_signal("top.y", kLogic1, SignalKind::Net, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(x, kLogic1, kSource),
            model.add_signal_ref(y, kLogic1, kSource), kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic1, kSource),
            model.add_signal_ref(x, kLogic1, kSource), kSource));

        SignalStore store(model);
        const auto error = ContinuousEvaluator::settle(model, store);
        A1_EXPECT(error == "cyclic continuous assignments unsupported by phase-1 evaluator");
    }

    {
        ModelIR model;
        static_cast<void>(model.add_continuous_assign(a1::ir::LValueId{0}, a1::ir::ExprId{0},
                                                       kSource));

        SignalStore store(model);
        const auto error = ContinuousEvaluator::settle(model, store);
        A1_EXPECT(error == "invalid model: assignment target is out of range");
    }

    {
        ModelIR model;
        const auto signed_result =
            model.add_signal("top.signed_result", kLogic1, SignalKind::Net, kSource);
        const auto unsigned_result =
            model.add_signal("top.unsigned_result", kLogic1, SignalKind::Net, kSource);
        const auto arithmetic_shift =
            model.add_signal("top.arithmetic_shift", kLogic4, SignalKind::Net, kSource);
        const auto truncated_shift =
            model.add_signal("top.truncated_shift", kLogic2, SignalKind::Net, kSource);
        const auto negative =
            model.add_constant(LogicValue::from_binary("1000"), kSignedLogic4, kSource);
        const auto positive =
            model.add_constant(LogicValue::from_binary("0001"), kSignedLogic4, kSource);
        const auto unsigned_negative =
            model.add_constant(LogicValue::from_binary("1000"), kLogic4, kSource);
        const auto unsigned_positive =
            model.add_constant(LogicValue::from_binary("0001"), kLogic4, kSource);
        const auto one = model.add_constant(LogicValue::from_binary("1"), kLogic1, kSource);
        const auto signed_ge = model.add_binary(BinaryOp::GreaterEqual, negative, positive,
                                                kSignedLogic4, kLogic1, kSource);
        const auto unsigned_ge = model.add_binary(BinaryOp::GreaterEqual, unsigned_negative,
                                                  unsigned_positive, kLogic4, kLogic1, kSource);
        const auto shift = model.add_binary(BinaryOp::ArithmeticShiftRight, negative, one,
                                            kSignedLogic4, kLogic4, kSource);
        const auto narrowed_shift = model.add_binary(BinaryOp::ArithmeticShiftRight, negative,
                                                     one, kSignedLogic4, kLogic2, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(signed_result, kLogic1, kSource), signed_ge, kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(unsigned_result, kLogic1, kSource), unsigned_ge,
            kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(arithmetic_shift, kLogic4, kSource), shift, kSource));
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(truncated_shift, kLogic2, kSource), narrowed_shift,
            kSource));

        SignalStore store(model);
        A1_EXPECT(settles(model, store));
        A1_EXPECT(store.value(signed_result).to_binary() == "0");
        A1_EXPECT(store.value(unsigned_result).to_binary() == "1");
        A1_EXPECT(store.value(arithmetic_shift).to_binary() == "1100");
        A1_EXPECT(store.value(truncated_shift).to_binary() == "00");
    }

    return EXIT_SUCCESS;
}
