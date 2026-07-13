#include <cstdlib>
#include <iostream>
#include <string>

#include "frontend/probe_report.h"

int main() {
    a1::frontend::ProbeReport report;
    report.top_instances = {"tb2"};
    report.increment(report.symbols, "Instance");
    report.increment(report.symbols, "Instance");
    report.increment(report.statements, "Timed");
    report.increment(report.expressions, "Call");
    report.increment(report.timing_controls, "Delay");
    report.increment(report.data_types, "PackedArrayType");
    report.increment(report.system_calls, "$fscanf");

    const std::string expected =
        "{\n"
        "  \"top_instances\": [\"tb2\"],\n"
        "  \"symbols\": {\"Instance\": 2},\n"
        "  \"statements\": {\"Timed\": 1},\n"
        "  \"expressions\": {\"Call\": 1},\n"
        "  \"timing_controls\": {\"Delay\": 1},\n"
        "  \"data_types\": {\"PackedArrayType\": 1},\n"
        "  \"system_calls\": {\"$fscanf\": 1}\n"
        "}\n";

    if (report.to_json() != expected) {
        std::cerr << "unexpected JSON:\n" << report.to_json();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
