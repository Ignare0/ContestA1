#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

#include "frontend/ast_probe.h"
#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"

int main(int argc, char** argv) {
    slang::driver::Driver driver;
    driver.addStandardArgs();

    std::optional<bool> show_help;
    std::optional<std::string> output_path;
    driver.cmdLine.add("-h,--help", show_help, "Display help and exit");
    driver.cmdLine.add("--probe-output", output_path,
                       "Write the elaborated AST inventory as JSON", "<file>");

    if (!driver.parseCommandLine(argc, argv))
        return 1;
    if (show_help == true) {
        std::puts(driver.cmdLine.getHelpText("A1 elaborated AST probe").c_str());
        return 0;
    }
    if (!output_path) {
        driver.printError("--probe-output is required");
        return 1;
    }
    if (!driver.processOptions())
        return 2;
    if (!driver.parseAllSources()) {
        static_cast<void>(driver.reportDiagnostics(false));
        return 3;
    }

    auto compilation = driver.createCompilation();
    driver.reportCompilation(*compilation, false);
    if (!driver.reportDiagnostics(false))
        return 3;

    const auto report = a1::frontend::collect_probe(*compilation);
    std::ofstream output(*output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        driver.printError("cannot open probe output: " + *output_path);
        return 4;
    }
    output << report.to_json();
    if (!output) {
        driver.printError("cannot write probe output: " + *output_path);
        return 4;
    }
    return 0;
}
