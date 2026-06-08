// doctest entry point for the Dungeon Grid Arena test suite.
//
// This single translation unit defines the doctest main() via
// DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN. All other test translation units
// (test_world.cpp, test_combat.cpp, ...) include "doctest.h" WITHOUT the
// implementation macro, so the framework is compiled exactly once here.
//
// The suite is intentionally runnable with zero registered tests: doctest
// reports success when no test cases exist, which keeps the `tests` target
// green while the project is scaffolded out task by task.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
