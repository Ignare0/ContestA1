#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace a1::test {
inline bool expect(bool condition, std::string_view expression,
                   std::string_view file, int line) {
    if (condition)
        return true;
    std::cerr << file << ':' << line << ": expectation failed: " << expression << '\n';
    return false;
}

inline std::filesystem::path make_temp_dir(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() /
                      (std::string("a1-") + std::string(name));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

inline void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) std::exit(EXIT_FAILURE);
    out << text;
    if (!out) std::exit(EXIT_FAILURE);
}
} // namespace a1::test

#define A1_EXPECT(expression) \
    do { \
        if (!::a1::test::expect((expression), #expression, __FILE__, __LINE__)) \
            return EXIT_FAILURE; \
    } while (false)
