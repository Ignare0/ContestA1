#include "frontend/frontend.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/Expression.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Statement.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/NetType.h"
#include "slang/driver/Driver.h"
#include "slang/numeric/ConstantValue.h"
#include "slang/syntax/SyntaxTree.h"

namespace a1::frontend {
namespace {

using slang::ast::ArgumentDirection;
using slang::ast::AssignmentExpression;
using slang::ast::BinaryExpression;
using slang::ast::BinaryOperator;
using slang::ast::BlockStatement;
using slang::ast::CallExpression;
using slang::ast::Compilation;
using slang::ast::ConditionalExpression;
using slang::ast::ConditionalStatement;
using slang::ast::ContinuousAssignSymbol;
using slang::ast::ConversionExpression;
using slang::ast::DelayControl;
using slang::ast::EdgeKind;
using slang::ast::ElementSelectExpression;
using slang::ast::EventListControl;
using slang::ast::Expression;
using slang::ast::ExpressionKind;
using slang::ast::ExpressionStatement;
using slang::ast::GenerateBlockArraySymbol;
using slang::ast::GenerateBlockSymbol;
using slang::ast::InstanceBodySymbol;
using slang::ast::InstanceSymbol;
using slang::ast::IntegerLiteral;
using slang::ast::NamedValueExpression;
using slang::ast::NetSymbol;
using slang::ast::NetType;
using slang::ast::ParameterSymbol;
using slang::ast::PortSymbol;
using slang::ast::ProceduralBlockKind;
using slang::ast::ProceduralBlockSymbol;
using slang::ast::RangeSelectExpression;
using slang::ast::RangeSelectionKind;
using slang::ast::ReplicationExpression;
using slang::ast::Scope;
using slang::ast::SignalEventControl;
using slang::ast::Statement;
using slang::ast::StatementBlockKind;
using slang::ast::StatementKind;
using slang::ast::StatementList;
using slang::ast::Symbol;
using slang::ast::SymbolKind;
using slang::ast::TimedStatement;
using slang::ast::TimingControlKind;
using slang::ast::Type;
using slang::ast::UniquePriorityCheck;
using slang::ast::UnaryExpression;
using slang::ast::UnaryOperator;
using slang::ast::ValueSymbol;
using slang::ast::VariableSymbol;

struct LoweringContext {
    explicit LoweringContext(slang::driver::Driver& driver) : driver(driver) {}

    slang::driver::Driver& driver;
    ir::ModelIR model;
    std::unordered_map<const ValueSymbol*, ir::SignalId> signals;
    std::vector<const ContinuousAssignSymbol*> assignments;
    std::vector<const NetSymbol*> initialized_nets;
    std::vector<const ProceduralBlockSymbol*> procedural_blocks;
    std::optional<Diagnostic> failure;

    [[nodiscard]] ir::SourceSpan source(slang::SourceRange range) const {
        auto location = driver.sourceManager.getFullyExpandedLoc(range.start());
        if (location == slang::SourceLocation::NoLocation)
            return {};
        return {std::string(driver.sourceManager.getFileName(location)),
                static_cast<std::uint32_t>(driver.sourceManager.getLineNumber(location)),
                static_cast<std::uint32_t>(driver.sourceManager.getColumnNumber(location))};
    }

    [[nodiscard]] ir::SourceSpan source(const Symbol& symbol) const {
        return source(slang::SourceRange(symbol.location, symbol.location));
    }

    void fail(std::string message, slang::SourceRange range) {
        if (!failure)
            failure = Diagnostic{std::move(message), source(range)};
    }

    void fail(std::string message, const Symbol& symbol) {
        if (!failure)
            failure = Diagnostic{std::move(message), source(symbol)};
    }

    [[nodiscard]] std::optional<ir::PackedType> packed_type(const Type& type,
                                                              slang::SourceRange range) {
        if (!type.isIntegral() || type.isAggregate() || type.getBitWidth() == 0) {
            fail("unsupported non-packed integral type", range);
            return std::nullopt;
        }
        if (type.getBitWidth() > std::numeric_limits<std::uint32_t>::max()) {
            fail("unsupported packed type width", range);
            return std::nullopt;
        }
        return ir::PackedType{static_cast<std::uint32_t>(type.getBitWidth()), type.isSigned(),
                              type.isFourState()};
    }

    [[nodiscard]] std::optional<ir::PackedType> packed_type(const Type& type,
                                                              const Symbol& symbol) {
        return packed_type(type, slang::SourceRange(symbol.location, symbol.location));
    }

    [[nodiscard]] std::optional<ir::SignalId> signal_id(const ValueSymbol& symbol) const {
        const auto found = signals.find(&symbol);
        if (found == signals.end())
            return std::nullopt;
        return found->second;
    }

