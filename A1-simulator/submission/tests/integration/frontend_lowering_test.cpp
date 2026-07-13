#include "../unit/test_support.h"
#include "frontend/frontend.h"

int main() {
    const a1::frontend::ProjectSpec spec;
    A1_EXPECT(spec.top.empty());
    return EXIT_SUCCESS;
}
