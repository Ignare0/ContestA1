#include "test_support.h"
#include "ir/model_ir.h"

int main() {
    const a1::ir::ModelIR model;
    A1_EXPECT(model.signals.empty());
    return EXIT_SUCCESS;
}