    [[nodiscard]] bool normal_net(const NetSymbol& net) {
        const auto kind = net.netType.netKind;
        if (kind != NetType::Wire && kind != NetType::Tri) {
            fail("unsupported special net", net);
            return false;
        }
        if (net.netType.getResolutionFunction() != nullptr) {
            fail("unsupported net resolution", net);
            return false;
        }
        if (net.getDelay() != nullptr || net.getChargeStrength().has_value()) {
            fail("unsupported net delay or strength", net);
            return false;
        }
        const auto [drive0, drive1] = net.getDriveStrength();
        if (drive0.has_value() || drive1.has_value()) {
            fail("unsupported net delay or strength", net);
            return false;
        }
        return true;
    }

    [[nodiscard]] std::optional<ir::SignalId> add_signal(const ValueSymbol& value) {
        if (const auto found = signals.find(&value); found != signals.end())
            return found->second;

        const auto type = packed_type(value.getType(), value);
        if (!type)
            return std::nullopt;

        ir::SignalKind kind;
        if (value.kind == SymbolKind::Net) {
            const auto& net = value.as<NetSymbol>();
            if (!normal_net(net))
                return std::nullopt;
            kind = ir::SignalKind::Net;
        } else if (value.kind == SymbolKind::Variable) {
            const auto& variable = value.as<VariableSymbol>();
            if (variable.getInitializer() != nullptr) {
                fail("unsupported variable initializer", variable);
                return std::nullopt;
            }
            kind = ir::SignalKind::Variable;
        } else {
            fail("unsupported value symbol", value);
            return std::nullopt;
        }

        const auto id = model.add_signal(value.getHierarchicalPath(), *type, kind, source(value));
        signals.emplace(&value, id);
        return id;
    }

    // 连续赋值目标必须是 Net；过程赋值目标必须是 Variable（Net/memory → unsupported）。
    void fail_lvalue(bool procedural, slang::SourceRange range) {
        if (procedural)
            fail_unsupported("procedural assignment target", range);
        else
            fail("unsupported continuous-assignment lvalue", range);
    }

    [[nodiscard]] std::optional<ir::SignalId> lvalue_signal(const Expression& expression,
                                                             bool procedural) {
        const auto expected = procedural ? SymbolKind::Variable : SymbolKind::Net;
        const auto* named = expression.as_if<NamedValueExpression>();
        if (named == nullptr || named->symbol.kind != expected) {
            fail_lvalue(procedural, expression.sourceRange);
            return std::nullopt;
        }
        const auto id = signal_id(named->symbol);
        if (!id) {
            fail_lvalue(procedural, expression.sourceRange);
            return std::nullopt;
        }
        return *id;
    }

    [[nodiscard]] std::optional<std::int32_t> constant_index(const Expression& expression) {
        const slang::ConstantValue* constant = expression.getConstant();
        if (constant == nullptr && expression.kind == ExpressionKind::IntegerLiteral)
            return expression.as<IntegerLiteral>().getValue().as<std::int32_t>();
        if (constant == nullptr || !constant->isInteger())
            return std::nullopt;
        return constant->integer().as<std::int32_t>();
    }

