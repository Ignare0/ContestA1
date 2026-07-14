#pragma once

#include "runtime/value.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace a1::ir {

struct SignalId {
    std::uint32_t value;
    auto operator<=>(const SignalId&) const = default;
};

struct ExprId {
    std::uint32_t value;
    auto operator<=>(const ExprId&) const = default;
};

struct LValueId {
    std::uint32_t value;
    auto operator<=>(const LValueId&) const = default;
};

struct ContinuousAssignId {
    std::uint32_t value;
    auto operator<=>(const ContinuousAssignId&) const = default;
};

struct StmtId {
    std::uint32_t value;
    auto operator<=>(const StmtId&) const = default;
};

struct ProcessId {
    std::uint32_t value;
    auto operator<=>(const ProcessId&) const = default;
};

enum class EdgeSense { AnyChange, Posedge, Negedge };
enum class ProcessKind { Initial, Always };

struct TimingSense {
    EdgeSense edge;
    SignalId signal;
};

struct BlockingAssignStmt {
    LValueId target;
    ExprId value;
};

struct NonBlockingAssignStmt {
    LValueId target;
    ExprId value;
};

struct SeqBlockStmt {
    std::vector<StmtId> statements;
};

struct DelayStmt {
    std::uint64_t ticks;
    StmtId next;
};

struct IfStmt {
    ExprId condition;
    StmtId then_stmt;
    std::optional<StmtId> else_stmt;
};

struct FinishStmt {};

struct DisplayStubStmt {};

struct EmptyStmt {};

struct SourceSpan {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct Statement {
    std::variant<BlockingAssignStmt, NonBlockingAssignStmt, SeqBlockStmt, DelayStmt, IfStmt,
                 FinishStmt, DisplayStubStmt, EmptyStmt>
        payload;
    SourceSpan source;
};

struct Process {
    ProcessKind kind;
    std::vector<TimingSense> sensitivity;
    std::vector<SignalId> read_signals;
    StmtId body;
    SourceSpan source;
};

struct PackedType {
    std::uint32_t width;
    bool is_signed;
    bool is_four_state;
    auto operator<=>(const PackedType&) const = default;
};

enum class SignalKind { Variable, Net };
enum class UnaryOp { BitwiseNot, LogicalNot, ReduceAnd, ReduceOr, ReduceXor };
enum class BinaryOp {
    Add,
    Subtract,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    LogicalEqual,
    LogicalNotEqual,
    CaseEqual,
    CaseNotEqual,
    GreaterEqual,
    ShiftLeft,
    LogicalShiftRight,
    ArithmeticShiftRight,
};

struct Signal {
    std::string canonical_path;
    PackedType type;
    SignalKind kind;
    SourceSpan source;
};

struct ConstantExpr {
    runtime::LogicValue value;
};

struct SignalRefExpr {
    SignalId signal;
};

struct UnaryExpr {
    UnaryOp op;
    ExprId operand;
};

struct BinaryExpr {
    BinaryOp op;
    ExprId lhs;
    ExprId rhs;
    PackedType operation_type;
};

struct ConditionalExpr {
    ExprId condition;
    ExprId when_true;
    ExprId when_false;
};

struct ConcatExpr {
    std::vector<ExprId> operands;
};

struct ReplicateExpr {
    ExprId operand;
    std::uint32_t count;
};

struct BitSelectExpr {
    ExprId value;
    std::uint32_t bit_offset;
};

struct RangeSelectExpr {
    ExprId value;
    std::uint32_t bit_offset;
    std::uint32_t width;
};

struct CastExpr {
    ExprId value;
};

struct Expression {
    std::variant<ConstantExpr, SignalRefExpr, UnaryExpr, BinaryExpr, ConditionalExpr,
                 ConcatExpr, ReplicateExpr, BitSelectExpr, RangeSelectExpr, CastExpr>
        payload;
    PackedType type;
    SourceSpan source;
};

struct WholeSignalLValue {
    SignalId signal;
};

struct BitSelectLValue {
    SignalId signal;
    std::uint32_t bit_offset;
};

struct RangeSelectLValue {
    SignalId signal;
    std::uint32_t bit_offset;
    std::uint32_t width;
};

struct LValue {
    std::variant<WholeSignalLValue, BitSelectLValue, RangeSelectLValue> payload;
    PackedType type;
    SourceSpan source;
};

struct ContinuousAssign {
    LValueId target;
    ExprId value;
    SourceSpan source;
    std::vector<SignalId> read_signals;
};

class ModelIR {
public:
    [[nodiscard]] SignalId add_signal(std::string canonical_path, PackedType type,
                                      SignalKind kind, SourceSpan source);

