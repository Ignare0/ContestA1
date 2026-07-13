#include "runtime/value.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace a1::runtime {
namespace {

constexpr std::uint32_t kWordBits = 64;

bool is_known(LogicValue::Bit bit) {
    return bit == LogicValue::Bit::Zero || bit == LogicValue::Bit::One;
}

LogicValue::Bit unknown_as_x(LogicValue::Bit bit) {
    return is_known(bit) ? bit : LogicValue::Bit::X;
}

std::uint32_t common_width(const LogicValue& lhs, const LogicValue& rhs) {
    return std::max(lhs.width(), rhs.width());
}

bool common_signed(const LogicValue& lhs, const LogicValue& rhs) {
    return lhs.is_signed() && rhs.is_signed();
}

LogicValue extend_for_binary_operation(const LogicValue& value, std::uint32_t width,
                                       bool operands_are_signed) {
    return value.resize(width, operands_are_signed);
}

bool has_unknown(const LogicValue& value) {
    for (std::uint32_t index = 0; index < value.width(); ++index) {
        if (!is_known(value.bit(index))) {
            return true;
        }
    }
    return false;
}

bool shift_count(const LogicValue& value, std::uint64_t* count) {
    *count = 0;
    for (std::uint32_t index = 0; index < value.width(); ++index) {
        const auto current = value.bit(index);
        if (!is_known(current)) {
            return false;
        }
        if (current == LogicValue::Bit::One) {
            if (index >= kWordBits) {
                *count = std::numeric_limits<std::uint64_t>::max();
                return true;
            }
            *count |= std::uint64_t{1} << index;
        }
    }
    return true;
}

}  // namespace

LogicValue::LogicValue(std::uint32_t width, bool is_signed, Bit fill)
    : width_(width),
      is_signed_(is_signed),
      aval_(width / kWordBits + (width % kWordBits != 0)),
      bval_(width / kWordBits + (width % kWordBits != 0)) {
    for (std::uint32_t index = 0; index < width_; ++index) {
        set_bit(index, fill);
    }
    clear_unused_high_bits();
}

LogicValue LogicValue::zeros(std::uint32_t width) {
    return LogicValue(width, false, Bit::Zero);
}

LogicValue LogicValue::ones(std::uint32_t width) {
    return LogicValue(width, false, Bit::One);
}

LogicValue LogicValue::x(std::uint32_t width) {
    return LogicValue(width, false, Bit::X);
}

LogicValue LogicValue::z(std::uint32_t width) {
    return LogicValue(width, false, Bit::Z);
}

LogicValue LogicValue::from_binary(std::string_view text) {
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("binary logic value is too wide");
    }
    auto result = zeros(static_cast<std::uint32_t>(text.size()));
    result.is_signed_ = true;
    for (std::uint32_t index = 0; index < result.width_; ++index) {
        const auto character = text[text.size() - 1 - index];
        switch (character) {
            case '0': result.set_bit(index, Bit::Zero); break;
            case '1': result.set_bit(index, Bit::One); break;
            case 'x':
            case 'X': result.set_bit(index, Bit::X); break;
            case 'z':
            case 'Z': result.set_bit(index, Bit::Z); break;
            default:
                throw std::invalid_argument("binary logic value contains an invalid character");
        }
    }
    return result;
}

LogicValue LogicValue::concat(std::initializer_list<LogicValue> values) {
    std::uint64_t total_width = 0;
    for (const auto& value : values) {
        total_width += value.width_;
    }
    if (total_width > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("concatenation width is too large");
    }

    auto result = zeros(static_cast<std::uint32_t>(total_width));
    std::uint32_t offset = 0;
    for (auto iterator = values.end(); iterator != values.begin();) {
        --iterator;
        for (std::uint32_t index = 0; index < iterator->width_; ++index) {
            result.set_bit(offset + index, iterator->bit(index));
        }
        offset += iterator->width_;
    }
    return result;
}

