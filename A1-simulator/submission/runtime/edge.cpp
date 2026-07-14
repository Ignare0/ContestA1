#include "runtime/edge.h"

#include <stdexcept>

namespace a1::runtime {

EdgeKind detect_edge(LogicValue::Bit /*from*/, LogicValue::Bit /*to*/) {
    throw std::logic_error("unimplemented");
}

}  // namespace a1::runtime
