#include "test_support.h"
#include "runtime/value.h"

#include <string>

using a1::runtime::LogicValue;
using a1::runtime::StateDomain;
using a1::runtime::TruthValue;

int main() {
    A1_EXPECT(LogicValue::from_binary("10xz").to_binary() == "10xz");
    A1_EXPECT(LogicValue::zeros(65).to_binary() == std::string(65, '0'));
    A1_EXPECT(LogicValue::ones(65).to_binary() == std::string(65, '1'));
    A1_EXPECT(LogicValue::x(65).to_binary() == std::string(65, 'x'));
    A1_EXPECT(LogicValue::z(65).to_binary() == std::string(65, 'z'));
    A1_EXPECT(LogicValue::from_binary("101").resize(5, false).to_binary() == "00101");
    A1_EXPECT(LogicValue::from_binary("101").resize(5, true).to_binary() == "11101");
    A1_EXPECT(LogicValue::from_binary("101101").slice(4, 2).to_binary() == "011");
    A1_EXPECT(LogicValue::concat({LogicValue::from_binary("10"),
                                  LogicValue::from_binary("xz")})
                  .to_binary() == "10xz");
    A1_EXPECT(LogicValue::from_binary("10").replicate(3).to_binary() == "101010");
    A1_EXPECT((~LogicValue::from_binary("0z1x")).to_binary() == "1x0x");
    A1_EXPECT((LogicValue::from_binary("z0x1") & LogicValue::from_binary("0101"))
                  .to_binary() == "0001");
    A1_EXPECT((LogicValue::from_binary("z0x1") | LogicValue::from_binary("0100"))
                  .to_binary() == "x1x1");
    A1_EXPECT(LogicValue::from_binary("1111").reduce_and().to_binary() == "1");
    A1_EXPECT(LogicValue::from_binary("1010").reduce_xor().to_binary() == "0");
    A1_EXPECT(LogicValue::add(LogicValue::from_binary("1111"),
                              LogicValue::from_binary("0001"), 4, false)
                  .to_binary() == "0000");
    A1_EXPECT(LogicValue::logical_equal(LogicValue::from_binary("1x"),
                                        LogicValue::from_binary("10"))
                  .to_binary() == "x");
    A1_EXPECT(LogicValue::case_equal(LogicValue::from_binary("1x"),
                                     LogicValue::from_binary("1x"))
                  .to_binary() == "1");
    A1_EXPECT(LogicValue::conditional(LogicValue::from_binary("x"),
                                      LogicValue::from_binary("1010"),
                                      LogicValue::from_binary("1001"))
                  .to_binary() == "10xx");
    A1_EXPECT(LogicValue::from_binary("0000").truth_value() == TruthValue::Zero);
    A1_EXPECT(LogicValue::from_binary("00xz").truth_value() == TruthValue::Unknown);
    A1_EXPECT(LogicValue::from_binary("x1z0").truth_value() == TruthValue::One);
    A1_EXPECT(LogicValue::from_binary("10xz").coerce(4, false, StateDomain::TwoState)
                  .to_binary() == "1000");
    A1_EXPECT(LogicValue::from_binary("10xz").coerce(4, false, StateDomain::FourState)
                  .to_binary() == "10xz");
    A1_EXPECT(LogicValue::z(4).with_slice(1, LogicValue::from_binary("10")).to_binary() ==
              "z10z");
    A1_EXPECT(LogicValue::greater_equal(LogicValue::from_binary("1000"),
                                        LogicValue::from_binary("0001"), true)
                  .to_binary() == "0");
    A1_EXPECT(LogicValue::greater_equal(LogicValue::from_binary("1000"),
                                        LogicValue::from_binary("0001"), false)
                  .to_binary() == "1");
    A1_EXPECT(LogicValue::from_binary("1000")
                  .shift_right(LogicValue::from_binary("01"), 4, true)
                  .to_binary() == "1100");
    return EXIT_SUCCESS;
}