    [[nodiscard]] ExprId add_constant(runtime::LogicValue value, PackedType type,
                                      SourceSpan source);
    [[nodiscard]] ExprId add_signal_ref(SignalId signal, PackedType type, SourceSpan source);
    [[nodiscard]] ExprId add_unary(UnaryOp op, ExprId operand, PackedType type,
                                   SourceSpan source);
    [[nodiscard]] ExprId add_binary(BinaryOp op, ExprId lhs, ExprId rhs,
                                    PackedType operation_type, PackedType type,
                                    SourceSpan source);
    [[nodiscard]] ExprId add_conditional(ExprId condition, ExprId when_true, ExprId when_false,
                                         PackedType type, SourceSpan source);
    [[nodiscard]] ExprId add_concat(std::vector<ExprId> operands, PackedType type,
                                    SourceSpan source);
    [[nodiscard]] ExprId add_replicate(ExprId operand, std::uint32_t count,
                                       PackedType type, SourceSpan source);
    [[nodiscard]] ExprId add_bit_select(ExprId value, std::uint32_t bit_offset,
                                        PackedType type, SourceSpan source);
    [[nodiscard]] ExprId add_range_select(ExprId value, std::uint32_t bit_offset,
                                          std::uint32_t width, PackedType type,
                                          SourceSpan source);
    [[nodiscard]] ExprId add_cast(ExprId value, PackedType type, SourceSpan source);

    [[nodiscard]] LValueId add_whole_signal_lvalue(SignalId signal, PackedType type,
                                                    SourceSpan source);
    [[nodiscard]] LValueId add_bit_select_lvalue(SignalId signal,
                                                  std::uint32_t bit_offset,
                                                  PackedType type, SourceSpan source);
    [[nodiscard]] LValueId add_range_select_lvalue(SignalId signal,
                                                    std::uint32_t bit_offset,
                                                    std::uint32_t width,
                                                    PackedType type, SourceSpan source);
    [[nodiscard]] ContinuousAssignId add_continuous_assign(LValueId target, ExprId value,
                                                            SourceSpan source);

    [[nodiscard]] StmtId add_blocking_assign(LValueId target, ExprId value, SourceSpan source);
    [[nodiscard]] StmtId add_nonblocking_assign(LValueId target, ExprId value,
                                                SourceSpan source);
    [[nodiscard]] StmtId add_seq_block(std::vector<StmtId> statements, SourceSpan source);
    [[nodiscard]] StmtId add_delay(std::uint64_t ticks, StmtId next, SourceSpan source);
    [[nodiscard]] StmtId add_if(ExprId condition, StmtId then_stmt,
                                std::optional<StmtId> else_stmt, SourceSpan source);
    [[nodiscard]] StmtId add_finish(SourceSpan source);
    [[nodiscard]] StmtId add_display_stub(SourceSpan source);
    [[nodiscard]] StmtId add_empty(SourceSpan source);
    [[nodiscard]] ProcessId add_process(ProcessKind kind, std::vector<TimingSense> sensitivity,
                                        std::vector<SignalId> read_signals, StmtId body,
                                        SourceSpan source);

    [[nodiscard]] const std::vector<Signal>& signals() const;
    [[nodiscard]] const std::vector<Expression>& expressions() const;
    [[nodiscard]] const std::vector<LValue>& lvalues() const;
    [[nodiscard]] const std::vector<ContinuousAssign>& continuous_assigns() const;
    [[nodiscard]] const std::vector<Statement>& statements() const;
    [[nodiscard]] const std::vector<Process>& processes() const;

    [[nodiscard]] std::vector<std::string> validate() const;
    [[nodiscard]] std::optional<std::vector<ContinuousAssignId>> continuous_order() const;

    // 供 frontend 复用（@* read_signals 收集，IEEE 1364-2005 §9.7.5）。
    [[nodiscard]] std::vector<SignalId> collect_reads(ExprId value) const;

private:
    void refresh_read_sets() const;

    std::vector<Signal> signals_;
    std::vector<Expression> expressions_;
    std::vector<LValue> lvalues_;
    mutable std::vector<ContinuousAssign> continuous_assigns_;
    std::vector<Statement> statements_;
    std::vector<Process> processes_;
};

}  // namespace a1::ir
