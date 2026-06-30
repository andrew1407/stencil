// Headless round-trip check for the per-project accent `color` field
// (fileStore::projectToJson <-> projectFromJson). Verifies the desktop emits and
// reads back the SAME "color" the browser/CLI/server carry, and that it is omitted
// from the JSON when empty (so a plain project's bytes are unchanged). Mirrors the
// browser's projectsStore color persistence and the server ProjectRecord.Color.
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

  // A project with no colour → the "color" key is omitted (byte-stable with old files).
  {
    Project pr;
    pr.meta.id = "p1";
    pr.meta.name = "Bare";
    const QJsonObject o = fileStore::projectToJson(pr);
    check(!o.contains("color"), "empty colour: no color key emitted");
    const Project r = fileStore::projectFromJson(o);
    check(r.meta.color.empty(), "empty colour: round-trips empty");
  }

  // A project WITH a colour → emitted + round-trips unchanged (lower-case "#rrggbb").
  {
    Project pr;
    pr.meta.id = "p2";
    pr.meta.name = "Tinted";
    pr.meta.color = "#3366ff";
    const QJsonObject o = fileStore::projectToJson(pr);
    check(o.value("color").toString() == "#3366ff", "colour emitted as #rrggbb");
    const Project r = fileStore::projectFromJson(o);
    check(r.meta.color == "#3366ff", "colour round-trips");
  }

  // A record missing the key (legacy / no-colour) parses to an empty colour.
  {
    QJsonObject o;
    o["id"] = "p3";
    o["name"] = "Legacy";
    const Project r = fileStore::projectFromJson(o);
    check(r.meta.color.empty(), "absent color key → empty colour");
  }

  std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
