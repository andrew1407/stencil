// The walkers themselves: chatDoc (§12.1 chat documents), layout (the envelope plus tolerant line
// parsing) and stencilProject (.stencil files), each against the real desktop io/fileStore.
#include "storeFixturesParts.hpp"


QJsonArray loadCases(const char* rel) {
  bool ok = false;
  const QJsonArray cases = readJsonFile(corpusPath(rel), &ok).array();
  check(ok && !cases.isEmpty(), qPrintable(QStringLiteral("%1 loads").arg(rel)));
  return cases;
}

QString taggedName(const QString& name, const FixtureOverride& ov) {
  return ov.present ? name + QStringLiteral(" [override]") : name;
}
// ── chatDoc ──────────────────────────────────────────────────────────────

int walkChatDoc() {
  int overridden = 0;
  std::printf("chatDoc/roundtrip:\n");
  for (const QJsonValue& cv : loadCases("llm/fixtures/chatDoc/roundtrip.json")) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString();
    const FixtureOverride ov = findOverride("chatDoc", name);
    if (ov.present) ++overridden;
    const QJsonObject doc = c.value("doc").toObject();
    const QJsonArray msgs = fileStore::parseChatDoc(doc);
    const QJsonObject rebuilt =
        fileStore::buildChatDoc(msgs, qint64(doc.value("savedAt").toDouble()));
    const QJsonValue want = ov.present ? ov.verdict : QJsonValue(doc);
    checkJsonEq(rebuilt, want,
                QStringLiteral("%1: serialize(parse(doc)) == doc").arg(taggedName(name, ov)));
  }

  std::printf("chatDoc/tolerance (messages half; savedAt has no desktop read path):\n");
  for (const QJsonValue& cv : loadCases("llm/fixtures/chatDoc/tolerance.json")) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString();
    const FixtureOverride ov = findOverride("chatDoc", name);
    if (ov.present) ++overridden;
    // Object input, or the raw-string form for malformed-JSON cases; anything
    // that is not a JSON object reads as an empty doc (→ no messages).
    QJsonObject doc;
    if (c.contains("docString")) {
      const QJsonDocument d =
          QJsonDocument::fromJson(c.value("docString").toString().toUtf8());
      if (d.isObject()) doc = d.object();
    } else if (c.value("doc").isObject()) {
      doc = c.value("doc").toObject();
    }
    const QJsonArray got = fileStore::parseChatDoc(doc);
    // expectParsed null = "missing document" — desktop's signal is no messages.
    const QJsonValue ep = c.value("expectParsed");
    const QJsonValue want = ov.present
                                ? ov.verdict
                                : QJsonValue(ep.isObject()
                                                 ? ep.toObject().value("messages").toArray()
                                                 : QJsonArray());
    checkJsonEq(got, want, QStringLiteral("%1").arg(taggedName(name, ov)));
  }
  return overridden;
}

// ── layout ───────────────────────────────────────────────────────────────

// A core::Line with the shared cross-surface defaults filled in — the
// expectFilled shape (pointColor always present; "" = inherit the stroke).
QJsonObject filledLineJson(const core::Line& l) {
  QJsonArray pts;
  for (const auto& p : l.points)
    pts.append(QJsonObject{{"x", p.x}, {"y", p.y}});
  return QJsonObject{{"points", pts},
                     {"color", QString::fromStdString(l.color)},
                     {"thickness", l.thickness},
                     {"pointSize", l.pointSize},
                     {"style", QString::fromStdString(l.style)},
                     {"locked", l.locked},
                     {"fillColor", QString::fromStdString(l.fillColor)},
                     {"pointColor", QString::fromStdString(l.pointColor)}};
}

int walkLayout() {
  int overridden = 0;
  std::printf("layout/payload (desktop buildLayoutJson from the vector's export inputs):\n");
  for (const QJsonValue& cv : loadCases("fixtures/layout/payload.json")) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString();
    const FixtureOverride ov = findOverride("layout", name);
    if (ov.present) ++overridden;
    const QJsonObject L = c.value("layout").toObject();
    core::CropRect crop{};
    if (L.value("cropRect").isObject()) {
      const QJsonObject cr = L.value("cropRect").toObject();
      crop = {cr.value("x").toDouble(), cr.value("y").toDouble(),
              cr.value("w").toDouble(), cr.value("h").toDouble()};
    }
    fileStore::LayoutMeta meta;
    meta.pageSize = L.value("pageSize").toString();
    meta.customPageWidth = L.value("customPageWidth").toDouble(0);
    meta.customPageHeight = L.value("customPageHeight").toDouble(0);
    meta.allowFormulas = L.value("allowFormulas").toBool(false);
    meta.formulaX = L.value("formulaX").toString();
    meta.formulaY = L.value("formulaY").toString();
    const QJsonObject got = fileStore::buildLayoutJson(
        L.value("imageWidth").toInt(0), L.value("imageHeight").toInt(0),
        fileStore::linesFromJson(L.value("lines").toArray()),
        L.value("imageFilter").isString() ? L.value("imageFilter").toString()
                                          : QStringLiteral("none"),
        L.value("filterColor").isString() ? L.value("filterColor").toString()
                                          : QStringLiteral("#7c3aed"),
        crop, L.value("rotationQuarters").toInt(0), meta);
    const QJsonValue want =
        ov.present ? ov.verdict : QJsonValue(c.value("expectPayload").toObject());
    checkJsonEq(got, want, QStringLiteral("%1").arg(taggedName(name, ov)));
  }

  std::printf("layout/sparse (linesFromJson vs the cross-surface expectFilled):\n");
  for (const QJsonValue& cv : loadCases("fixtures/layout/sparse.json")) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString();
    const FixtureOverride ov = findOverride("layout", name);
    if (ov.present) ++overridden;
    const core::Lines lines =
        c.value("sparse").isArray()
            ? fileStore::linesFromJson(c.value("sparse").toArray())
            : core::Lines{};
    QJsonArray got;
    for (const core::Line& l : lines) got.append(filledLineJson(l));
    const QJsonValue want =
        ov.present ? ov.verdict : QJsonValue(c.value("expectFilled").toArray());
    checkJsonEq(got, want, QStringLiteral("%1").arg(taggedName(name, ov)));
  }
  return overridden;
}