    [[nodiscard]] std::optional<std::uint32_t> physical_index(const Type& type,
                                                                std::int32_t index,
                                                                slang::SourceRange range) {
        if (!type.isSimpleBitVector()) {
            fail("unsupported selected type", range);
            return std::nullopt;
        }
        const auto declared = type.getFixedRange();
        if (!declared.containsPoint(index)) {
            fail("constant select is out of range", range);
            return std::nullopt;
        }
        const auto offset = declared.translateIndex(index);
        if (offset < 0) {
            fail("constant select is out of range", range);
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(offset);
    }

    [[nodiscard]] std::optional<runtime::LogicValue> logic_value(const slang::SVInt& value,
                                                                  const ir::PackedType& type) {
        const auto resized = value.extend(type.width, type.is_signed);
        std::string bits;
        bits.reserve(type.width);
        for (std::uint32_t index = type.width; index > 0; --index) {
            const auto bit = resized[static_cast<std::int32_t>(index - 1)];
            if (bit.value == slang::logic_t::x.value)
                bits.push_back('x');
            else if (bit.value == slang::logic_t::z.value)
                bits.push_back('z');
            else
                bits.push_back(bit.value == 0 ? '0' : '1');
        }
        const auto result = runtime::LogicValue::from_binary(bits);
        return result.coerce(type.width, type.is_signed,
                             type.is_four_state ? runtime::StateDomain::FourState
                                                : runtime::StateDomain::TwoState);
    }

    [[nodiscard]] std::optional<ir::ExprId> lower_constant(const slang::SVInt& value,
                                                            const Type& type,
                                                            slang::SourceRange range) {
        const auto packed = packed_type(type, range);
        if (!packed)
            return std::nullopt;
        const auto logic = logic_value(value, *packed);
        if (!logic)
            return std::nullopt;
        return model.add_constant(*logic, *packed, source(range));
    }

    [[nodiscard]] std::optional<ir::LValueId> lower_lvalue(const Expression& expression,
                                                            bool procedural = false) {
        const auto target_type = packed_type(*expression.type, expression.sourceRange);
        if (!target_type)
            return std::nullopt;

        if (const auto* named = expression.as_if<NamedValueExpression>()) {
            const auto expected = procedural ? SymbolKind::Variable : SymbolKind::Net;
            const auto id = signal_id(named->symbol);
            if (!id || named->symbol.kind != expected) {
                fail_lvalue(procedural, expression.sourceRange);
                return std::nullopt;
            }
            return model.add_whole_signal_lvalue(*id, *target_type, source(expression.sourceRange));
        }

        if (const auto* select = expression.as_if<ElementSelectExpression>()) {
            const auto id = lvalue_signal(select->value(), procedural);
            const auto index = constant_index(select->selector());
            if (!id || !index)
                goto invalid_lvalue;
            const auto offset = physical_index(*select->value().type, *index,
                                               select->selector().sourceRange);
            if (!offset)
                return std::nullopt;
            return model.add_bit_select_lvalue(*id, *offset, *target_type,
                                               source(expression.sourceRange));
        }

        if (const auto* select = expression.as_if<RangeSelectExpression>()) {
            if (select->getSelectionKind() != RangeSelectionKind::Simple) {
                fail("unsupported dynamic range select", expression.sourceRange);
                return std::nullopt;
            }
            const auto id = lvalue_signal(select->value(), procedural);
            const auto left = constant_index(select->left());
            const auto right = constant_index(select->right());
            if (!id || !left || !right)
                goto invalid_lvalue;
            const auto& value_type = *select->value().type;
            if (!value_type.isSimpleBitVector()) {
                fail("unsupported selected type", expression.sourceRange);
                return std::nullopt;
            }
            const auto declared = value_type.getFixedRange();
            if (!declared.containsPoint(*left) || !declared.containsPoint(*right)) {
                fail("constant select is out of range", expression.sourceRange);
                return std::nullopt;
            }
            const auto left_offset = declared.translateIndex(*left);
            const auto right_offset = declared.translateIndex(*right);
            const auto width = static_cast<std::uint32_t>(
                std::abs(static_cast<std::int64_t>(*left) - static_cast<std::int64_t>(*right)) +
                1);
            return model.add_range_select_lvalue(*id,
                                                 static_cast<std::uint32_t>(
                                                     std::min(left_offset, right_offset)),
                                                 width, *target_type,
                                                 source(expression.sourceRange));
        }

        if (const auto* conversion = expression.as_if<ConversionExpression>())
            return lower_lvalue(conversion->operand(), procedural);

    invalid_lvalue:
        fail_lvalue(procedural, expression.sourceRange);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ir::UnaryOp> unary_op(UnaryOperator op,
                                                       slang::SourceRange range) {
        switch (op) {
            case UnaryOperator::BitwiseNot: return ir::UnaryOp::BitwiseNot;
            case UnaryOperator::LogicalNot: return ir::UnaryOp::LogicalNot;
            case UnaryOperator::BitwiseAnd: return ir::UnaryOp::ReduceAnd;
            case UnaryOperator::BitwiseOr: return ir::UnaryOp::ReduceOr;
            case UnaryOperator::BitwiseXor: return ir::UnaryOp::ReduceXor;
            default:
                fail("unsupported UnaryOp at " + location_text(range), range);
                return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<ir::BinaryOp> binary_op(BinaryOperator op,
                                                         slang::SourceRange range) {
        switch (op) {
            case BinaryOperator::Add: return ir::BinaryOp::Add;
            case BinaryOperator::Subtract: return ir::BinaryOp::Subtract;
            case BinaryOperator::BinaryAnd: return ir::BinaryOp::BitwiseAnd;
            case BinaryOperator::BinaryOr: return ir::BinaryOp::BitwiseOr;
            case BinaryOperator::BinaryXor: return ir::BinaryOp::BitwiseXor;
            case BinaryOperator::Equality: return ir::BinaryOp::LogicalEqual;
            case BinaryOperator::Inequality: return ir::BinaryOp::LogicalNotEqual;
            case BinaryOperator::CaseEquality: return ir::BinaryOp::CaseEqual;
            case BinaryOperator::CaseInequality: return ir::BinaryOp::CaseNotEqual;
            case BinaryOperator::GreaterThanEqual: return ir::BinaryOp::GreaterEqual;
            case BinaryOperator::LogicalShiftLeft: return ir::BinaryOp::ShiftLeft;
            case BinaryOperator::LogicalShiftRight: return ir::BinaryOp::LogicalShiftRight;
            case BinaryOperator::ArithmeticShiftRight:
                return ir::BinaryOp::ArithmeticShiftRight;
            default:
                fail("unsupported BinaryOp at " + location_text(range), range);
                return std::nullopt;
        }
    }

    [[nodiscard]] std::string location_text(slang::SourceRange range) const {
        const auto span = source(range);
        return span.file + ":" + std::to_string(span.line) + ":" +
               std::to_string(span.column);
    }

    void fail_unsupported(std::string_view kind, slang::SourceRange range) {
        fail(std::string("unsupported ") + std::string(kind) + " at " + location_text(range),
             range);
    }

    [[nodiscard]] std::optional<ir::ExprId> lower_expression(const Expression& expression) {
        const auto type = packed_type(*expression.type, expression.sourceRange);
        if (!type)
            return std::nullopt;
        const auto source_span = source(expression.sourceRange);

        switch (expression.kind) {
            case ExpressionKind::IntegerLiteral:
                return lower_constant(expression.as<IntegerLiteral>().getValue(), *expression.type,
                                      expression.sourceRange);
            case ExpressionKind::NamedValue: {
                const auto& named = expression.as<NamedValueExpression>();
                if (named.symbol.kind == SymbolKind::Parameter)
                    return lower_constant(named.symbol.as<ParameterSymbol>().getValue().integer(),
                                          *expression.type, expression.sourceRange);
                const auto id = signal_id(named.symbol);
                if (!id) {
                    fail("unsupported NamedValue at " + location_text(expression.sourceRange),
                         expression.sourceRange);
                    return std::nullopt;
                }
                return model.add_signal_ref(*id, *type, source_span);
            }
            case ExpressionKind::Conversion: {
                const auto& conversion = expression.as<ConversionExpression>();
                const auto operand = lower_expression(conversion.operand());
                if (!operand)
                    return std::nullopt;
                return model.add_cast(*operand, *type, source_span);
            }
            case ExpressionKind::UnaryOp: {
                const auto& unary = expression.as<UnaryExpression>();
                const auto operand = lower_expression(unary.operand());
                const auto op = unary_op(unary.op, unary.opRange);
                if (!operand || !op)
                    return std::nullopt;
                return model.add_unary(*op, *operand, *type, source_span);
            }
            case ExpressionKind::BinaryOp: {
                const auto& binary = expression.as<BinaryExpression>();
                const auto lhs = lower_expression(binary.left());
                const auto rhs = lower_expression(binary.right());
                const auto op = binary_op(binary.op, binary.opRange);
                if (!lhs || !rhs || !op)
                    return std::nullopt;
                const bool is_shift = *op == ir::BinaryOp::ShiftLeft ||
                                     *op == ir::BinaryOp::LogicalShiftRight ||
                                     *op == ir::BinaryOp::ArithmeticShiftRight;
                const auto left_type =
                    packed_type(*binary.left().type, binary.left().sourceRange);
                if (!left_type)
                    return std::nullopt;
                if (!is_shift) {
                    const auto right_type =
                        packed_type(*binary.right().type, binary.right().sourceRange);
                    if (!right_type)
                        return std::nullopt;
                    if (*left_type != *right_type) {
                        fail_unsupported("BinaryOp", binary.opRange);
                        return std::nullopt;
                    }
                }
                return model.add_binary(*op, *lhs, *rhs, *left_type, *type, source_span);
            }
            case ExpressionKind::ConditionalOp: {
                const auto& conditional = expression.as<ConditionalExpression>();
                if (conditional.conditions.size() != 1 ||
                    conditional.conditions.front().pattern != nullptr) {
                    fail_unsupported("ConditionalOp", expression.sourceRange);
                    return std::nullopt;
                }
                const auto condition = lower_expression(*conditional.conditions.front().expr);
                const auto when_true = lower_expression(conditional.left());
                const auto when_false = lower_expression(conditional.right());
                if (!condition || !when_true || !when_false)
                    return std::nullopt;
                return model.add_conditional(*condition, *when_true, *when_false, *type,
                                             source_span);
            }
            case ExpressionKind::Concatenation: {
                const auto& concat = expression.as<slang::ast::ConcatenationExpression>();
                std::vector<ir::ExprId> operands;
                operands.reserve(concat.operands().size());
                for (const auto* operand : concat.operands()) {
                    const auto lowered = lower_expression(*operand);
                    if (!lowered)
                        return std::nullopt;
                    operands.push_back(*lowered);
                }
                return model.add_concat(std::move(operands), *type, source_span);
            }
            case ExpressionKind::Replication: {
                const auto& replication = expression.as<ReplicationExpression>();
                const auto count = constant_index(replication.count());
                if (!count || *count < 0 || replication.count().type->isSigned()) {
                    fail("unsupported replication count", replication.count().sourceRange);
                    return std::nullopt;
                }
                const auto operand = lower_expression(replication.concat());
                if (!operand)
                    return std::nullopt;
                return model.add_replicate(*operand, static_cast<std::uint32_t>(*count), *type,
                                           source_span);
            }
            case ExpressionKind::ElementSelect: {
                const auto& select = expression.as<ElementSelectExpression>();
                const auto value = lower_expression(select.value());
                const auto index = constant_index(select.selector());
                if (!value || !index)
                    goto invalid_expression;
                const auto offset = physical_index(*select.value().type, *index,
                                                   select.selector().sourceRange);
                if (!offset)
                    return std::nullopt;
                return model.add_bit_select(*value, *offset, *type, source_span);
            }
            case ExpressionKind::RangeSelect: {
                const auto& select = expression.as<RangeSelectExpression>();
                if (select.getSelectionKind() != RangeSelectionKind::Simple) {
                    fail("unsupported dynamic range select", expression.sourceRange);
                    return std::nullopt;
                }
                const auto value = lower_expression(select.value());
                const auto left = constant_index(select.left());
                const auto right = constant_index(select.right());
                if (!value || !left || !right)
                    goto invalid_expression;
                const auto declared = select.value().type->getFixedRange();
                if (!declared.containsPoint(*left) || !declared.containsPoint(*right)) {
                    fail("constant select is out of range", expression.sourceRange);
                    return std::nullopt;
                }
                const auto left_offset = declared.translateIndex(*left);
                const auto right_offset = declared.translateIndex(*right);
                const auto width = static_cast<std::uint32_t>(
                    std::abs(static_cast<std::int64_t>(*left) - static_cast<std::int64_t>(*right)) +
                    1);
                return model.add_range_select(*value,
                                              static_cast<std::uint32_t>(
                                                  std::min(left_offset, right_offset)),
                                              width, *type, source_span);
            }
            case ExpressionKind::Assignment:
                fail_unsupported("Assignment", expression.sourceRange);
                return std::nullopt;
            default:
                fail_unsupported(slang::ast::toString(expression.kind), expression.sourceRange);
                return std::nullopt;
        }

    invalid_expression:
        fail_unsupported(slang::ast::toString(expression.kind), expression.sourceRange);
        return std::nullopt;
    }

    [[nodiscard]] bool lower_assignment(const ContinuousAssignSymbol& symbol) {
        if (symbol.getDelay() != nullptr) {
            fail("unsupported continuous-assignment delay", symbol);
            return false;
        }
        const auto [drive0, drive1] = symbol.getDriveStrength();
        if (drive0.has_value() || drive1.has_value()) {
            fail("unsupported continuous-assignment strength", symbol);
            return false;
        }
        const auto& expression = symbol.getAssignment();
        const auto* assignment = expression.as_if<AssignmentExpression>();
        if (assignment == nullptr || assignment->isCompound() || assignment->timingControl != nullptr ||
            assignment->isNonBlocking()) {
            fail("unsupported continuous assignment", expression.sourceRange);
            return false;
        }
        const auto target = lower_lvalue(assignment->left());
        const auto value = lower_expression(assignment->right());
        if (!target || !value)
            return false;
        static_cast<void>(model.add_continuous_assign(*target, *value, source(symbol)));
        return true;
    }

    [[nodiscard]] std::optional<std::uint64_t> constant_delay(const Expression& expression) {
        const slang::ConstantValue* constant = expression.getConstant();
        std::optional<slang::SVInt> value;
        if (constant != nullptr && constant->isInteger())
            value = constant->integer();
        else if (expression.kind == ExpressionKind::IntegerLiteral)
            value = expression.as<IntegerLiteral>().getValue();
        if (!value || value->hasUnknown())
            return std::nullopt;
        return value->as<std::uint64_t>();
    }

    [[nodiscard]] std::optional<ir::StmtId> lower_expression_statement(
        const ExpressionStatement& statement) {
        const auto span = source(statement.sourceRange);
        const auto& expression = statement.expr;

        if (const auto* assignment = expression.as_if<AssignmentExpression>()) {
            if (assignment->isCompound() || assignment->timingControl != nullptr) {
                fail_unsupported("Assignment", expression.sourceRange);
                return std::nullopt;
            }
            const auto target = lower_lvalue(assignment->left(), /*procedural=*/true);
            const auto value = lower_expression(assignment->right());
            if (!target || !value)
                return std::nullopt;
            if (assignment->isNonBlocking())
                return model.add_nonblocking_assign(*target, *value, span);
            return model.add_blocking_assign(*target, *value, span);
        }

        if (const auto* call = expression.as_if<CallExpression>()) {
            if (call->isSystemCall()) {
                const auto name = call->getSubroutineName();
                if (name == "$finish")
                    return model.add_finish(span);
                // $display/$error 降级为无实参 stub（丢弃实参）。
                if (name == "$display" || name == "$error")
                    return model.add_display_stub(span);
            }
            fail_unsupported("Call", expression.sourceRange);
            return std::nullopt;
        }

        fail_unsupported(slang::ast::toString(expression.kind), expression.sourceRange);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ir::StmtId> lower_statement(const Statement& statement) {
        const auto span = source(statement.sourceRange);
        switch (statement.kind) {
            case StatementKind::Empty:
                return model.add_empty(span);
            case StatementKind::List: {
                std::vector<ir::StmtId> children;
                for (const auto* child : statement.as<StatementList>().list) {
                    const auto lowered = lower_statement(*child);
                    if (!lowered)
                        return std::nullopt;
                    children.push_back(*lowered);
                }
                return model.add_seq_block(std::move(children), span);
            }
            case StatementKind::Block: {
                const auto& block = statement.as<BlockStatement>();
                if (block.blockKind != StatementBlockKind::Sequential) {
                    fail_unsupported("fork-join block", statement.sourceRange);
                    return std::nullopt;
                }
                const auto body = lower_statement(block.body);
                if (!body)
                    return std::nullopt;
                return model.add_seq_block({*body}, span);
            }
            case StatementKind::ExpressionStatement:
                return lower_expression_statement(statement.as<ExpressionStatement>());
            case StatementKind::Timed: {
                const auto& timed = statement.as<TimedStatement>();
                if (timed.timing.kind != TimingControlKind::Delay) {
                    fail_unsupported(slang::ast::toString(timed.timing.kind),
                                     timed.timing.sourceRange);
                    return std::nullopt;
                }
                const auto& delay = timed.timing.as<DelayControl>();
                const auto ticks = constant_delay(delay.expr);
                if (!ticks) {
                    fail_unsupported("non-constant delay", delay.expr.sourceRange);
                    return std::nullopt;
                }
                const auto next = lower_statement(timed.stmt);
                if (!next)
                    return std::nullopt;
                return model.add_delay(*ticks, *next, span);
            }
            case StatementKind::Conditional: {
                const auto& conditional = statement.as<ConditionalStatement>();
                if (conditional.conditions.size() != 1 ||
                    conditional.conditions.front().pattern != nullptr ||
                    conditional.check != UniquePriorityCheck::None) {
                    fail_unsupported("Conditional", statement.sourceRange);
                    return std::nullopt;
                }
                const auto condition = lower_expression(*conditional.conditions.front().expr);
                const auto then_stmt = lower_statement(conditional.ifTrue);
                if (!condition || !then_stmt)
                    return std::nullopt;
                std::optional<ir::StmtId> else_stmt;
                if (conditional.ifFalse != nullptr) {
                    else_stmt = lower_statement(*conditional.ifFalse);
                    if (!else_stmt)
                        return std::nullopt;
                }
                return model.add_if(*condition, *then_stmt, else_stmt, span);
            }
            default:
                fail_unsupported(slang::ast::toString(statement.kind), statement.sourceRange);
                return std::nullopt;
        }
    }

    [[nodiscard]] bool lower_signal_event(const SignalEventControl& event,
                                          std::vector<ir::TimingSense>& sensitivity) {
        if (event.iffCondition != nullptr) {
            fail_unsupported("event iff condition", event.sourceRange);
            return false;
        }
        ir::EdgeSense sense;
        switch (event.edge) {
            case EdgeKind::None: sense = ir::EdgeSense::AnyChange; break;
            case EdgeKind::PosEdge: sense = ir::EdgeSense::Posedge; break;
            case EdgeKind::NegEdge: sense = ir::EdgeSense::Negedge; break;
            default:
                // SV `edge` / BothEdges 不支持。
                fail_unsupported("edge sensitivity", event.sourceRange);
                return false;
        }
        const Expression* expression = &event.expr;
        while (const auto* conversion = expression->as_if<ConversionExpression>())
            expression = &conversion->operand();
        const auto* named = expression->as_if<NamedValueExpression>();
        std::optional<ir::SignalId> id;
        if (named != nullptr)
            id = signal_id(named->symbol);
        if (!id) {
            fail_unsupported("event expression", event.expr.sourceRange);
            return false;
        }
        sensitivity.push_back({sense, *id});
        return true;
    }

    // @* read_signals（IEEE 1364-2005 §9.7.5）：收集 RHS 与 if 条件中的读；
    // 赋值目标基信号不因作为目标而进入列表。Phase 1 的 LValue 位/段选偏移在
    // lowering 期即折叠为常量，动态下标 lvalue 已 fail-closed，故无目标下标读可收集。
    [[nodiscard]] std::vector<ir::SignalId> collect_statement_reads(ir::StmtId body) const {
        std::vector<ir::SignalId> reads;
        std::unordered_set<std::uint32_t> seen;
        const auto add_reads = [&](ir::ExprId value) {
            for (const auto signal : model.collect_reads(value)) {
                if (seen.insert(signal.value).second)
                    reads.push_back(signal);
            }
        };
        std::vector<ir::StmtId> pending{body};
        std::unordered_set<std::uint32_t> visited;
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            if (!visited.insert(id.value).second)
                continue;
            std::visit(
                [&](const auto& payload) {
                    using Payload = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<Payload, ir::BlockingAssignStmt> ||
                                  std::is_same_v<Payload, ir::NonBlockingAssignStmt>) {
                        add_reads(payload.value);
                    } else if constexpr (std::is_same_v<Payload, ir::SeqBlockStmt>) {
                        for (const auto child : payload.statements)
                            pending.push_back(child);
                    } else if constexpr (std::is_same_v<Payload, ir::DelayStmt>) {
                        pending.push_back(payload.next);
                    } else if constexpr (std::is_same_v<Payload, ir::IfStmt>) {
                        add_reads(payload.condition);
                        pending.push_back(payload.then_stmt);
                        if (payload.else_stmt.has_value())
                            pending.push_back(*payload.else_stmt);
                    }
                },
                model.statements().at(id.value).payload);
        }
        return reads;
    }

    [[nodiscard]] bool lower_procedural_block(const ProceduralBlockSymbol& block) {
        const auto block_range = slang::SourceRange(block.location, block.location);
        switch (block.procedureKind) {
            case ProceduralBlockKind::Initial: {
                const auto body = lower_statement(block.getBody());
                if (!body)
                    return false;
                static_cast<void>(model.add_process(ir::ProcessKind::Initial, {}, {}, *body,
                                                    source(block)));
                return true;
            }
            case ProceduralBlockKind::Always:
                break;
            default:
                // always_comb/always_ff/always_latch/final 一律 fail-closed，不降级。
                fail("unsupported procedural block " +
                         std::string(slang::ast::toString(block.procedureKind)) + " at " +
                         location_text(block_range),
                     block);
                return false;
        }

        const auto* timed = block.getBody().as_if<TimedStatement>();
        if (timed == nullptr) {
            fail_unsupported("always timing control", block.getBody().sourceRange);
            return false;
        }
        std::vector<ir::TimingSense> sensitivity;
        bool implicit = false;
        switch (timed->timing.kind) {
            case TimingControlKind::ImplicitEvent:
                implicit = true;
                break;
            case TimingControlKind::SignalEvent:
                if (!lower_signal_event(timed->timing.as<SignalEventControl>(), sensitivity))
                    return false;
                break;
            case TimingControlKind::EventList:
                for (const auto* event : timed->timing.as<EventListControl>().events) {
                    const auto* signal_event = event->as_if<SignalEventControl>();
                    if (signal_event == nullptr) {
                        fail_unsupported(slang::ast::toString(event->kind), event->sourceRange);
                        return false;
                    }
                    if (!lower_signal_event(*signal_event, sensitivity))
                        return false;
                }
                break;
            default:
                fail_unsupported(slang::ast::toString(timed->timing.kind),
                                 timed->timing.sourceRange);
                return false;
        }

        const auto body = lower_statement(timed->stmt);
        if (!body)
            return false;
        std::vector<ir::SignalId> reads;
        if (implicit)
            reads = collect_statement_reads(*body);
        static_cast<void>(model.add_process(ir::ProcessKind::Always, std::move(sensitivity),
                                            std::move(reads), *body, source(block)));
        return true;
    }

    [[nodiscard]] bool lower_net_initializer(const NetSymbol& net) {
        if (net.getInitializer() == nullptr)
            return true;
        if (net.getDelay() != nullptr || net.getChargeStrength().has_value()) {
            fail("unsupported net initializer delay or strength", net);
            return false;
        }
        const auto [drive0, drive1] = net.getDriveStrength();
        if (drive0.has_value() || drive1.has_value()) {
            fail("unsupported net initializer delay or strength", net);
            return false;
        }
        const auto id = signal_id(net);
        const auto type = packed_type(net.getType(), net);
        const auto value = lower_expression(*net.getInitializer());
        if (!id || !type || !value)
            return false;
        const auto target = model.add_whole_signal_lvalue(*id, *type, source(net));
        static_cast<void>(model.add_continuous_assign(target, *value, source(net)));
        return true;
    }

    [[nodiscard]] bool collect_scope(const Scope& scope) {
        for (const auto& symbol : scope.members()) {
            if (failure)
                return false;
            switch (symbol.kind) {
                case SymbolKind::Net: {
                    const auto& net = symbol.as<NetSymbol>();
                    if (!add_signal(net))
                        return false;
                    if (net.getInitializer() != nullptr)
                        initialized_nets.push_back(&net);
                    break;
                }
                case SymbolKind::Variable:
                    if (!add_signal(symbol.as<VariableSymbol>()))
                        return false;
                    break;
                case SymbolKind::ContinuousAssign:
                    assignments.push_back(&symbol.as<ContinuousAssignSymbol>());
                    break;
                case SymbolKind::ProceduralBlock:
                    procedural_blocks.push_back(&symbol.as<ProceduralBlockSymbol>());
                    break;
                case SymbolKind::Instance:
                    fail("unsupported child instance " + symbol.getHierarchicalPath() +
                             ": port binding not implemented",
                         symbol);
                    return false;
                case SymbolKind::InstanceArray:
                    fail("unsupported child instance " + symbol.getHierarchicalPath() +
                             ": port binding not implemented",
                         symbol);
                    return false;
                case SymbolKind::GenerateBlock:
                case SymbolKind::GenerateBlockArray:
                    if (!collect_scope(symbol.as<Scope>()))
                        return false;
                    break;
                case SymbolKind::Port:
                    break;
                default:
                    break;
            }
        }
        return true;
    }

    [[nodiscard]] bool collect_ports(const InstanceBodySymbol& body) {
        for (const auto* port_symbol : body.getPortList()) {
            if (port_symbol->kind != SymbolKind::Port) {
                fail("unsupported port", *port_symbol);
                return false;
            }
            const auto& port = port_symbol->as<PortSymbol>();
            if (!port.isAnsiPort || port.isNullPort || port.internalSymbol == nullptr ||
                (port.internalSymbol->kind != SymbolKind::Net &&
                 port.internalSymbol->kind != SymbolKind::Variable)) {
                fail("unsupported port", port);
                return false;
            }
            if (!add_signal(port.internalSymbol->as<ValueSymbol>()))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool lower(const InstanceBodySymbol& body) {
        if (!collect_ports(body) || !collect_scope(body))
            return false;
        for (const auto* net : initialized_nets) {
            if (!lower_net_initializer(*net))
                return false;
        }
        for (const auto* assignment : assignments) {
            if (!lower_assignment(*assignment))
                return false;
        }
        for (const auto* block : procedural_blocks) {
            if (!lower_procedural_block(*block))
                return false;
        }
        return !failure;
    }
};

void append_diagnostic(CompileResult& result, const slang::driver::Driver& driver,
                       const slang::Diagnostic& diagnostic) {
    const auto location = driver.sourceManager.getFullyExpandedLoc(diagnostic.location);
    ir::SourceSpan source;
    if (location != slang::SourceLocation::NoLocation) {
        source = {std::string(driver.sourceManager.getFileName(location)),
                   static_cast<std::uint32_t>(driver.sourceManager.getLineNumber(location)),
                   static_cast<std::uint32_t>(driver.sourceManager.getColumnNumber(location))};
    }
    result.diagnostics.push_back({driver.diagEngine.formatMessage(diagnostic), std::move(source)});
}

void append_parse_diagnostics(CompileResult& result, const slang::driver::Driver& driver) {
    for (const auto& tree : driver.syntaxTrees) {
        for (const auto& diagnostic : tree->diagnostics()) {
            if (diagnostic.isError())
                append_diagnostic(result, driver, diagnostic);
        }
    }
}

std::string filelist_error(const ProjectSpec& spec) {
    return "unable to process filelist " + spec.filelist.string();
}

}  // namespace

CompileResult compile_to_ir(const ProjectSpec& spec) {
    CompileResult result;
    slang::driver::Driver driver;
    driver.addStandardArgs();

    std::vector<std::string> arguments;
    arguments.reserve(6 + spec.include_dirs.size() * 2 + spec.defines.size() * 2);
    arguments.emplace_back("a1-simc");
    arguments.emplace_back("-f");
    arguments.push_back(spec.filelist.string());
    arguments.emplace_back("--top");
    arguments.push_back(spec.top);
    for (const auto& include : spec.include_dirs) {
        arguments.emplace_back("-I");
        arguments.push_back(include.string());
    }
    for (const auto& define : spec.defines) {
        arguments.emplace_back("-D");
        arguments.push_back(define);
    }
    std::vector<const char*> argv;
    argv.reserve(arguments.size());
    for (const auto& argument : arguments)
        argv.push_back(argument.c_str());

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        append_parse_diagnostics(result, driver);
        if (result.diagnostics.empty())
            result.diagnostics.push_back({filelist_error(spec), {spec.filelist.string(), 0, 0}});
        return result;
    }
    if (!driver.processOptions() || !driver.parseAllSources()) {
        append_parse_diagnostics(result, driver);
        if (result.diagnostics.empty())
            result.diagnostics.push_back({filelist_error(spec), {spec.filelist.string(), 0, 0}});
        return result;
    }

    auto compilation = driver.createCompilation();
    const auto& diagnostics = compilation->getAllDiagnostics();
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.isError())
            append_diagnostic(result, driver, diagnostic);
    }
    if (!result.diagnostics.empty())
        return result;

    const auto& root = compilation->getRoot();
    const InstanceBodySymbol* body = nullptr;
    for (const auto* instance : root.topInstances) {
        if (instance->name == spec.top) {
            if (body != nullptr) {
                result.diagnostics.push_back({"top instance is not unique", {}});
                return result;
            }
            body = &instance->body;
        }
    }
    if (body == nullptr) {
        result.diagnostics.push_back({"top instance was not found", {}});
        return result;
    }

    LoweringContext context(driver);
    if (!context.lower(*body)) {
        result.diagnostics.push_back(*context.failure);
        return result;
    }

    const auto validation = context.model.validate();
    if (!validation.empty()) {
        result.diagnostics.push_back({"invalid ModelIR: " + validation.front(), {}});
        return result;
    }
    result.model = std::move(context.model);
    return result;
}

}  // namespace a1::frontend
