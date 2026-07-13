#pragma once

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace a1::runtime {

enum class StateDomain : std::uint8_t { TwoState, FourState };
enum class TruthValue : std::uint8_t { Zero, One, Unknown };

class LogicValue {
public:
    enum class Bit : std::uint8_t { Zero, One, X, Z };

    static LogicValue zeros(std::uint32_t width);
    static LogicValue ones(std::uint32_t width);
    static LogicValue x(std::uint32_t width);
    static LogicValue z(std::uint32_t width);
    static LogicValue from_binary(std::string_view text);
    static LogicValue concat(std::initializer_list<LogicValue> values);
    static LogicValue add(const LogicValue&, const LogicValue&, std::uint32_t, bool);
    static LogicValue subtract(const LogicValue&, const LogicValue&, std::uint32_t, bool);
    static LogicValue logical_equal(const LogicValue&, const LogicValue&);
    static LogicValue case_equal(const LogicValue&, const LogicValue&);
    static LogicValue greater_equal(const LogicValue&, const LogicValue&, bool);
    static LogicValue conditional(const LogicValue&, const LogicValue&, const LogicValue&);

    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] bool is_signed() const;
    [[nodiscard]] Bit bit(std::uint32_t index) const;
    [[nodiscard]] std::string to_binary() const;
    [[nodiscard]] LogicValue resize(std::uint32_t width, bool is_signed) const;
    [[nodiscard]] LogicValue coerce(std::uint32_t width, bool is_signed,
                                    StateDomain domain) const;
    [[nodiscard]] LogicValue slice(std::uint32_t msb, std::uint32_t lsb) const;
    [[nodiscard]] LogicValue with_slice(std::uint32_t bit_offset,
                                        const LogicValue& payload) const;
    [[nodiscard]] LogicValue replicate(std::uint32_t count) const;
    [[nodiscard]] LogicValue reduce_and() const;
    [[nodiscard]] LogicValue reduce_or() const;
    [[nodiscard]] LogicValue reduce_xor() const;
    [[nodiscard]] LogicValue shift_left(const LogicValue&, std::uint32_t) const;
    [[nodiscard]] LogicValue shift_right(const LogicValue&, std::uint32_t, bool) const;
    [[nodiscard]] TruthValue truth_value() const;
    [[nodiscard]] bool exactly_equals(const LogicValue&) const;
    [[nodiscard]] bool is_z() const;

    friend LogicValue operator~(const LogicValue&);
    friend LogicValue operator&(const LogicValue&, const LogicValue&);
    friend LogicValue operator|(const LogicValue&, const LogicValue&);
    friend LogicValue operator^(const LogicValue&, const LogicValue&);
    friend LogicValue operator!(const LogicValue&);
    friend LogicValue resolve_net(std::span<const LogicValue>);

private:
    LogicValue(std::uint32_t width, bool is_signed, Bit fill);

    static LogicValue bitwise_binary(const LogicValue& lhs, const LogicValue& rhs,
                                     char operation);
    void set_bit(std::uint32_t index, Bit value);
    void clear_unused_high_bits();

    std::uint32_t width_;
    bool is_signed_;
    std::vector<std::uint64_t> aval_;
    std::vector<std::uint64_t> bval_;
};

LogicValue resolve_net(std::span<const LogicValue> drivers);

}  // namespace a1::runtime
