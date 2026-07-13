#pragma once

#include "ir/model_ir.h"

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

private:
    [[nodiscard]] static LogicValue evaluate(const ir::ModelIR& model,
                                             const SignalStore& store,
                                             ir::ExprId expression);
};

}  // namespace a1::runtime
