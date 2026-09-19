// Headless round-trip check for the per-project `keywords` field (fileStore::projectToJson <->
// projectFromJson): the desktop emits and reads back the SAME list the browser/CLI/server carry, omits
// it when empty (byte-stable with old files), and drops blanks on read. Mirrors the browser's
// projectsStore keywords persistence and the server ProjectRecord.Keywords. Needs Qt.
#include "fileStore.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

using namespace stencil::gui;

#include "support/check.hpp"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // No keywords → the "keywords" key is omitted (byte-stable with old files).
  {
    Project pr;
    pr.meta.id = "p1";
    pr.meta.name = "Bare";
    const QJsonObject o = fileStore::projectToJson(pr);
    check(!o.contains("keywords"), "empty keywords: no keywords key emitted");
    const Project r = fileStore::projectFromJson(o);
    check(r.meta.keywords.empty(), "empty keywords: round-trips empty");
  }

  // WITH keywords → emitted as a JSON array + round-trips in order.
  {
    Project pr;
    pr.meta.id = "p2";
    pr.meta.name = "Tagged";
    pr.meta.keywords = {"alpha", "beta", "gamma"};
    const QJsonObject o = fileStore::projectToJson(pr);
    const QJsonArray arr = o.value("keywords").toArray();
    check(arr.size() == 3 && arr[0].toString() == "alpha" && arr[2].toString() == "gamma",
          "keywords emitted as an ordered array");
    const Project r = fileStore::projectFromJson(o);
    check(r.meta.keywords.size() == 3 && r.meta.keywords[0] == "alpha" && r.meta.keywords[2] == "gamma",
          "keywords round-trip in order");
  }

  // Blank entries are dropped on read (defensive against hand-edited files).
  {
    QJsonObject o;
    o["id"] = "p3";
    o["name"] = "Messy";
    QJsonArray kw;
    kw.append("keep");
    kw.append("");
    o["keywords"] = kw;
    const Project r = fileStore::projectFromJson(o);
    check(r.meta.keywords.size() == 1 && r.meta.keywords[0] == "keep", "blank keywords dropped on read");
  }

  // A record missing the key (legacy) parses to no keywords.
  {
    QJsonObject o;
    o["id"] = "p4";
    o["name"] = "Legacy";
    const Project r = fileStore::projectFromJson(o);
    check(r.meta.keywords.empty(), "absent keywords key -> empty");
  }

  std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
