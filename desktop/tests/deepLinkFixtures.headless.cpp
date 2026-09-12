// Walks the shared deep-link conformance vectors (browser/js/config/fixtures/
// deepLink) against the desktop codec (src/io/deepLink.cpp).
//
// Desktop's module is BUILDERS only, so only telegramStart.json applies:
// encodeTelegramStartPayload against the golden vectors (expectPayload null =
// overflow → empty string here). launchPayload.json pins the RECEIVER-side
// normalizeLaunchPayload, which desktop does not implement — skipped, and said
// so. The historical golden literals in tests/deepLink.headless.cpp stay; this
// walker reads the same vectors from the corpus instead of duplicating them.
#include "deepLink.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

#include "support/check.hpp"
#include "support/fixtureCorpus.hpp"

using namespace stencil::gui;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  std::printf("deepLink/telegramStart:\n");
  bool ok = false;
  const QJsonArray cases =
      readJsonFile(corpusPath("fixtures/deepLink/telegramStart.json"), &ok).array();
  check(ok && !cases.isEmpty(), "telegramStart.json loads");

  int overridden = 0;
  for (const QJsonValue& cv : cases) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString();
    const FixtureOverride ov = findOverride("deepLink", name);
    if (ov.present) ++overridden;
    const QJsonValue expect = ov.present ? ov.verdict : c.value("expectPayload");
    const QString got = deepLink::encodeTelegramStartPayload(
        c.value("serverUrl").toString(), c.value("projectId").toString());
    // null golden = over the 64-char cap; the desktop codec signals that as "".
    const bool pass = expect.isNull() ? got.isEmpty() : got == expect.toString();
    check(pass, qPrintable(name + (ov.present ? QStringLiteral(" [override]") : QString())));
    if (!pass) std::printf("       got: \"%s\"\n", qPrintable(got));
    check(got.size() <= deepLink::TELEGRAM_START_LIMIT, "within the 64-char limit");
  }
  std::printf("  walked %d vectors, %d local overrides\n", int(cases.size()), overridden);
  std::printf("  (skip) launchPayload.json — receiver-side normalizeLaunchPayload; "
              "desktop has builders only\n");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
