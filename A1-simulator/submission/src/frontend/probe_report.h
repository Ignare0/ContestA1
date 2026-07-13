#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace a1::frontend {

struct ProbeReport {
    using Counts = std::map<std::string, std::uint64_t, std::less<>>;

    std::vector<std::string> top_instances;
    Counts symbols;
    Counts statements;
    Counts expressions;
    Counts timing_controls;
    Counts data_types;
    Counts system_calls;

    void increment(Counts& counts, std::string_view name);
    [[nodiscard]] std::string to_json() const;
};

} // namespace a1::frontend
