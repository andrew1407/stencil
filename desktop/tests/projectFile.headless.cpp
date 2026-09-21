// Headless round-trip check for the .stencil format (fileStore::buildProjectFile <-> parseProjectFile), QtCore-only — mirrors the browser project/file.js round-trip test.
#include "fileStore.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

using namespace stencil;
using namespace stencil::gui;

#include "support/check.hpp"

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
    check(bytes.contains("stencil-project"), "format sentinel present");

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
    check(!bytes.contains("\"chat\""), "no chat block when none was attached (§12.3)");
  }

  // Opt-in chat block (contract §12.3): rides the file when present, round-trips,
  // and stays out of a plain project's bytes.
  {
    fileStore::ProjectFileData pf;
    pf.name = "Chatty";
    pf.imageBytes = QByteArray("\x01\x02", 2);
    pf.imageWidth = 1;
    pf.imageHeight = 1;
    pf.layout = fileStore::buildLayoutJson(1, 1, {}, "none", "#7c3aed", {}, 0, {});
    QJsonArray msgs;
    QJsonObject u;
    u["role"] = "user";
    u["text"] = "make it sepia";
    QJsonObject a;
    a["role"] = "assistant";
    a["text"] = "Sepia applied.";
    msgs.append(u);
    msgs.append(a);
    pf.chat = fileStore::buildChatDoc(msgs, 77);
    const QByteArray bytes = fileStore::buildProjectFile(pf);
    check(bytes.contains("\"chat\""), "chat block written when attached");

    fileStore::ProjectFileData out;
    QString err;
    check(fileStore::parseProjectFile(bytes, out, &err), "chat-bearing file parses");
    const QJsonArray back = fileStore::parseChatDoc(out.chat);
    check(back.size() == 2 && back.at(1).toObject().value("text") == "Sepia applied.",
          "chat messages round-trip through the .stencil file");
  }

  // The projects.json record (projectToJson/projectFromJson) carries the chat
  // the same way: omitted when empty, round-tripped when present.
  {
    Project pr;
    pr.meta.id = "p_1";
    pr.meta.name = "Chatty";
    check(!QJsonDocument(fileStore::projectToJson(pr)).toJson().contains("\"chat\""),
          "plain project record has no chat key");
    QJsonArray msgs;
    QJsonObject u;
    u["role"] = "user";
    u["text"] = "hello";
    msgs.append(u);
    pr.chat = fileStore::buildChatDoc(msgs, 5);
    const Project back = fileStore::projectFromJson(fileStore::projectToJson(pr));
    check(fileStore::parseChatDoc(back.chat).size() == 1,
          "project-record chat round-trips through projects.json");
  }

  std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "OK", failures);
  return failures ? 1 : 0;
}
