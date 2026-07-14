#include "runtime/edge.h"

namespace a1::runtime {

EdgeKind detect_edge(LogicValue::Bit from, LogicValue::Bit to) {
    if (from == to) return EdgeKind::None;
    if (to == LogicValue::Bit::One && from != LogicValue::Bit::One) return EdgeKind::Posedge;
    if (to == LogicValue::Bit::Zero && from != LogicValue::Bit::Zero) return EdgeKind::Negedge;
    return EdgeKind::None;
}

}  // namespace a1::runtime
