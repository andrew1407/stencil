// The wasmEditApi.cpp exports driven natively through their extern "C" prototypes: the chain
// edits over flat point arrays.
#include "doctest.h"

extern "C" {
  int stencil_chainUnchain(const double*, int, int, double*);
  int stencil_chainPullOut(const double*, int, int, int, int, double, double, double*, int*);
}

TEST_CASE("wasm edit abi: unchain and pull-out over flat point arrays") {
  const double closed[] = {0, 0, 10, 0, 10, 10, 0, 0};
  double out[10] = {};
  CHECK(stencil_chainUnchain(closed, 4, 1, out) == 3);
  CHECK(out[4] == 10);
  CHECK(out[5] == 10);
  CHECK(stencil_chainUnchain(closed, 4, 0, out) == -1);
  int count = 0;
  CHECK(stencil_chainPullOut(closed, 4, 1, 1, 1, 11, 4, out, &count) == 3);
  CHECK(count == 4);
  CHECK(out[0] == 10);
  CHECK(out[6] == 10);
  CHECK(stencil_chainPullOut(closed, 4, 0, 1, 9, 0, 0, out, &count) == -1);
  CHECK(count == 4);
}