// ── stencilProject ───────────────────────────────────────────────────────

// Desktop's parse result projected onto the fixtures' normalized-project
// shape (image bytes re-encoded so the embedded payload is compared too).
QJsonObject projectProjection(const fileStore::ProjectFileData& p) {
  QJsonObject theme;
  if (p.hasTheme) {
    if (!p.themeMode.isEmpty()) theme["mode"] = p.themeMode;
    if (!p.themeAccent.isEmpty()) theme["accent"] = p.themeAccent;
  }
  return QJsonObject{{"name", p.name},
                     {"color", p.color},
                     {"keywords", QJsonArray::fromStringList(p.keywords)},
                     {"source", p.source},
                     {"resource", p.resource},
                     {"blank", p.blank},
                     {"blankColor", p.blankColor},
                     {"ext", p.imageExt},
                     {"w", p.imageWidth},
                     {"h", p.imageHeight},
                     {"imageB64", QString::fromLatin1(p.imageBytes.toBase64())},
                     {"layout", p.layout},
                     {"theme", p.hasTheme ? QJsonValue(theme) : QJsonValue()}};
}

// The fixture's expected project, projected the same way.
QJsonObject expectedProjection(const QJsonObject& pr) {
  const QJsonObject img = pr.value("image").toObject();
  const QString dataUrl = img.value("dataUrl").toString();
  const int b64 = dataUrl.indexOf("base64,");
  const QByteArray bytes =
      b64 < 0 ? QByteArray() : QByteArray::fromBase64(dataUrl.mid(b64 + 7).toLatin1());
  return QJsonObject{{"name", pr.value("name")},
                     {"color", pr.value("color")},
                     {"keywords", pr.value("keywords")},
                     {"source", pr.value("source")},
                     {"resource", pr.value("resource")},
                     {"blank", pr.value("blank")},
                     {"blankColor", pr.value("blankColor")},
                     {"ext", img.value("ext")},
                     {"w", img.value("w").toInt(0)},
                     {"h", img.value("h").toInt(0)},
                     {"imageB64", QString::fromLatin1(bytes.toBase64())},
                     {"layout", pr.value("layout")},
                     {"theme", pr.value("theme")}};
}

int walkStencilProject(const char* rel) {
  int overridden = 0;
  std::printf("stencilProject/%s:\n", rel);
  for (const QJsonValue& cv : loadCases(
           (QByteArrayLiteral("fixtures/stencilProject/") + rel).constData())) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString();
    const FixtureOverride ov = findOverride("stencilProject", name);
    const QJsonObject ovv = ov.verdict.toObject();
    if (ov.present) ++overridden;

    fileStore::ProjectFileData out;
    QString err;
    const bool ok =
        fileStore::parseProjectFile(c.value("file").toString().toUtf8(), out, &err);
    // errorIncludes strings are browser messages — the verdict (CASE) is what
    // desktop matches, per the corpus schema.
    const QString want = ovv.contains("expect") ? ovv.value("expect").toString()
                                                : c.value("expect").toString();
    check(ok == (want == "ok"),
          qPrintable(QStringLiteral("%1 -> %2").arg(taggedName(name, ov), want)));
    if (ok != (want == "ok")) {
      std::printf("       desktop said %s (%s)\n", ok ? "ok" : "error", qPrintable(err));
      continue;
    }
    if (!ok) continue;
    QJsonObject expected = expectedProjection(c.value("project").toObject());
    const QJsonObject patch = ovv.value("projectPatch").toObject();
    for (auto it = patch.begin(); it != patch.end(); ++it) expected[it.key()] = it.value();
    checkJsonEq(projectProjection(out), expected,
                QStringLiteral("%1: parsed project").arg(taggedName(name, ov)));
  }
  return overridden;
}

