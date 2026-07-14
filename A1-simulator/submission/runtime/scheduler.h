#pragma once

#include "ir/model_ir.h"
#include "runtime/continuous_evaluator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace a1::runtime {

struct SimOptions {
    std::uint64_t delta_limit = 10000;
    std::uint64_t time_limit = 1000000;
};

struct SimResult {
    std::optional<std::string> error;
    std::uint64_t time = 0;
    bool finished = false;
    // 成功结束（含 $finish）时非空，供垂直切片断言最终信号值。
    std::shared_ptr<SignalStore> final_store;
};

[[nodiscard]] SimResult run_model(const ir::ModelIR& model, const SimOptions& options = {});

}  // namespace a1::runtime
