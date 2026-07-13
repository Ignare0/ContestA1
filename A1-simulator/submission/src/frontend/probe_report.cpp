#include "frontend/probe_report.h"

#include <sstream>

namespace a1::frontend {
namespace {

std::string escape_json(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += ch; break;
        }
    }
    return result;
}

void append_counts(std::ostringstream& out, const ProbeReport::Counts& counts) {
    out << '{';
    bool first = true;
    for (const auto& [name, count] : counts) {
        if (!first)
            out << ", ";
        first = false;
        out << '"' << escape_json(name) << "\": " << count;
    }
    out << '}';
}

} // namespace

void ProbeReport::increment(Counts& counts, std::string_view name) {
    ++counts[std::string(name)];
}

std::string ProbeReport::to_json() const {
    std::ostringstream out;
    out << "{\n  \"top_instances\": [";
    for (std::size_t index = 0; index < top_instances.size(); ++index) {
        if (index != 0)
            out << ", ";
        out << '"' << escape_json(top_instances[index]) << '"';
    }
    out << "],\n  \"symbols\": ";
    append_counts(out, symbols);
    out << ",\n  \"statements\": ";
    append_counts(out, statements);
    out << ",\n  \"expressions\": ";
    append_counts(out, expressions);
    out << ",\n  \"timing_controls\": ";
    append_counts(out, timing_controls);
    out << ",\n  \"data_types\": ";
    append_counts(out, data_types);
    out << ",\n  \"system_calls\": ";
    append_counts(out, system_calls);
    out << "\n}\n";
    return out.str();
}

} // namespace a1::frontend
