#include "test_support.h"
#include "runtime/edge.h"

int main() {
    using a1::runtime::EdgeKind;
    using a1::runtime::LogicValue;
    using a1::runtime::detect_edge;
    A1_EXPECT(detect_edge(LogicValue::Bit::Zero, LogicValue::Bit::One) == EdgeKind::Posedge);
    return EXIT_SUCCESS;
}
