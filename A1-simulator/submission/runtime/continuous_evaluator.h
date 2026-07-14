#pragma once

#include "ir/model_ir.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace a1::runtime {

class SignalStore {
public:
    explicit SignalStore(const ir::ModelIR& model);

    void set_variable(ir::SignalId signal, LogicValue value);
    void set_external_driver(ir::SignalId signal, LogicValue value);
    [[nodiscard]] const LogicValue& value(ir::SignalId signal) const;

private:
    friend class ContinuousEvaluator;

    std::vector<LogicValue> values_;
    std::vector<std::optional<LogicValue>> external_drivers_;
    struct AssignmentDriverSlot {
        ir::LValueId target;
        LogicValue payload;
    };
    std::vector<AssignmentDriverSlot> assignment_drivers_;
    std::vector<ir::Signal> signals_;
};

class ContinuousEvaluator {
public:
    [[nodiscard]] static std::optional<std::string> settle(const ir::ModelIR& model,
                                                            SignalStore& store);

    [[nodiscard]] static bool propagate_once(const ir::ModelIR& model, SignalStore& store);

    [[nodiscard]] static std::optional<std::string>
    settle_with_limit(const ir::ModelIR& model, SignalStore& store, std::uint64_t delta_limit);

    // 供 scheduler 复用的表达式求值入口（Phase 2 复用，禁止拷贝求值逻辑）。
    [[nodiscard]] static LogicValue evaluate(const ir::ModelIR& model,
                                             const SignalStore& store,
                                             ir::ExprId expression);

private:
    using ResolveTargetFn = std::function<void(ir::SignalId)>;

    static ResolveTargetFn make_resolve_target(const ir::ModelIR& model, SignalStore& store);
    static void apply_continuous_assign(const ir::ModelIR& model, SignalStore& store,
                                        ir::ContinuousAssignId assignment_id,
                                        const ResolveTargetFn& resolve_target);
    static void resolve_all_nets(const ir::ModelIR& model,
                                 const ResolveTargetFn& resolve_target);
    static bool signals_unchanged(const SignalStore& store,
                                  const std::vector<LogicValue>& before);
};

}  // namespace a1::runtime
