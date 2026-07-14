#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ir/model_ir.h"

namespace a1::frontend {

struct ProjectSpec {
    std::filesystem::path filelist;
    std::string top;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<std::string> defines;
};

struct Diagnostic {
    std::string message;
    ir::SourceSpan source;
};

struct CompileResult {
    std::optional<ir::ModelIR> model;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] CompileResult compile_to_ir(const ProjectSpec& spec);

}  // namespace a1::frontend
