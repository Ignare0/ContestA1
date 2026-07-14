#pragma once

#include "runtime/value.h"

namespace a1::runtime {

enum class EdgeKind : std::uint8_t { None, Posedge, Negedge };

[[nodiscard]] EdgeKind detect_edge(LogicValue::Bit from, LogicValue::Bit to);

}  // namespace a1::runtime
