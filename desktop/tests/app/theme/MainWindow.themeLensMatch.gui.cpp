// MainWindow GUI e2e — The theme lens's photograph against the real thing (app/theme/
// ThemePainterLens.cpp; browser twin ui/drag/themeLens.js): the window photographed in the other
// theme matches a grab after a real switch to it, region by region and both ways, with the selected
// line's bar, the points table and the chat dock up. Shared ground is in themeLensGui.hpp.
#include "themeLensGui.hpp"

namespace {
  constexpr int TOLERANCE = 8;   // per channel: the two are the same paint, so only noise may differ

  // Pixels in `r` (window px) whose channels differ by more than TOLERANCE, and where they lie.
  int differing(const QImage& a, const QImage& b, const QRect& r, QRect* where) {
    const qreal dpr = a.devicePixelRatio();
    const QRect px(qRound(r.x() * dpr), qRound(r.y() * dpr), qRound(r.width() * dpr), qRound(r.height() * dpr));
    int n = 0;
    for (int y = px.top(); y <= px.bottom(); ++y)
      for (int x = px.left(); x <= px.right(); ++x) {
        const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
        if (qAbs(p.red() - q.red()) <= TOLERANCE && qAbs(p.green() - q.green()) <= TOLERANCE &&
            qAbs(p.blue() - q.blue()) <= TOLERANCE)
          continue;
        ++n;
        *where |= QRect(qRound(x / dpr), qRound(y / dpr), 1, 1);
      }
    return n;
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static void storeTheme(MainWindow& win, const QString& mode) {
    Settings s = win.settings;
    s.themeMode = mode;
    win.applySettings(s, /*persist=*/true);
  }
  // Pending layouts run and toasts (on a clock of their own) go, so only the theme can differ.
  static void quiet(MainWindow& win) {
    QTest::qWait(60);
    for (const QString& name : {QStringLiteral("toast"), QStringLiteral("toastShine")})
      for (QWidget* toast : win.findChildren<QWidget*>(name)) toast->hide();
  }
  // Every toolbar row, dock and the canvas: the regions a report could name, by what paints them.
  static QList<QPair<QString, QRect>> regions(MainWindow& win) {
    QList<QWidget*> parts;
    for (QToolBar* bar : win.findChildren<QToolBar*>()) parts << bar;
    for (QDockWidget* dock : win.findChildren<QDockWidget*>()) parts << dock;
    parts << win.scroll;
    QList<QPair<QString, QRect>> out;
    for (QWidget* part : parts)
      if (part->isVisible() && !part->isWindow())
        out.append({QString::fromLatin1(part->metaObject()->className()) + QLatin1Char('#') + part->objectName(),
                    QRect(part->mapTo(&win, QPoint(0, 0)), part->size()) & win.rect()});
    out.append({QStringLiteral("the whole window"), win.rect()});
    return out;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void thePhotographMatchesARealSwitchBothWays() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    stencil::core::Line line;
    line.points = {{40, 30}, {180, 110}, {200, 40}};
    canvas->setLines({line});
    canvas->selectLineByIndex(0);
    win.addDockWidget(Qt::RightDockWidgetArea, win.chatDock, Qt::Horizontal);
    win.chatDock->show();
    settle([&win] {
      for (QTableWidget* table : win.findChildren<QTableWidget*>())
        if (table->isVisible() && table->rowCount() >= 3) return win.chatDock->isVisible();
      return false;
    });
    const QString mode = win.settings.themeMode;
    QStringList misses;
    for (int pass = 0; pass < 2; ++pass) {
      quiet(win);
      const QImage lens = win.parts.theme.otherThemeShot().toImage();
      storeTheme(win, win.painted.dark ? QStringLiteral("light") : QStringLiteral("dark"));
      quiet(win);
      const QImage real = win.grab().toImage();
      QCOMPARE(lens.size(), real.size());
      for (const auto& [name, r] : regions(win)) {
        QRect where;
        if (const int n = differing(lens, real, r, &where))
          misses << QStringLiteral("to %1, %2: %3 px in (%4,%5 %6x%7)")
                        .arg(win.painted.dark ? QStringLiteral("dark") : QStringLiteral("light"), name)
                        .arg(n).arg(where.x()).arg(where.y()).arg(where.width()).arg(where.height());
      }
    }
    storeTheme(win, mode);
    QVERIFY2(misses.isEmpty(), qPrintable(misses.join(QStringLiteral("; "))));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeLensMatch.gui.moc"
