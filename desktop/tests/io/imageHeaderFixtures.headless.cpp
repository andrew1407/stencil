// The shared image-header corpus (browser/js/config/fixtures/imageHeader/cases.json) through the
// desktop's sniffer, io/mediaTypes.cpp sniffImageHeader: a measured case names its format and
// size, a null one leaves the size 0x0. A local override (tests/fixtureOverrides.json,
// "imageHeader/<name>") replaces an expectation with a measured one.
#include "MediaLoader.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

#include "../support/check.hpp"
#include "../support/fixtureCorpus.hpp"

void checkImageHeaderCorpus() {
  std::printf("imageHeader:\n");
  bool ok = false;
  const QJsonArray cases = readJsonFile(corpusPath("fixtures/imageHeader/cases.json"), &ok).array();
  check(ok && cases.size() >= 40, "cases.json loads");

  int overridden = 0;
  for (const QJsonValue& cv : cases) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString();
    const FixtureOverride ov = findOverride("imageHeader", name);
    if (ov.present) ++overridden;
    const QJsonValue expect = ov.present ? ov.verdict : c.value("expect");
    const QByteArray bytes = QByteArray::fromBase64(c.value("base64").toString().toLatin1());
    const stencil::gui::ImageHeader got = stencil::gui::sniffImageHeader(bytes);

    bool pass = false;
    if (expect.isNull()) {
      pass = got.width == 0 && got.height == 0;
    } else {
      const QJsonObject e = expect.toObject();
      pass = got.format == e.value("format").toString() &&
             got.width == quint32(e.value("width").toInteger()) &&
             got.height == quint32(e.value("height").toInteger());
    }
    check(pass, qPrintable(name + (ov.present ? QStringLiteral(" [override]") : QString())));
    if (!pass)
      std::printf("       got: \"%s\" %ux%u\n", qPrintable(got.format), got.width, got.height);
  }
  std::printf("  walked %d cases, %d local overrides\n", int(cases.size()), overridden);
}
