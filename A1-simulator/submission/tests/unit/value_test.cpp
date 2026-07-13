#include "test_support.h"
#include "runtime/value.h"

int main() {
    const auto value = a1::runtime::LogicValue::from_binary("1");
    A1_EXPECT(value.width() == 1);
    return EXIT_SUCCESS;
}
