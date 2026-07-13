#include "runtime/continuous_evaluator.h"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace a1::runtime {
namespace {

StateDomain state_domain(const ir::PackedType& type) {
    return type.is_four_state ? StateDomain::FourState : StateDomain::TwoState;
}

LogicValue coerce_to_type(const LogicValue& value, const ir::PackedType& type) {
    return value.coerce(type.width, type.is_signed, state_domain(type));
}

bool is_all_z(const LogicValue& value) {
    for (std::uint32_t bit = 0; bit < value.width(); ++bit) {
        if (value.bit(bit) != LogicValue::Bit::Z) return false;
    }
    return true;
}

ir::SignalId lvalue_base(const ir::LValue& lvalue) {
    return std::visit(
        [](const auto& target) { return target.signal; }, lvalue.payload);
}

std::uint32_t lvalue_bit_offset(const ir::LValue& lvalue) {
    return std::visit(
        [](const auto& target) -> std::uint32_t {
            using Target = std::decay_t<decltype(target)>;
            if constexpr (std::is_same_v<Target, ir::WholeSignalLValue>) {
                return 0;
            } else {
                return target.bit_offset;
            }
        },
        lvalue.payload);
}

}  // namespace

SignalStore::SignalStore(const ir::ModelIR& model) : signals_(model.signals()) {
    values_.reserve(signals_.size());
    external_drivers_.resize(signals_.size());
    for (const auto& signal : signals_) {
        if (signal.kind == ir::SignalKind::Variable) {
            values_.push_back(signal.type.is_four_state ? LogicValue::x(signal.type.width)
                                                        : LogicValue::zeros(signal.type.width));
        } else {
            values_.push_back(signal.type.is_four_state ? LogicValue::z(signal.type.width)
                                                        : LogicValue::zeros(signal.type.width));
        }
    }

    assignment_drivers_.reserve(model.continuous_assigns().size());
    for (const auto& assignment : model.continuous_assigns()) {
        const auto payload_width = assignment.target.value < model.lvalues().size()
                                       ? model.lvalues()[assignment.target.value].type.width
                                       : 0;
        assignment_drivers_.push_back({assignment.target, LogicValue::z(payload_width)});
    }
}

void SignalStore::set_variable(ir::SignalId signal, LogicValue value) {
    const auto& destination = signals_.at(signal.value);
    if (destination.kind != ir::SignalKind::Variable) {
        throw std::invalid_argument("set_variable requires a variable signal");
    }
    values_.at(signal.value) = coerce_to_type(value, destination.type);
}

void SignalStore::set_external_driver(ir::SignalId signal, LogicValue value) {
    const auto& destination = signals_.at(signal.value);
    if (destination.kind != ir::SignalKind::Net) {
        throw std::invalid_argument("set_external_driver requires a net signal");
    }

    auto& external = external_drivers_.at(signal.value);
    if (is_all_z(value)) {
        external.reset();
    } else {
        external = coerce_to_type(value, destination.type);
    }
}

const LogicValue& SignalStore::value(ir::SignalId signal) const {
    return values_.at(signal.value);
}

std::optional<std::string> ContinuousEvaluator::settle(const ir::ModelIR& model,
                                                        SignalStore& store) {
    const auto diagnostics = model.validate();
    if (!diagnostics.empty()) {
        return "invalid model: " + diagnostics.front();
    }

    const auto order = model.continuous_order();
    if (!order.has_value()) {
        return "cyclic continuous assignments unsupported by phase-1 evaluator";
    }

    const auto resolve_target = [&](ir::SignalId signal) {
        const auto& base = model.signals().at(signal.value);
        std::vector<LogicValue> drivers;
        for (const auto& slot : store.assignment_drivers_) {
            const auto& target = model.lvalues().at(slot.target.value);
            if (lvalue_base(target) != signal) continue;
            drivers.push_back(LogicValue::z(base.type.width)
                                  .with_slice(lvalue_bit_offset(target), slot.payload));
        }
        if (const auto& external = store.external_drivers_.at(signal.value);
            external.has_value()) {
            drivers.push_back(*external);
        }
        const auto resolved = drivers.empty() ? LogicValue::z(base.type.width)
                                              : resolve_net(drivers);
        store.values_.at(signal.value) = coerce_to_type(resolved, base.type);
    };

    for (const auto assignment_id : *order) {
        const auto& assignment = model.continuous_assigns().at(assignment_id.value);
        const auto& target = model.lvalues().at(assignment.target.value);
        store.assignment_drivers_.at(assignment_id.value).payload =
            coerce_to_type(evaluate(model, store, assignment.value), target.type);
        resolve_target(lvalue_base(target));
    }
    for (std::uint32_t signal = 0; signal < model.signals().size(); ++signal) {
        if (model.signals()[signal].kind == ir::SignalKind::Net) {
            resolve_target({signal});
        }
    }
    return std::nullopt;
}