LogicValue LogicValue::add(const LogicValue& lhs, const LogicValue& rhs, std::uint32_t width,
                           bool is_signed) {
    const auto left = lhs.resize(width, is_signed);
    const auto right = rhs.resize(width, is_signed);
    if (has_unknown(left) || has_unknown(right)) {
        return x(width).resize(width, is_signed);
    }

    auto result = zeros(width).resize(width, is_signed);
    bool carry = false;
    for (std::uint32_t index = 0; index < width; ++index) {
        const auto a = left.bit(index) == Bit::One;
        const auto b = right.bit(index) == Bit::One;
        result.set_bit(index, (a != b) != carry ? Bit::One : Bit::Zero);
        carry = (a && b) || (a && carry) || (b && carry);
    }
    return result;
}

LogicValue LogicValue::subtract(const LogicValue& lhs, const LogicValue& rhs, std::uint32_t width,
                                bool is_signed) {
    const auto left = lhs.resize(width, is_signed);
    const auto right = rhs.resize(width, is_signed);
    if (has_unknown(left) || has_unknown(right)) {
        return x(width).resize(width, is_signed);
    }

    auto result = zeros(width).resize(width, is_signed);
    bool carry = true;
    for (std::uint32_t index = 0; index < width; ++index) {
        const auto a = left.bit(index) == Bit::One;
        const auto b = right.bit(index) != Bit::One;
        result.set_bit(index, (a != b) != carry ? Bit::One : Bit::Zero);
        carry = (a && b) || (a && carry) || (b && carry);
    }
    return result;
}

LogicValue LogicValue::logical_equal(const LogicValue& lhs, const LogicValue& rhs) {
    const auto width = common_width(lhs, rhs);
    const auto operands_are_signed = common_signed(lhs, rhs);
    const auto left = extend_for_binary_operation(lhs, width, operands_are_signed);
    const auto right = extend_for_binary_operation(rhs, width, operands_are_signed);
    bool unknown = false;
    for (std::uint32_t index = 0; index < width; ++index) {
        const auto a = left.bit(index);
        const auto b = right.bit(index);
        if (is_known(a) && is_known(b) && a != b) {
            return zeros(1);
        }
        unknown = unknown || !is_known(a) || !is_known(b);
    }
    return unknown ? x(1) : ones(1);
}

LogicValue LogicValue::case_equal(const LogicValue& lhs, const LogicValue& rhs) {
    const auto width = common_width(lhs, rhs);
    const auto operands_are_signed = common_signed(lhs, rhs);
    const auto left = extend_for_binary_operation(lhs, width, operands_are_signed);
    const auto right = extend_for_binary_operation(rhs, width, operands_are_signed);
    for (std::uint32_t index = 0; index < width; ++index) {
        if (left.bit(index) != right.bit(index)) {
            return zeros(1);
        }
    }
    return ones(1);
}

LogicValue LogicValue::greater_equal(const LogicValue& lhs, const LogicValue& rhs, bool is_signed) {
    const auto width = common_width(lhs, rhs);
    const auto left = lhs.resize(width, is_signed);
    const auto right = rhs.resize(width, is_signed);
    if (has_unknown(left) || has_unknown(right)) {
        return x(1);
    }
    if (width == 0) {
        return ones(1);
    }

    if (is_signed && left.bit(width - 1) != right.bit(width - 1)) {
        return left.bit(width - 1) == Bit::Zero ? ones(1) : zeros(1);
    }
    for (std::uint32_t index = width; index > 0; --index) {
        const auto a = left.bit(index - 1);
        const auto b = right.bit(index - 1);
        if (a != b) {
            return a == Bit::One ? ones(1) : zeros(1);
        }
    }
    return ones(1);
}

