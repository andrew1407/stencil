// MainWindow GUI e2e — The toolbar's sections: their order, the Share and Script buttons, and wrapping.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The Share button exists only where the OS has a share sheet of its own — hidden on
  // Linux, where none does. Browser parity: supportsShareFiles() (utils.js) keeps
  // #share-image display:none on every browser that turns files down, rather than
  // offering a button that can only apologise.
  void shareButtonOnlyWhereTheOsShares() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());   // the IMAGE cluster shows its icons
    settleLayout(&win, 120);

    const bool shares = stencil::support::isShareSheetAvailable();
    QCOMPARE(win.actShareImage_->isVisible(), shares);
    QToolButton* btn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == win.actShareImage_) btn = b;
    QVERIFY2(btn, "the Share action has no toolbar button at all");
    QCOMPARE(btn->isVisible(), shares);
    if (shares) {
      QVERIFY(win.actShareImage_->isEnabled());
      return;
    }
    // …and the chord is dead with it: Qt will not enable an invisible action, so the
    // refresh that follows every image load cannot bring it back.
    QVERIFY2(!win.actShareImage_->isEnabled(), "the Share chord is live with no share sheet");
    win.actShareImage_->setEnabled(true);
    QVERIFY2(!win.actShareImage_->isEnabled(), "an invisible Share action took enabling");
  }
  // The script window's button opens the DATA section (browser toolbar.js puts #script-btn
  // first there) and carries the shared registry's chord. Its dialog is exec()'d, so this
  // checks the wiring, not the window.
  void scriptButtonOpensTheDataSection() {
    MainWindow win(nullptr, false);
    win.resize(1400, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QWidget* data = nullptr;
    for (QLabel* l : win.findChildren<QLabel*>("sectionLabel"))
      if (l->text() == QLatin1String("DATA")) data = l->parentWidget();
    QVERIFY2(data, "no DATA section on the toolbar");
    QList<QAction*> got;
    for (QToolButton* b : data->findChildren<QToolButton*>())
      if (b->defaultAction()) got << b->defaultAction();
    QVERIFY2(!got.isEmpty(), "the DATA section has no buttons");
    QCOMPARE(got.first(), win.actScript_);

    QCOMPARE(win.actScript_->shortcut(), QKeySequence(win.hotkey("openScript", "Alt+Shift+S")));
    QMenu* dataMenu = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>())
      if (m->actions().contains(win.actScript_)) dataMenu = m;
    QVERIFY2(dataMenu, "the script action is not in any menu");

    // A script can open its OWN source, so the window is live with no image loaded.
    QVERIFY2(win.actScript_->isEnabled(), "the script window is gated on an image");
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY2(win.actScript_->isEnabled(), "the script window went dead once an image loaded");
    beat();
  }
  // ── The toolbar's clusters, in the browser's order ──────────────────────────
  // One sequence across both surfaces (browser js/ui/toolbar.js, pinned there by
  // ui-markup.test.js): Image · Description & attributes · Projects · Connections & chat ·
  // Edit / Line · Point / Draw · View / Zoom · Page · Formula · Data · Settings. The rows
  // are where this app's non-wrapping toolbars break that one sequence, so the check is
  // the concatenation of the rows top to bottom.
  void toolbarSectionsFollowTheBrowsersOrder() {
    MainWindow win(nullptr, false);
    win.resize(1600, 950);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 200);
    QList<QToolBar*> bars = win.findChildren<QToolBar*>();
    std::sort(bars.begin(), bars.end(), [](QToolBar* a, QToolBar* b) {
      return a->mapTo(a->window(), QPoint(0, 0)).y() < b->mapTo(b->window(), QPoint(0, 0)).y();
    });
    QStringList sections;
    for (QToolBar* tb : bars)
      for (QLabel* l : tb->findChildren<QLabel*>())
        if (l->objectName() == QLatin1String("sectionLabel")) sections << l->text();
    const QStringList want{"IMAGE", "DESCRIPTION & ATTRIBUTES", "PROJECTS",
                           "CONNECTIONS & CHAT", "EDIT", "LINE", "POINT", "DRAW", "VIEW",
                           "ZOOM", "PAGE", "FORMULA", "DATA", "SETTINGS"};
    QCOMPARE(sections, want);
    // Where the rows BREAK that sequence is a packing decision — the browser re-wraps the
    // same run with the window and a QToolBar cannot — so every row has to survive a narrow
    // window on its own. With the formula fields showing and the widest page state chosen,
    // none of them may fall back on QToolBar's "»", which is how SETTINGS once vanished.
    win.allowFormulas_->setChecked(true);
    const int custom = win.units_.pageSize->findData(QStringLiteral("custom"));
    QVERIFY(custom >= 0);
    const int a3 = win.units_.pageSize->findData(QStringLiteral("A3"));
    QVERIFY(a3 >= 0);
    win.units_.pageSize->setCurrentIndex(a3);   // the everyday state, whatever the settings hold
    settleLayout(&win, 150);
    for (const int width : {1400, 1100, 1000}) {
      win.resize(width, 950);
      settleLayout(&win, 250);
      for (QToolBar* tb : bars) {
        if (tb->objectName() == QLatin1String("headerToolbar")) continue;
        for (QWidget* c : tb->findChildren<QWidget*>())
          if (c->metaObject()->className() == QLatin1String("QToolBarExtension"))
            QVERIFY2(!c->isVisible(),
                     qPrintable(QString("at %1px the %2 row overflows into \"»\"")
                                    .arg(width).arg(tb->objectName())));
      }
    }
    // …and once more with the custom page's W × H boxes out — they add ~160px to the PAGE
    // cluster (squeezable, but only so far), so that state is checked one step wider.
    win.units_.pageSize->setCurrentIndex(custom);
    settleLayout(&win, 150);
    for (const int width : {1400, 1100}) {
      win.resize(width, 950);
      settleLayout(&win, 250);
      for (QToolBar* tb : bars) {
        if (tb->objectName() == QLatin1String("headerToolbar")) continue;
        for (QWidget* c : tb->findChildren<QWidget*>())
          if (c->metaObject()->className() == QLatin1String("QToolBarExtension"))
            QVERIFY2(!c->isVisible(),
                     qPrintable(QString("at %1px (custom page) the %2 row overflows into \"»\"")
                                    .arg(width).arg(tb->objectName())));
      }
    }
    win.units_.pageSize->setCurrentIndex(a3);   // this suite shares the real settings file
  }
  // A squeezed window WRAPS its tool row (support/WrapRow.hpp) — the browser's flex-wrap.
  // QToolBar's own answer is the "»" overflow, where a widget action is not drawn at all.
  void narrowToolbarRowsWrapInsteadOfLosingSections() {
    MainWindow win;
    win.resize(1500, 950);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 200);

    QStringList captions;
    for (QLabel* l : win.findChildren<QLabel*>("sectionLabel"))
      if (l->isVisible()) captions << l->text();
    QVERIFY2(captions.contains("EDIT"), "the wide window shows EDIT to begin with");
    QToolBar* main = win.findChild<QToolBar*>("mainToolbar");
    QVERIFY(main);
    const int oneLine = main->height();

    for (const int width : {1100, 900, 760}) {
      win.resize(width, 950);
      settleLayout(&win, 300);
      for (QLabel* l : win.findChildren<QLabel*>("sectionLabel")) {
        if (!captions.contains(l->text())) continue;
        QVERIFY2(l->isVisible(),
                 qPrintable(QString("at %1px the %2 section is gone").arg(width).arg(l->text())));
        const QPoint tl = l->mapTo(&win, QPoint(0, 0));
        QVERIFY2(tl.x() >= 0 && tl.x() < win.width(),
                 qPrintable(QString("at %1px %2 sits off the window at x=%3")
                                .arg(width).arg(l->text()).arg(tl.x())));
      }
      // …and no row falls back on the overflow button to get there.
      for (QToolBar* tb : win.findChildren<QToolBar*>()) {
        if (tb->objectName() == QLatin1String("headerToolbar")) continue;
        for (QWidget* c : tb->findChildren<QWidget*>())
          if (c->metaObject()->className() == QLatin1String("QToolBarExtension"))
            QVERIFY2(!c->isVisible(),
                     qPrintable(QString("at %1px the %2 row overflows into \"»\"")
                                    .arg(width).arg(tb->objectName())));
      }
    }
    // …and each hairline runs the full height of its line (browser .ctrl-sep stretch).
    for (QFrame* sep : win.findChildren<QFrame*>("toolWrapSep")) {
      if (!sep->isVisible()) continue;
      const int y = sep->mapTo(&win, QPoint(0, 0)).y();
      int tallest = 0;
      for (QLabel* l : win.findChildren<QLabel*>("sectionLabel")) {
        QWidget* sect = l->parentWidget();
        if (!sect || !sect->isVisible()) continue;
        if (qAbs(sect->mapTo(&win, QPoint(0, 0)).y() - y) > 4) continue;   // another line
        tallest = qMax(tallest, sect->height());
      }
      if (tallest <= 0) continue;
      QVERIFY2(sep->height() >= tallest,
               qPrintable(QString("a divider stops %1px short of its line (%2 vs %3)")
                              .arg(tallest - sep->height()).arg(sep->height()).arg(tallest)));
    }
    // Wrapped, not merely squeezed: the row that no longer fits is TALLER, because the
    // cluster that fell off the end went onto a second line.
    QVERIFY2(main->height() > oneLine,
             qPrintable(QString("the main row never wrapped: %1px at both widths").arg(oneLine)));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbar.gui.moc"
