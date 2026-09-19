// Single translation unit that provides Doctest's main(). All other *.test.cpp
// files just include the header and register their cases.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstddef>

// Test-count floor: a suite reports "0 failed" when a .test.cpp drops out of the source list.
// Only this TU compiles doctest's implementation, so only here is the registry readable.
static constexpr std::size_t kMinRegisteredCases = 240;

TEST_CASE("suite floor: the core doctest registry has not collapsed") {
  const std::size_t registered = doctest::detail::getRegisteredTests().size();
  CHECK_MESSAGE(registered >= kMinRegisteredCases,
                "core suite collapsed to " << registered << " doctest cases, floor is "
                                           << kMinRegisteredCases);
}