LogicValue LogicValue::conditional(const LogicValue& condition, const LogicValue& when_true,
                                   const LogicValue& when_false) {
    switch (condition.truth_value()) {
        case TruthValue::One: return when_true;
        case TruthValue::Zero: return when_false;
        case TruthValue::Unknown: break;
    }

    const auto width = common_width(when_true, when_false);
    const auto operands_are_signed = common_signed(when_true, when_false);
    const auto true_value = extend_for_binary_operation(when_true, width, operands_are_signed);
    const auto false_value = extend_for_binary_operation(when_false, width, operands_are_signed);
    auto result = x(width).resize(width, operands_are_signed);
    for (std::uint32_t index = 0; index < width; ++index) {
        if (true_value.bit(index) == false_value.bit(index)) {
            result.set_bit(index, true_value.bit(index));
        }
    }
    return result;
}

LogicValue LogicValue::bitwise_binary(const LogicValue& lhs, const LogicValue& rhs,
                                      char operation) {
    const auto width = common_width(lhs, rhs);
    const auto operands_are_signed = common_signed(lhs, rhs);
    const auto left = extend_for_binary_operation(lhs, width, operands_are_signed);
    const auto right = extend_for_binary_operation(rhs, width, operands_are_signed);
    auto result = zeros(width).resize(width, operands_are_signed);

    for (std::uint32_t index = 0; index < width; ++index) {
        const auto a = left.bit(index);
        const auto b = right.bit(index);
        Bit output = Bit::X;

        if (operation == '&') {
            if (a == Bit::Zero || b == Bit::Zero) {
                output = Bit::Zero;
            } else if (a == Bit::One && b == Bit::One) {
                output = Bit::One;
            }
        } else if (operation == '|') {
            if (a == Bit::One || b == Bit::One) {
                output = Bit::One;
            } else if (a == Bit::Zero && b == Bit::Zero) {
                output = Bit::Zero;
            }
        } else if (is_known(a) && is_known(b)) {
            output = (a == b) ? Bit::Zero : Bit::One;
        }
        result.set_bit(index, output);
    }
    return result;
}

std::uint32_t LogicValue::width() const {
    return width_;
}

bool LogicValue::is_signed() const {
    return is_signed_;
}

LogicValue::Bit LogicValue::bit(std::uint32_t index) const {
    if (index >= width_) {
        throw std::invalid_argument("logic value bit index is out of range");
    }
    const auto word = index / kWordBits;
    const auto offset = index % kWordBits;
    const auto a = (aval_[word] >> offset) & 1U;
    const auto b = (bval_[word] >> offset) & 1U;
    if (b == 0) {
        return a == 0 ? Bit::Zero : Bit::One;
    }
    return a == 0 ? Bit::Z : Bit::X;
}

std::string LogicValue::to_binary() const {
    std::string result;
    result.reserve(width_);
    for (std::uint32_t index = width_; index > 0; --index) {
        switch (bit(index - 1)) {
            case Bit::Zero: result.push_back('0'); break;
            case Bit::One: result.push_back('1'); break;
            case Bit::X: result.push_back('x'); break;
            case Bit::Z: result.push_back('z'); break;
        }
    }
    return result;
}

LogicValue LogicValue::resize(std::uint32_t width, bool is_signed) const {
    auto result = zeros(width);
    result.is_signed_ = is_signed;
    const auto copied_width = std::min(width_, width);
    for (std::uint32_t index = 0; index < copied_width; ++index) {
        result.set_bit(index, bit(index));
    }
    if (width > width_ && is_signed_ && is_signed && width_ != 0) {
        const auto extension = bit(width_ - 1);
        for (std::uint32_t index = width_; index < width; ++index) {
            result.set_bit(index, extension);
        }
    }
    result.clear_unused_high_bits();
    return result;
}

LogicValue LogicValue::coerce(std::uint32_t width, bool is_signed, StateDomain domain) const {
    auto result = resize(width, is_signed);
    if (domain == StateDomain::TwoState) {
        for (std::uint32_t index = 0; index < width; ++index) {
            if (!is_known(result.bit(index))) {
                result.set_bit(index, Bit::Zero);
            }
        }
    }
    result.clear_unused_high_bits();
    return result;
}

