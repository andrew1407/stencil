// MainWindow GUI e2e — An accent previewed from the logo's popover under a session skin repaints the
// skin's accent-derived parts live (aqua's gel and gel mark, webcore's ring and pixel mark) and the
// end of the preview puts the worn accent back. Browser twin: ui/accent/shownAccent.js.
#include "../../MainWindow.gui.hpp"
#include "skinPrefs.hpp"
#include "theme.hpp"
#include "../../../src/support/skin/skinAppliers.hpp"
#include "../../../src/support/aqua/sheet.hpp"
#include "../../../src/support/webcore/stylesheet.hpp"

#include <functional>

namespace support = stencil::support;
using stencil::gui::accentPrimary;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static bool shown(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }
  // The header's mark as LogoHoverFx re-takes it on every applyTheme (its button icon is blanked).
  static QImage mark(MainWindow& win) { return win.parts.theme.makeLogoPixmap(32).toImage(); }
  static void seed(MainWindow& win) {
    win.settings.themeMode = QStringLiteral("light");
    win.settings.accentColor = QStringLiteral("blue");
    win.applySettings(win.settings, /*persist=*/false);
  }
  // `sheetFor` rebuilds the expected sheet; it sets the skin accent itself, so it is read first.
  static void previewFollows(MainWindow& win, const std::function<QString(const QString&)>& sheetFor) {
    const QImage worn = mark(win);
    win.parts.theme.previewAccent(QStringLiteral("green"));
    QCOMPARE(support::skinAccent(), accentPrimary(QStringLiteral("green")));
    QVERIFY2(mark(win) != worn, "the skin's mark takes the previewed accent");
    QCOMPARE(qApp->styleSheet(), sheetFor(QStringLiteral("green")));
    win.parts.theme.endAccentPreview();
    QCOMPARE(support::skinAccent(), accentPrimary(QStringLiteral("blue")));
    QCOMPARE(mark(win), worn);
    QCOMPARE(qApp->styleSheet(), sheetFor(QStringLiteral("blue")));
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void cleanup() {
    if (support::isAqua()) { support::setSkin(support::Skin::DEFAULT); support::applyAquaLook(false); }
    if (support::isWebcore()) { support::setSkin(support::Skin::DEFAULT); support::applyWebcoreLook(false); }
    support::clearMotionOverride();
    support::clearForcedDark();
  }

  void aquaGelFollowsAPreviewedAccentAndReverts() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    seed(win);
    win.parts.theme.toggleAqua();
    QVERIFY(support::isAqua());
    previewFollows(win, [](const QString& a) { return support::buildAquaStylesheet(false, a); });
  }

  void webcoreRingFollowsAPreviewedAccentAndReverts() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    seed(win);
    win.parts.theme.toggleWebcore();
    QVERIFY(support::isWebcore());
    seed(win);   // the word set Gray; the preview is measured from Blue
    previewFollows(win, [](const QString& a) { return support::buildWebcoreStylesheet(false, a); });
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.skinPreview.gui.moc"
