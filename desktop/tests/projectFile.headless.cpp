// Headless round-trip check for the .stencil format (fileStore::buildProjectFile <-> parseProjectFile), QtCore-only — mirrors the browser projectFile.js round-trip test.
#include "fileStore.hpp"
#include <QCoreApplication>
#include <QJsonObject>
#include <cstdio>

using namespace stencil;
using namespace stencil::gui;

static int failures = 0;
static void check(bool ok, const char* msg) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // A full project (image + layout + metadata + theme) round-trips through build -> parse.
  {
    fileStore::ProjectFileData pf;
    pf.name = "Red Dot";
    pf.color = "#7c3aed";
    pf.keywords = QStringList{"road", "sign"};
    pf.source = "https://example.com/a.png";
    pf.imageExt = "png";
    pf.imageWidth = 4;
    pf.imageHeight = 2;
    pf.imageBytes = QByteArray("\xDE\xAD\xBE\xEF", 4);
    core::Lines lines;
    core::Line l;
    l.points = {{0, 0}, {1, 1}};
    l.color = "#ff0000";
    lines.push_back(l);
    pf.layout = fileStore::buildLayoutJson(4, 2, lines, "bw", "#7c3aed", {}, 1, {});
    pf.hasTheme = true;
    pf.themeMode = "dark";
    pf.themeAccent = "violet";

    const QByteArray bytes = fileStore::buildProjectFile(pf);
    check(bytes.contains("stencil-project"), "format marker present");

    fileStore::ProjectFileData out;
    QString err;
    check(fileStore::parseProjectFile(bytes, out, &err), "round-trip parses");
    check(out.name == "Red Dot", "name round-trips");
    check(out.color == "#7c3aed", "color round-trips");
    check(out.keywords == QStringList({"road", "sign"}), "keywords round-trip");
    check(out.imageBytes == QByteArray("\xDE\xAD\xBE\xEF", 4), "image bytes survive base64");
    check(out.imageWidth == 4 && out.imageHeight == 2, "image dims round-trip");
    check(out.imageExt == "png", "image ext round-trips");

    int w = 0, h = 0;
    core::CropRect crop;
    int rot = 0;
    const core::Lines rl = fileStore::parseLayoutJson(out.layout, w, h, &crop, &rot);
    check(rl.size() == 1, "layout lines round-trip");
    check(out.layout.value("imageFilter").toString() == "bw", "layout filter round-trips");
    check(rot == 1, "rotation round-trips");
    check(out.hasTheme && out.themeMode == "dark" && out.themeAccent == "violet", "theme round-trips");
  }

  // Foreign / malformed / too-new documents are rejected.
  {
    fileStore::ProjectFileData out;
    QString err;
    check(!fileStore::parseProjectFile(QByteArray("{\"version\":1}"), out, &err), "missing format rejected");
    check(!fileStore::parseProjectFile(QByteArray("{ not json"), out, &err), "bad JSON rejected");
    check(!fileStore::parseProjectFile(
              QByteArray("{\"format\":\"stencil-project\",\"version\":999,\"image\":{\"dataUrl\":\"data:image/png;base64,AAAA\"}}"),
              out, &err),
          "too-new version rejected");
    check(!fileStore::parseProjectFile(QByteArray("{\"format\":\"stencil-project\",\"version\":1}"), out, &err),
          "missing image rejected");
  }

  // Empty metadata / no theme is omitted from the file (minimal, diffable output).
  {
    fileStore::ProjectFileData pf;
    pf.name = "Bare";
    pf.imageBytes = QByteArray("\x01\x02", 2);
    pf.imageWidth = 1;
    pf.imageHeight = 1;
    pf.layout = fileStore::buildLayoutJson(1, 1, {}, "none", "#7c3aed", {}, 0, {});
    const QByteArray bytes = fileStore::buildProjectFile(pf);
    check(!bytes.contains("\"color\""), "empty color omitted");
    check(!bytes.contains("\"theme\""), "no theme when hasTheme=false");
    check(!bytes.contains("\"blank\""), "non-blank omits blank flag");
  }

  std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "OK", failures);
  return failures ? 1 : 0;
}
