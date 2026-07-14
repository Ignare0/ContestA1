#include "../unit/test_support.h"
#include "frontend/frontend.h"
#include "runtime/scheduler.h"

int main() {
    using a1::frontend::ProjectSpec;
    using a1::frontend::compile_to_ir;
    using a1::runtime::run_model;

    const auto directory = a1::test::make_temp_dir("flat-core-slice");
    const auto source_path = directory / "top.sv";
    const auto filelist = directory / "filelist.txt";
    a1::test::write_text(source_path, "module top; endmodule\n");
    a1::test::write_text(filelist, source_path.string() + "\n");

    const auto compiled = compile_to_ir(ProjectSpec{filelist, "top", {}, {}});
    A1_EXPECT(compiled.model.has_value());

    const auto result = run_model(*compiled.model);
    A1_EXPECT(!result.error);
    return EXIT_SUCCESS;
}
