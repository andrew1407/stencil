// The memento half of wasmHistoryApi.cpp driven natively: a step's view and filter go in beside
// its lines and come back out through readView / readFilter; a lines-only step has none.
#include "doctest.h"

#include <cstdint>
#include <cstring>

extern "C" {
  int stencil_history_create(void);
  void stencil_history_destroy(int);
  void stencil_history_resetMemento(int, int, int, const double*, int, const std::uint8_t*, int,
                                    const double*, const char*, const char*);
  void stencil_history_pushMemento(int, const double*, int, const std::uint8_t*, int,
                                   const double*, const char*, const char*);
  void stencil_history_push(int, const double*, int, const std::uint8_t*, int);
  int stencil_history_undo(int, int*);
  int stencil_history_redo(int, int*);
  int stencil_history_readView(int, double*);
  const char* stencil_history_readFilter(int, int);
}

TEST_CASE("wasm history abi: a memento's view and filter round-trip; a lines step has none") {
  const int h = stencil_history_create();
  const double none[] = {0};
  const std::uint8_t text[] = {0};
  const double viewA[] = {1, 2, 30, 40, 0, 0}, viewB[] = {5, 6, 70, 80, 3, 1};
  stencil_history_resetMemento(h, 0, 0, none, 1, text, 0, viewA, "", "");
  stencil_history_pushMemento(h, none, 1, text, 0, viewB, "custom", "#7c3aed");
  stencil_history_push(h, none, 1, text, 0);
  int sizes[2] = {};
  double out[6] = {};
  REQUIRE(stencil_history_undo(h, sizes) == 1);
  CHECK(stencil_history_readView(h, out) == 1);
  CHECK(out[0] == 5);
  CHECK(out[4] == 3);
  CHECK(out[5] == 1);
  CHECK(std::strcmp(stencil_history_readFilter(h, 0), "custom") == 0);
  CHECK(std::strcmp(stencil_history_readFilter(h, 1), "#7c3aed") == 0);
  REQUIRE(stencil_history_undo(h, sizes) == 1);   // the floor: the reset view, no lines
  CHECK(stencil_history_readView(h, out) == 1);
  CHECK(out[2] == 30);
  CHECK(out[5] == 0);
  CHECK(stencil_history_readFilter(h, 0)[0] == '\0');
  stencil_history_redo(h, sizes);
  REQUIRE(stencil_history_redo(h, sizes) == 1);
  CHECK(stencil_history_readView(h, out) == 0);
  stencil_history_destroy(h);
  CHECK(stencil_history_readView(h, out) == 0);
  CHECK(stencil_history_readFilter(h, 0)[0] == '\0');
}
