#pragma once

#include "frontend/probe_report.h"

namespace slang::ast {
class Compilation;
}

namespace a1::frontend {

[[nodiscard]] ProbeReport collect_probe(slang::ast::Compilation& compilation);

} // namespace a1::frontend
