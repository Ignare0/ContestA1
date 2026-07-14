#include "test_support.h"
#include "runtime/scheduler.h"

int main() {
    using a1::ir::ModelIR;
    using a1::runtime::run_model;

    const auto result = run_model(ModelIR{});
    A1_EXPECT(!result.error);
    return EXIT_SUCCESS;
}
