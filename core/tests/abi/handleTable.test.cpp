// abi::HandleTable: ids are positive ints that never overflow, and wrap past INT_MAX without
// handing out one still held.
#include "HandleTable.hpp"

#include <climits>
#include <cstdint>

#include "doctest.h"

using stencil::core::abi::HandleTable;

TEST_CASE("HandleTable: ids start at 1; zero, negative and unknown ids look up to nothing") {
  HandleTable<int> t;
  const int a = t.create(7);
  CHECK(a == 1);
  CHECK(*t.get(a) == 7);
  CHECK(t.get(0) == nullptr);
  CHECK(t.get(-1) == nullptr);
  CHECK(t.get(a + 1) == nullptr);
  t.destroy(a);
  CHECK(t.get(a) == nullptr);
}

TEST_CASE("HandleTable: past INT_MAX the ids wrap to 1, skipping one still held") {
  HandleTable<int> t(static_cast<std::uint64_t>(INT_MAX) - 1);
  CHECK(t.create(1) == INT_MAX);
  CHECK(t.create(2) == 1);

  // 2^64 - 1 is 3 mod INT_MAX, so id 4 comes first, the counter wraps, and 4 is skipped.
  HandleTable<int> u(UINT64_MAX);
  CHECK(u.create(0) == 4);
  CHECK(u.create(0) == 1);
  CHECK(u.create(0) == 2);
  CHECK(u.create(0) == 3);
  CHECK(u.create(0) == 5);
}
