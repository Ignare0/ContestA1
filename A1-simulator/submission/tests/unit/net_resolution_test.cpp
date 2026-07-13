#include "test_support.h"
#include "runtime/value.h"

int main() {
    A1_EXPECT(a1::runtime::resolve_net({}).is_z());
    return EXIT_SUCCESS;
}