LogicValue ContinuousEvaluator::evaluate(const ir::ModelIR& model, const SignalStore& store,
                                         ir::ExprId expression_id) {
    const auto& expression = model.expressions().at(expression_id.value);
    const auto result = std::visit(
        [&](const auto& payload) -> LogicValue {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, ir::ConstantExpr>) {
                return payload.value;
            } else if constexpr (std::is_same_v<Payload, ir::SignalRefExpr>) {
                return store.value(payload.signal);
            } else if constexpr (std::is_same_v<Payload, ir::UnaryExpr>) {
                const auto operand = evaluate(model, store, payload.operand);
                switch (payload.op) {
                    case ir::UnaryOp::BitwiseNot: return ~operand;
                    case ir::UnaryOp::LogicalNot: return !operand;
                    case ir::UnaryOp::ReduceAnd: return operand.reduce_and();
                    case ir::UnaryOp::ReduceOr: return operand.reduce_or();
                    case ir::UnaryOp::ReduceXor: return operand.reduce_xor();
                }
                throw std::logic_error("unsupported unary operation");
            } else if constexpr (std::is_same_v<Payload, ir::BinaryExpr>) {
                const auto lhs = coerce_to_type(evaluate(model, store, payload.lhs),
                                                payload.operation_type);
                const auto rhs_value = evaluate(model, store, payload.rhs);
                const auto rhs = payload.op == ir::BinaryOp::ShiftLeft ||
                                         payload.op == ir::BinaryOp::LogicalShiftRight ||
                                         payload.op == ir::BinaryOp::ArithmeticShiftRight
                                     ? rhs_value
                                     : coerce_to_type(rhs_value, payload.operation_type);
                switch (payload.op) {
                    case ir::BinaryOp::Add:
                        return LogicValue::add(lhs, rhs, payload.operation_type.width,
                                               payload.operation_type.is_signed);
                    case ir::BinaryOp::Subtract:
                        return LogicValue::subtract(lhs, rhs, payload.operation_type.width,
                                                    payload.operation_type.is_signed);
                    case ir::BinaryOp::BitwiseAnd: return lhs & rhs;
                    case ir::BinaryOp::BitwiseOr: return lhs | rhs;
                    case ir::BinaryOp::BitwiseXor: return lhs ^ rhs;
                    case ir::BinaryOp::LogicalEqual: return LogicValue::logical_equal(lhs, rhs);
                    case ir::BinaryOp::LogicalNotEqual:
                        return !LogicValue::logical_equal(lhs, rhs);
                    case ir::BinaryOp::CaseEqual: return LogicValue::case_equal(lhs, rhs);
                    case ir::BinaryOp::CaseNotEqual: return !LogicValue::case_equal(lhs, rhs);
                    case ir::BinaryOp::GreaterEqual:
                        return LogicValue::greater_equal(lhs, rhs, payload.operation_type.is_signed);
                    case ir::BinaryOp::ShiftLeft:
                        return lhs.shift_left(rhs, payload.operation_type.width);
                    case ir::BinaryOp::LogicalShiftRight:
                        return lhs.shift_right(rhs, payload.operation_type.width, false);
                    case ir::BinaryOp::ArithmeticShiftRight:
                        return lhs.shift_right(rhs, payload.operation_type.width,
                                               payload.operation_type.is_signed);
                }
                throw std::logic_error("unsupported binary operation");
            } else if constexpr (std::is_same_v<Payload, ir::ConditionalExpr>) {
                return LogicValue::conditional(evaluate(model, store, payload.condition),
                                               evaluate(model, store, payload.when_true),
                                               evaluate(model, store, payload.when_false));
            } else if constexpr (std::is_same_v<Payload, ir::ConcatExpr>) {
                if (payload.operands.empty()) return LogicValue::zeros(0);
                auto result = evaluate(model, store, payload.operands.front());
                for (std::size_t index = 1; index < payload.operands.size(); ++index) {
                    result = LogicValue::concat({result,
                                                 evaluate(model, store, payload.operands[index])});
                }
                return result;
            } else if constexpr (std::is_same_v<Payload, ir::ReplicateExpr>) {
                return evaluate(model, store, payload.operand).replicate(payload.count);
            } else if constexpr (std::is_same_v<Payload, ir::BitSelectExpr>) {
                return evaluate(model, store, payload.value)
                    .slice(payload.bit_offset, payload.bit_offset);
            } else if constexpr (std::is_same_v<Payload, ir::RangeSelectExpr>) {
                return evaluate(model, store, payload.value)
                    .slice(payload.bit_offset + payload.width - 1, payload.bit_offset);
            } else if constexpr (std::is_same_v<Payload, ir::CastExpr>) {
                return evaluate(model, store, payload.value);
            }
        },
        expression.payload);
    return coerce_to_type(result, expression.type);
}

}  // namespace a1::runtime
