#include "ir/model_ir.h"

#include <algorithm>
#include <set>
#include <type_traits>
#include <utility>

namespace a1::ir {
namespace {

template <typename Id>
bool is_valid(Id id, std::size_t size) {
    return id.value < size;
}

SignalId lvalue_base(const LValue& lvalue) {
    return std::visit([](const auto& select) { return select.signal; }, lvalue.payload);
}

bool in_bounds(std::uint32_t offset, std::uint32_t width, std::uint32_t base_width) {
    return offset <= base_width && width <= base_width - offset;
}

template <typename Id>
void append_invalid_id_diagnostic(std::vector<std::string>& diagnostics, Id id,
                                  std::size_t size, const char* description) {
    if (!is_valid(id, size)) {
        diagnostics.emplace_back(description);
    }
}

}  // namespace

SignalId ModelIR::add_signal(std::string canonical_path, PackedType type, SignalKind kind,
                             SourceSpan source) {
    const auto id = SignalId{static_cast<std::uint32_t>(signals_.size())};
    signals_.push_back({std::move(canonical_path), type, kind, std::move(source)});
    return id;
}

ExprId ModelIR::add_constant(runtime::LogicValue value, PackedType type, SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({ConstantExpr{std::move(value)}, type, std::move(source)});
    return id;
}

ExprId ModelIR::add_signal_ref(SignalId signal, PackedType type, SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({SignalRefExpr{signal}, type, std::move(source)});
    return id;
}

ExprId ModelIR::add_unary(UnaryOp op, ExprId operand, PackedType type, SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({UnaryExpr{op, operand}, type, std::move(source)});
    return id;
}

ExprId ModelIR::add_binary(BinaryOp op, ExprId lhs, ExprId rhs, PackedType operation_type,
                           PackedType type, SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({BinaryExpr{op, lhs, rhs, operation_type}, type, std::move(source)});
    return id;
}

ExprId ModelIR::add_conditional(ExprId condition, ExprId when_true, ExprId when_false,
                                PackedType type, SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back(
        {ConditionalExpr{condition, when_true, when_false}, type, std::move(source)});
    return id;
}

ExprId ModelIR::add_concat(std::vector<ExprId> operands, PackedType type, SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({ConcatExpr{std::move(operands)}, type, std::move(source)});
    return id;
}

ExprId ModelIR::add_replicate(ExprId operand, std::uint32_t count, PackedType type,
                              SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({ReplicateExpr{operand, count}, type, std::move(source)});
    return id;
}

ExprId ModelIR::add_bit_select(ExprId value, std::uint32_t bit_offset, PackedType type,
                               SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({BitSelectExpr{value, bit_offset}, type, std::move(source)});
    return id;
}

ExprId ModelIR::add_range_select(ExprId value, std::uint32_t bit_offset, std::uint32_t width,
                                 PackedType type, SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({RangeSelectExpr{value, bit_offset, width}, type,
                            std::move(source)});
    return id;
}

ExprId ModelIR::add_cast(ExprId value, PackedType type, SourceSpan source) {
    const auto id = ExprId{static_cast<std::uint32_t>(expressions_.size())};
    expressions_.push_back({CastExpr{value}, type, std::move(source)});
    return id;
}

LValueId ModelIR::add_whole_signal_lvalue(SignalId signal, PackedType type,
                                          SourceSpan source) {
    const auto id = LValueId{static_cast<std::uint32_t>(lvalues_.size())};
    lvalues_.push_back({WholeSignalLValue{signal}, type, std::move(source)});
    return id;
}

LValueId ModelIR::add_bit_select_lvalue(SignalId signal, std::uint32_t bit_offset,
                                        PackedType type, SourceSpan source) {
    const auto id = LValueId{static_cast<std::uint32_t>(lvalues_.size())};
    lvalues_.push_back({BitSelectLValue{signal, bit_offset}, type, std::move(source)});
    return id;
}

LValueId ModelIR::add_range_select_lvalue(SignalId signal, std::uint32_t bit_offset,
                                          std::uint32_t width, PackedType type,
                                          SourceSpan source) {
    const auto id = LValueId{static_cast<std::uint32_t>(lvalues_.size())};
    lvalues_.push_back({RangeSelectLValue{signal, bit_offset, width}, type,
                        std::move(source)});
    return id;
}

ContinuousAssignId ModelIR::add_continuous_assign(LValueId target, ExprId value,
                                                   SourceSpan source) {
    const auto id = ContinuousAssignId{static_cast<std::uint32_t>(continuous_assigns_.size())};
    continuous_assigns_.push_back({target, value, std::move(source), collect_reads(value)});
    return id;
}

const std::vector<Signal>& ModelIR::signals() const {
    return signals_;
}

const std::vector<Expression>& ModelIR::expressions() const {
    return expressions_;
}

const std::vector<LValue>& ModelIR::lvalues() const {
    return lvalues_;
}

const std::vector<ContinuousAssign>& ModelIR::continuous_assigns() const {
    return continuous_assigns_;
}

std::vector<std::string> ModelIR::validate() const {
    std::vector<std::string> diagnostics;

    for (const auto& signal : signals_) {
        if (signal.type.width == 0) {
            diagnostics.emplace_back("zero width signal");
        }
    }
    for (const auto& expression : expressions_) {
        if (expression.type.width == 0) {
            diagnostics.emplace_back("zero width expression");
        }
        if (const auto* binary = std::get_if<BinaryExpr>(&expression.payload);
            binary != nullptr && binary->operation_type.width == 0) {
            diagnostics.emplace_back("zero width binary operation type");
        }
    }
    for (const auto& lvalue : lvalues_) {
        if (lvalue.type.width == 0) {
            diagnostics.emplace_back("zero width lvalue");
        }
    }

    std::set<std::string> paths;
    for (const auto& signal : signals_) {
        if (!paths.insert(signal.canonical_path).second) {
            diagnostics.emplace_back("duplicate signal path");
        }
    }

    const auto check_child = [&](ExprId child) {
        append_invalid_id_diagnostic(diagnostics, child, expressions_.size(),
                                     "expression child is out of range");
    };
    for (const auto& expression : expressions_) {
        std::visit(
            [&](const auto& payload) {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<Payload, UnaryExpr>) {
                    check_child(payload.operand);
                } else if constexpr (std::is_same_v<Payload, BinaryExpr>) {
                    check_child(payload.lhs);
                    check_child(payload.rhs);
                } else if constexpr (std::is_same_v<Payload, ConditionalExpr>) {
                    check_child(payload.condition);
                    check_child(payload.when_true);
                    check_child(payload.when_false);
                } else if constexpr (std::is_same_v<Payload, ConcatExpr>) {
                    for (const auto child : payload.operands) check_child(child);
                } else if constexpr (std::is_same_v<Payload, ReplicateExpr>) {
                    check_child(payload.operand);
                } else if constexpr (std::is_same_v<Payload, BitSelectExpr> ||
                                     std::is_same_v<Payload, RangeSelectExpr> ||
                                     std::is_same_v<Payload, CastExpr>) {
                    check_child(payload.value);
                }
            },
            expression.payload);
    }

