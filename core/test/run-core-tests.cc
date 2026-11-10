// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Standalone runner: every core test, no simulator (core/CMakeLists.txt,
// ctest). Exits non-zero when any test failed.
#include "ara-core-tests.h"

#include <cstdio>

int main() {
    int failed = 0;
    for (const auto& t : ara::core::test::AllTests()) {
        ara::core::test::Checker c;
        t.run(c);
        for (const auto& f : c.failures())
            std::fprintf(stderr, "FAIL %s: %s\n", t.name, f.c_str());
        std::printf("%s %s\n", c.failures().empty() ? "ok  " : "FAIL", t.name);
        if (!c.failures().empty()) ++failed;
    }
    std::printf("%d failed\n", failed);
    return failed == 0 ? 0 : 1;
}
