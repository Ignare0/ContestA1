#include "test_support.h"
#include "runtime/continuous_evaluator.h"

int main() {
    a1::runtime::SignalStore signals;
    A1_EXPECT(signals.empty());
    return EXIT_SUCCESS;
}