LogicValue LogicValue::slice(std::uint32_t msb, std::uint32_t lsb) const {
    if (lsb > msb || msb >= width_) {
        throw std::invalid_argument("logic value slice is out of range");
    }
    auto result = zeros(msb - lsb + 1);
    for (std::uint32_t index = 0; index < result.width_; ++index) {
        result.set_bit(index, bit(lsb + index));
    }
    return result;
}

LogicValue LogicValue::with_slice(std::uint32_t bit_offset, const LogicValue& payload) const {
    if (bit_offset > width_ || payload.width_ > width_ - bit_offset) {
        throw std::invalid_argument("logic value replacement slice is out of range");
    }
    auto result = *this;
    for (std::uint32_t index = 0; index < payload.width_; ++index) {
        result.set_bit(bit_offset + index, payload.bit(index));
    }
    result.clear_unused_high_bits();
    return result;
}

LogicValue LogicValue::replicate(std::uint32_t count) const {
    if (count != 0 && width_ > std::numeric_limits<std::uint32_t>::max() / count) {
        throw std::invalid_argument("replication width is too large");
    }
    auto result = zeros(width_ * count);
    for (std::uint32_t copy = 0; copy < count; ++copy) {
        for (std::uint32_t index = 0; index < width_; ++index) {
            result.set_bit(copy * width_ + index, bit(index));
        }
    }
    return result;
}

LogicValue LogicValue::reduce_and() const {
    bool unknown = false;
    for (std::uint32_t index = 0; index < width_; ++index) {
        const auto current = bit(index);
        if (current == Bit::Zero) {
            return zeros(1);
        }
        unknown = unknown || !is_known(current);
    }
    return unknown ? x(1) : ones(1);
}

LogicValue LogicValue::reduce_or() const {
    bool unknown = false;
    for (std::uint32_t index = 0; index < width_; ++index) {
        const auto current = bit(index);
        if (current == Bit::One) {
            return ones(1);
        }
        unknown = unknown || !is_known(current);
    }
    return unknown ? x(1) : zeros(1);
}

LogicValue LogicValue::reduce_xor() const {
    bool parity = false;
    for (std::uint32_t index = 0; index < width_; ++index) {
        const auto current = bit(index);
        if (!is_known(current)) {
            return x(1);
        }
        parity = parity != (current == Bit::One);
    }
    return parity ? ones(1) : zeros(1);
}

LogicValue LogicValue::shift_left(const LogicValue& amount, std::uint32_t width) const {
    std::uint64_t count = 0;
    if (!shift_count(amount, &count)) {
        return x(width).resize(width, is_signed_);
    }
    const auto source = resize(width, is_signed_);
    auto result = zeros(width).resize(width, is_signed_);
    if (count >= width) {
        return result;
    }
    for (std::uint32_t index = static_cast<std::uint32_t>(count); index < width; ++index) {
        result.set_bit(index, source.bit(index - static_cast<std::uint32_t>(count)));
    }
    return result;
}

LogicValue LogicValue::shift_right(const LogicValue& amount, std::uint32_t width,
                                   bool arithmetic) const {
    std::uint64_t count = 0;
    if (!shift_count(amount, &count)) {
        return x(width).resize(width, is_signed_);
    }
    const auto source = resize(width, is_signed_);
    const auto fill = arithmetic && width != 0 ? unknown_as_x(source.bit(width - 1)) : Bit::Zero;
    auto result = LogicValue(width, is_signed_, fill);
    if (count >= width) {
        return result;
    }
    for (std::uint32_t index = 0; index + count < width; ++index) {
        result.set_bit(index, source.bit(index + static_cast<std::uint32_t>(count)));
    }
    result.clear_unused_high_bits();
    return result;
}

TruthValue LogicValue::truth_value() const {
    bool unknown = false;
    for (std::uint32_t index = 0; index < width_; ++index) {
        const auto current = bit(index);
        if (current == Bit::One) {
            return TruthValue::One;
        }
        unknown = unknown || !is_known(current);
    }
    return unknown ? TruthValue::Unknown : TruthValue::Zero;
}

