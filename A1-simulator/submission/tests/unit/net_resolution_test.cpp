#include <array>
#include <stdexcept>

#include "test_support.h"
#include "runtime/value.h"

using a1::runtime::LogicValue;

int main() {
    const std::array<LogicValue, 0> no_drivers{};
    A1_EXPECT(a1::runtime::resolve_net(no_drivers).to_binary() == "z");

    bool rejected_mismatched_widths = false;
    try {
        static_cast<void>(a1::runtime::resolve_net(
            std::array{LogicValue::from_binary("0"), LogicValue::from_binary("00")}));
    } catch (const std::invalid_argument&) {
        rejected_mismatched_widths = true;
    }
    A1_EXPECT(rejected_mismatched_widths);

    A1_EXPECT(a1::runtime::resolve_net(std::array{LogicValue::from_binary("z")}).to_binary() ==
              "z");
    A1_EXPECT(a1::runtime::resolve_net(std::array{LogicValue::from_binary("0"),
                                                   LogicValue::from_binary("z")})
                  .to_binary() == "0");
    A1_EXPECT(a1::runtime::resolve_net(std::array{LogicValue::from_binary("1"),
                                                   LogicValue::from_binary("1")})
                  .to_binary() == "1");
    A1_EXPECT(a1::runtime::resolve_net(std::array{LogicValue::from_binary("0"),
                                                   LogicValue::from_binary("1")})
                  .to_binary() == "x");
    A1_EXPECT(a1::runtime::resolve_net(std::array{LogicValue::from_binary("x"),
                                                   LogicValue::from_binary("1")})
                  .to_binary() == "x");
    A1_EXPECT(a1::runtime::resolve_net(std::array{LogicValue::from_binary("z0x1"),
                                                   LogicValue::from_binary("z1z1"),
                                                   LogicValue::from_binary("zzzz")})
                  .to_binary() == "zxx1");
    return EXIT_SUCCESS;
}