    for (const auto& expression : expressions_) {
        if (const auto* ref = std::get_if<SignalRefExpr>(&expression.payload);
            ref != nullptr) {
            append_invalid_id_diagnostic(diagnostics, ref->signal, signals_.size(),
                                         "signal reference is out of range");
        }
    }

    for (const auto& lvalue : lvalues_) {
        const auto base = lvalue_base(lvalue);
        if (!is_valid(base, signals_.size())) {
            diagnostics.emplace_back("lvalue base is out of range");
            continue;
        }

        const auto& signal = signals_[base.value];
        std::visit(
            [&](const auto& select) {
                using Select = std::decay_t<decltype(select)>;
                if constexpr (std::is_same_v<Select, WholeSignalLValue>) {
                    if (lvalue.type != signal.type) {
                        diagnostics.emplace_back("lvalue type does not match base signal");
                    }
                } else if constexpr (std::is_same_v<Select, BitSelectLValue>) {
                    if (!in_bounds(select.bit_offset, 1, signal.type.width)) {
                        diagnostics.emplace_back("lvalue select is out of range");
                    }
                    if (lvalue.type.width != 1) {
                        diagnostics.emplace_back("lvalue type does not match select width");
                    }
                    if (lvalue.type.is_four_state != signal.type.is_four_state) {
                        diagnostics.emplace_back("lvalue four-state domain does not match base");
                    }
                } else {
                    if (!in_bounds(select.bit_offset, select.width, signal.type.width)) {
                        diagnostics.emplace_back("lvalue select is out of range");
                    }
                    if (lvalue.type.width != select.width) {
                        diagnostics.emplace_back("lvalue type does not match select width");
                    }
                    if (lvalue.type.is_four_state != signal.type.is_four_state) {
                        diagnostics.emplace_back("lvalue four-state domain does not match base");
                    }
                }
            },
            lvalue.payload);
    }

