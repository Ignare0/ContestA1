#include "test_support.h"
#include "runtime/edge.h"

#include <array>

int main() {
    using a1::runtime::EdgeKind;
    using a1::runtime::LogicValue;
    using a1::runtime::detect_edge;
    using Bit = LogicValue::Bit;

    A1_EXPECT(detect_edge(Bit::Zero, Bit::One) == EdgeKind::Posedge);
    A1_EXPECT(detect_edge(Bit::X, Bit::One) == EdgeKind::Posedge);
    A1_EXPECT(detect_edge(Bit::Z, Bit::One) == EdgeKind::Posedge);
    A1_EXPECT(detect_edge(Bit::One, Bit::Zero) == EdgeKind::Negedge);
    A1_EXPECT(detect_edge(Bit::X, Bit::Zero) == EdgeKind::Negedge);
    A1_EXPECT(detect_edge(Bit::Z, Bit::Zero) == EdgeKind::Negedge);

    const std::array<Bit, 4> bits{Bit::Zero, Bit::One, Bit::X, Bit::Z};
    for (Bit from : bits) {
        A1_EXPECT(detect_edge(from, from) == EdgeKind::None);
    }
    A1_EXPECT(detect_edge(Bit::Zero, Bit::X) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::One, Bit::X) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::Zero, Bit::Z) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::One, Bit::Z) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::X, Bit::Z) == EdgeKind::None);
    A1_EXPECT(detect_edge(Bit::Z, Bit::X) == EdgeKind::None);
    return EXIT_SUCCESS;
}