bool LogicValue::exactly_equals(const LogicValue& other) const {
    if (width_ != other.width_) {
        return false;
    }
    for (std::uint32_t index = 0; index < width_; ++index) {
        if (bit(index) != other.bit(index)) {
            return false;
        }
    }
    return true;
}

void LogicValue::set_bit(std::uint32_t index, Bit value) {
    if (index >= width_) {
        throw std::invalid_argument("logic value bit index is out of range");
    }
    const auto word = index / kWordBits;
    const auto offset = index % kWordBits;
    const auto mask = std::uint64_t{1} << offset;
    const auto a = value == Bit::One || value == Bit::X;
    const auto b = value == Bit::X || value == Bit::Z;
    aval_[word] = a ? aval_[word] | mask : aval_[word] & ~mask;
    bval_[word] = b ? bval_[word] | mask : bval_[word] & ~mask;
    clear_unused_high_bits();
}

void LogicValue::clear_unused_high_bits() {
    if (width_ == 0 || width_ % kWordBits == 0) {
        return;
    }
    const auto mask = (std::uint64_t{1} << (width_ % kWordBits)) - 1;
    aval_.back() &= mask;
    bval_.back() &= mask;
}

LogicValue resolve_net(std::span<const LogicValue> drivers) {
    if (drivers.empty()) {
        return LogicValue::z(1);
    }

    const auto width = drivers.front().width();
    for (const auto& driver : drivers) {
        if (driver.width() != width) {
            throw std::invalid_argument("net drivers must have equal widths");
        }
    }

    auto result = LogicValue::z(width);
    for (std::uint32_t index = 0; index < width; ++index) {
        bool saw_zero = false;
        bool saw_one = false;
        bool saw_x = false;
        for (const auto& driver : drivers) {
            switch (driver.bit(index)) {
                case LogicValue::Bit::Zero: saw_zero = true; break;
                case LogicValue::Bit::One: saw_one = true; break;
                case LogicValue::Bit::X: saw_x = true; break;
                case LogicValue::Bit::Z: break;
            }
        }

        const auto resolved = saw_x || (saw_zero && saw_one)
                                  ? LogicValue::Bit::X
                                  : saw_one ? LogicValue::Bit::One
                                             : saw_zero ? LogicValue::Bit::Zero
                                                        : LogicValue::Bit::Z;
        result.set_bit(index, resolved);
    }
    return result;
}

LogicValue operator~(const LogicValue& value) {
    auto result = LogicValue::zeros(value.width_).resize(value.width_, value.is_signed_);
    for (std::uint32_t index = 0; index < value.width_; ++index) {
        switch (value.bit(index)) {
            case LogicValue::Bit::Zero: result.set_bit(index, LogicValue::Bit::One); break;
            case LogicValue::Bit::One: result.set_bit(index, LogicValue::Bit::Zero); break;
            case LogicValue::Bit::X:
            case LogicValue::Bit::Z: result.set_bit(index, LogicValue::Bit::X); break;
        }
    }
    return result;
}

LogicValue operator&(const LogicValue& lhs, const LogicValue& rhs) {
    return LogicValue::bitwise_binary(lhs, rhs, '&');
}

LogicValue operator|(const LogicValue& lhs, const LogicValue& rhs) {
    return LogicValue::bitwise_binary(lhs, rhs, '|');
}

LogicValue operator^(const LogicValue& lhs, const LogicValue& rhs) {
    return LogicValue::bitwise_binary(lhs, rhs, '^');
}

LogicValue operator!(const LogicValue& value) {
    switch (value.truth_value()) {
        case TruthValue::Zero: return LogicValue::ones(1);
        case TruthValue::One: return LogicValue::zeros(1);
        case TruthValue::Unknown: return LogicValue::x(1);
    }
    return LogicValue::x(1);
}

}  // namespace a1::runtime
