#include <gtest/gtest.h>

#include "Application.hpp"

// Application's constructor must be cheap — no logger init, no engine, no
// file I/O. Those only fire inside run(). This keeps the shell usable from
// tests without starting a real engine.
TEST(Application, ConstructsWithArgv) {
  const char* argv[] = {"journeyman_engine"};
  Application app(1, const_cast<char**>(argv));
  SUCCEED();
}
