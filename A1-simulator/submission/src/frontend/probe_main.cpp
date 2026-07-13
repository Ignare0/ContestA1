#include <cstdio>

#include "slang/util/VersionInfo.h"

int main() {
    std::printf("a1 ir_probe using slang %s\n",
                slang::VersionInfo::getVersionString().c_str());
    return 0;
}
