// Headless round-trip check for the server-layout page format + x/y formulas
// (fileStore::buildLayoutJson <-> parseLayoutMeta). Verifies the desktop emits and reads back
// the SAME pageSize/customPage*/allowFormulas/formula* the browser + CLI carry, and that those
// fields are omitted when at their defaults (so plain file exports stay byte-stable). Mirrors
// the browser tests/layout.test.js and the CLI server.zig "round-trips page format" test.
// Not part of stencil_tests (that target is Qt-free); built only when Qt is present.
#include "fileStore.hpp"
#include <QCoreApplication>
#include <QJsonObject>
#include <cstdio>

using namespace stencil::gui;

static int failures = 0;
static void check(bool ok, const char* msg) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const stencil::core::Lines noLines;

  // Default meta → no page/formula keys, so a file export's bytes are unchanged.
  {
    const QJsonObject o = fileStore::buildLayoutJson(10, 20, noLines);
    check(!o.contains("pageSize"), "bare: no pageSize key");
    check(!o.contains("customPageWidth"), "bare: no customPageWidth key");
    check(!o.contains("allowFormulas"), "bare: no allowFormulas key");
    check(!o.contains("formulaX"), "bare: no formulaX key");
  }

  // A custom page + x/y formulas survive build -> parse unchanged.
  {
    fileStore::LayoutMeta m;
    m.pageSize = "custom";
    m.customPageWidth = 15.0;
    m.customPageHeight = 25.0;
    m.allowFormulas = true;
    m.formulaX = "x*2";
    m.formulaY = "y+1";
    const QJsonObject o = fileStore::buildLayoutJson(
        5, 6, noLines, "none", "#7c3aed", stencil::core::CropRect{}, 0, m);
    const fileStore::LayoutMeta r = fileStore::parseLayoutMeta(o);
    check(r.pageSize == "custom", "custom pageSize round-trips");
    check(r.customPageWidth == 15.0, "customPageWidth round-trips");
    check(r.customPageHeight == 25.0, "customPageHeight round-trips");
    check(r.allowFormulas, "allowFormulas round-trips");
    check(r.formulaX == "x*2", "formulaX round-trips");
    check(r.formulaY == "y+1", "formulaY round-trips");
  }

  // A named page with formulas off: pageSize kept, but no formula keys are emitted
  // (allowFormulas:false and empty expressions are the defaults → omitted).
  {
    fileStore::LayoutMeta m;
    m.pageSize = "A4";
    m.customPageWidth = 21.0;
    m.customPageHeight = 29.7;
    const QJsonObject o = fileStore::buildLayoutJson(
        1, 1, noLines, "none", "#7c3aed", stencil::core::CropRect{}, 0, m);
    check(o.value("pageSize").toString() == "A4", "A4 pageSize emitted");
    check(o.value("customPageWidth").toDouble() == 21.0, "A4 customPageWidth emitted");
    check(!o.contains("allowFormulas"), "formulas-off: no allowFormulas key");
    check(!o.contains("formulaX"), "formulas-off: no formulaX key");
  }

  // A B-series name rides the same pageSize field unchanged — the envelope is
  // name-agnostic, so the full ISO A/B/C table needs no fileStore changes.
  {
    fileStore::LayoutMeta m;
    m.pageSize = "B5";
    const QJsonObject o = fileStore::buildLayoutJson(
        1, 1, noLines, "none", "#7c3aed", stencil::core::CropRect{}, 0, m);
    check(o.value("pageSize").toString() == "B5", "B5 pageSize emitted");
    const fileStore::LayoutMeta r = fileStore::parseLayoutMeta(o);
    check(r.pageSize == "B5", "B5 pageSize round-trips");
  }

  std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
