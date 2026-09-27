// Port of browser/tests/core/editorMemento.test.js: the history over editor mementos, where a
// crop or a turn is one undo step and the step -1 stop keeps the view it started from.
#include "doctest.h"
#include "HistoryStack.hpp"

using namespace stencil::core;

namespace {
  EditorMemento at(double cropX, int quarters, int lineCount) {
    EditorMemento m;
    m.hasView = true;
    m.crop = CropRect{cropX, 0, 50, 40};
    m.quarters = quarters;
    for (int i = 0; i < lineCount; ++i) {
      Line l;
      l.points = {{static_cast<double>(i), 1}};
      m.lines.push_back(l);
    }
    return m;
  }
}  // namespace

TEST_CASE("editor history: a crop and a turn step back and forth with the lines") {
  EditorHistory h;
  h.reset(at(0, 0, 0));
  CHECK_FALSE(h.canUndo());
  h.push(at(0, 0, 1));   // a line
  h.push(at(7, 1, 1));   // a crop and a turn
  std::optional<EditorMemento> u = h.undo();
  REQUIRE(u.has_value());
  CHECK(u->crop.x == 0);
  CHECK(u->quarters == 0);
  CHECK(u->lines.size() == 1);
  u = h.undo();
  REQUIRE(u.has_value());
  CHECK(u->lines.empty());
  CHECK(u->hasView);
  CHECK(u->crop.x == 0);
  CHECK_FALSE(h.undo().has_value());
  CHECK(h.redo()->lines.size() == 1);
  const std::optional<EditorMemento> r = h.redo();
  CHECK(r->crop.x == 7);
  CHECK(r->quarters == 1);
}

TEST_CASE("editor history: a crop on a fresh image is undoable") {
  EditorHistory h;
  h.reset(at(0, 0, 0));
  h.push(at(9, 0, 0));
  CHECK(h.canUndo());
  const std::optional<EditorMemento> u = h.undo();
  CHECK(u->crop.x == 0);
  CHECK(h.step() == -1);
}

TEST_CASE("editor history: the floor clears the lines but keeps the view the stack started on") {
  EditorHistory h;
  h.reset(at(3, 2, 2), 0);
  h.push(at(8, 2, 2));
  h.undo();
  const std::optional<EditorMemento> f = h.undo();
  CHECK(f->lines.empty());
  CHECK(f->crop.x == 3);
  CHECK(f->quarters == 2);
}

TEST_CASE("editor history: past MAX_STEPS the floor takes the view of the last step dropped") {
  EditorHistory h;
  h.reset(at(-1, 0, 0));
  for (int i = 0; i < 70; ++i) h.push(at(i, 0, 1));
  CHECK(h.size() == EditorHistory::MAX_STEPS);
  for (int i = 0; i < 63; ++i) h.undo();
  CHECK(h.step() == 0);
  const std::optional<EditorMemento> f = h.undo();
  CHECK(f->crop.x == 5);
  CHECK(f->lines.empty());
}

TEST_CASE("editor history: a step without a view, and the filter, come back as they went in") {
  EditorHistory h;
  EditorMemento bare;
  bare.lines = at(0, 0, 1).lines;
  EditorMemento tinted = at(2, 0, 1);
  tinted.filter = "custom";
  tinted.filterColor = "#7c3aed";
  h.push(bare);
  h.push(tinted);
  CHECK_FALSE(h.undo()->hasView);
  const std::optional<EditorMemento> r = h.redo();
  CHECK(r->filter == "custom");
  CHECK(r->filterColor == "#7c3aed");
}
