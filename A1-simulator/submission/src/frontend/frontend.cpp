#include "frontend/frontend.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/Expression.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
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
using slang::ast::Compilation;
using slang::ast::ConditionalExpression;
using slang::ast::ContinuousAssignSymbol;
using slang::ast::ConversionExpression;
using slang::ast::ElementSelectExpression;
using slang::ast::Expression;
using slang::ast::ExpressionKind;
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
using slang::ast::ProceduralBlockSymbol;
using slang::ast::RangeSelectExpression;
using slang::ast::RangeSelectionKind;
using slang::ast::ReplicationExpression;
using slang::ast::Scope;
using slang::ast::Symbol;
using slang::ast::SymbolKind;
using slang::ast::Type;
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

    [[nodiscard]] std::optional<ir::SignalId> lvalue_signal(const Expression& expression) {
        const auto* named = expression.as_if<NamedValueExpression>();
        if (named == nullptr || named->symbol.kind != SymbolKind::Net) {
            fail("unsupported continuous-assignment lvalue", expression.sourceRange);
            return std::nullopt;
        }
        const auto id = signal_id(named->symbol);
        if (!id) {
            fail("continuous-assignment lvalue signal was not lowered", expression.sourceRange);
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

    [[nodiscard]] std::optional<ir::LValueId> lower_lvalue(const Expression& expression) {
        const auto target_type = packed_type(*expression.type, expression.sourceRange);
        if (!target_type)
            return std::nullopt;

        if (const auto* named = expression.as_if<NamedValueExpression>()) {
            const auto id = signal_id(named->symbol);
            if (!id || named->symbol.kind != SymbolKind::Net) {
                fail("unsupported continuous-assignment lvalue", expression.sourceRange);
                return std::nullopt;
            }
            return model.add_whole_signal_lvalue(*id, *target_type, source(expression.sourceRange));
        }

        if (const auto* select = expression.as_if<ElementSelectExpression>()) {
            const auto id = lvalue_signal(select->value());
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
            const auto id = lvalue_signal(select->value());
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
            return lower_lvalue(conversion->operand());

    invalid_lvalue:
        fail("unsupported continuous-assignment lvalue", expression.sourceRange);
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
                const auto operation_type = packed_type(
                    *binary.left().type, binary.left().sourceRange);
                if (!operation_type)
                    return std::nullopt;
                return model.add_binary(*op, *lhs, *rhs, *operation_type, *type, source_span);
            }
            case ExpressionKind::ConditionalOp: {
                const auto& conditional = expression.as<ConditionalExpression>();
                if (conditional.conditions.size() != 1 ||
                    conditional.conditions.front().pattern != nullptr) {
                    fail("unsupported conditional expression", expression.sourceRange);
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
            case ExpressionKind::Assignment: {
                const auto& assignment = expression.as<AssignmentExpression>();
                if (assignment.isCompound() || assignment.timingControl != nullptr ||
                    assignment.isNonBlocking()) {
                    fail("unsupported assignment expression", expression.sourceRange);
                    return std::nullopt;
                }
                return lower_expression(assignment.right());
            }
            default:
                fail("unsupported " + std::string(slang::ast::toString(expression.kind)) +
                         " at " + location_text(expression.sourceRange),
                     expression.sourceRange);
                return std::nullopt;
        }

    invalid_expression:
        fail("unsupported expression select", expression.sourceRange);
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
                    fail("unsupported procedural block", symbol);
                    return false;
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