    for (const auto& assignment : continuous_assigns_) {
        append_invalid_id_diagnostic(diagnostics, assignment.target, lvalues_.size(),
                                     "assignment target is out of range");
        append_invalid_id_diagnostic(diagnostics, assignment.value, expressions_.size(),
                                     "assignment value is out of range");
    }

    for (const auto& lvalue : lvalues_) {
        const auto base = lvalue_base(lvalue);
        if (is_valid(base, signals_.size()) && signals_[base.value].kind != SignalKind::Net) {
            diagnostics.emplace_back("lvalue base is not a net");
        }
    }

    for (const auto& assignment : continuous_assigns_) {
        if (is_valid(assignment.target, lvalues_.size()) &&
            is_valid(assignment.value, expressions_.size()) &&
            lvalues_[assignment.target.value].type.width !=
                expressions_[assignment.value.value].type.width) {
            diagnostics.emplace_back("target/value width mismatch");
        }
    }

    return diagnostics;
}

std::optional<std::vector<ContinuousAssignId>> ModelIR::continuous_order() const {
    if (!validate().empty()) {
        return std::nullopt;
    }

    const auto count = continuous_assigns_.size();
    std::vector<std::vector<std::uint32_t>> producers(signals_.size());
    for (std::uint32_t index = 0; index < count; ++index) {
        producers[lvalue_base(lvalues_[continuous_assigns_[index].target.value]).value]
            .push_back(index);
    }

    std::vector<std::set<std::uint32_t>> outgoing(count);
    std::vector<std::uint32_t> indegree(count, 0);
    for (std::uint32_t consumer = 0; consumer < count; ++consumer) {
        for (const auto signal : continuous_assigns_[consumer].read_signals) {
            for (const auto producer : producers[signal.value]) {
                if (outgoing[producer].insert(consumer).second) {
                    ++indegree[consumer];
                }
            }
        }
    }

    std::set<std::uint32_t> ready;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (indegree[index] == 0) ready.insert(index);
    }

    std::vector<ContinuousAssignId> result;
    result.reserve(count);
    while (!ready.empty()) {
        const auto current = *ready.begin();
        ready.erase(ready.begin());
        result.push_back({current});
        for (const auto successor : outgoing[current]) {
            if (--indegree[successor] == 0) ready.insert(successor);
        }
    }

    if (result.size() != count) {
        return std::nullopt;
    }
    return result;
}

std::vector<SignalId> ModelIR::collect_reads(ExprId value) const {
    std::vector<SignalId> reads;
    const auto visit = [&](const auto& self, ExprId expression) -> void {
        if (!is_valid(expression, expressions_.size())) return;
        std::visit(
            [&](const auto& payload) {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<Payload, SignalRefExpr>) {
                    reads.push_back(payload.signal);
                } else if constexpr (std::is_same_v<Payload, UnaryExpr>) {
                    self(self, payload.operand);
                } else if constexpr (std::is_same_v<Payload, BinaryExpr>) {
                    self(self, payload.lhs);
                    self(self, payload.rhs);
                } else if constexpr (std::is_same_v<Payload, ConditionalExpr>) {
                    self(self, payload.condition);
                    self(self, payload.when_true);
                    self(self, payload.when_false);
                } else if constexpr (std::is_same_v<Payload, ConcatExpr>) {
                    for (const auto operand : payload.operands) self(self, operand);
                } else if constexpr (std::is_same_v<Payload, ReplicateExpr>) {
                    self(self, payload.operand);
                } else if constexpr (std::is_same_v<Payload, BitSelectExpr> ||
                                     std::is_same_v<Payload, RangeSelectExpr> ||
                                     std::is_same_v<Payload, CastExpr>) {
                    self(self, payload.value);
                }
            },
            expressions_[expression.value].payload);
    };

    visit(visit, value);
    std::sort(reads.begin(), reads.end(), [](SignalId lhs, SignalId rhs) {
        return lhs.value < rhs.value;
    });
    reads.erase(std::unique(reads.begin(), reads.end()), reads.end());
    return reads;
}

}  // namespace a1::ir
