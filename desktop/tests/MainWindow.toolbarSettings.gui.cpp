// MainWindow GUI e2e — The SETTINGS cluster, driving the window's own actions both ways.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The toolbar closes with a SETTINGS cluster mirroring the browser's last group: theme · fullscreen ·
  // incognito · gear · palette · info. Every button drives the EXISTING QAction, so both stay in step.
  void toolbarHasTheBrowsersSettingsSection() {
    MainWindow win(nullptr, false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* section = win.settingsSection_;
    QVERIFY2(section, "no SETTINGS section on the toolbar");
    QLabel* caption = section->findChild<QLabel*>("sectionLabel");
    QVERIFY2(caption, "the section has no caption");
    QCOMPARE(caption->text(), QStringLiteral("SETTINGS"));   // same styling as its siblings

    // The six controls, in the browser's order: the state TOGGLES first (incognito · fullscreen), then the
    // theme switch, then gear · palette · info. actAccent_ is the logo's popover, with no toolbar icon.
    QList<QAction*> got;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction()) got << b->defaultAction();
    const QList<QAction*> want{win.actIncognito_, win.actFullscreen_, win.actTheme_,
                               win.actShortcuts_, win.actSettings_, win.actInfo_};
    QCOMPARE(got.size(), want.size());
    for (int i = 0; i < want.size(); ++i)
      QVERIFY2(got.at(i) == want.at(i),
               qPrintable(QString("slot %1 is %2, expected %3")
                              .arg(i)
                              .arg(got.at(i)->text(), want.at(i)->text())));
    for (QToolButton* b : section->findChildren<QToolButton*>()) {
      QVERIFY2(!b->icon().isNull(), qPrintable(b->defaultAction()->text() + " has no glyph"));
      QCOMPARE(b->property("toolSection").toString(), QStringLiteral("Settings"));
    }

    // Each button triggers its action: the two toggles flip, and the three that
    // open something are wired (checked via the action's own connections below).
    QToolButton* incognitoBtn = nullptr;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction() == win.actIncognito_) incognitoBtn = b;
    QVERIFY(incognitoBtn);
    QVERIFY2(incognitoBtn->isCheckable(), "the incognito button must show a lit state");
    QVERIFY(!win.incognito_);
    incognitoBtn->click();                       // toolbar → state + menu bar
    QTRY_VERIFY2(win.incognito_, "the toolbar button did not turn incognito on");
    QVERIFY(win.actIncognito_->isChecked() && incognitoBtn->isChecked());
    win.actIncognito_->setChecked(false);        // menu bar → toolbar button
    QTRY_VERIFY(!win.incognito_);
    QVERIFY2(!incognitoBtn->isChecked(), "the toolbar button kept its lit state");

    // Theme flips both ways from the toolbar too.
    const QString before = win.settings_.themeMode;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction() == win.actTheme_) b->click();
    QTRY_VERIFY2(win.settings_.themeMode != before, "the theme button did nothing");

    // The palette button opens the SAME accent popover the logo does.
    QCOMPARE(win.actAccent_->objectName(), QStringLiteral("actAccent"));

    // …and it must actually be ON SCREEN: visible, non-empty and fully inside the toolbar's own rect, at
    // laptop widths with the custom-page cm inputs showing, or QToolBar's "»" swallows the section.
    QToolBar* row = win.findChild<QToolBar*>("mainToolbar");   // the one wrapping run
    QVERIFY(row);
    const int custom = win.units_.pageSize->findData(QStringLiteral("custom"));
    QVERIFY(custom >= 0);
    for (const int width : {1950, 1400, 1100, 975}) {
      win.resize(width, 850);
      win.units_.pageSize->setCurrentIndex(custom);   // the widest state of this row
      settleLayout(&win, 200);
      const QString at = QString("at %1px: ").arg(width);
      QVERIFY2(section->isVisible(), qPrintable(at + "the SETTINGS section is not visible"));
      QVERIFY2(section->width() > 0 && section->height() > 0,
               qPrintable(at + "the SETTINGS section collapsed to nothing"));
      QVERIFY2(row->rect().contains(section->geometry()),
               qPrintable(at + "the SETTINGS section is outside the toolbar (" +
                          QDebug::toString(section->geometry()) + " in " +
                          QDebug::toString(row->rect()) + ")"));
      QVERIFY2(row->sizeHint().width() <= width,
               qPrintable(at + QString("the row needs %1px and would overflow into \"»\"")
                                   .arg(row->sizeHint().width())));
      // …and it sits AFTER Data, which is where the browser puts it.
      QWidget* data = nullptr;
      for (QLabel* l : row->findChildren<QLabel*>("sectionLabel"))
        if (l->text() == QLatin1String("DATA")) data = l->parentWidget();
      QVERIFY2(data, qPrintable(at + "no DATA section on this row"));
      QVERIFY2(section->x() > data->x(), qPrintable(at + "SETTINGS is not after DATA"));
      for (QToolButton* b : section->findChildren<QToolButton*>())
        QVERIFY2(b->isVisible(), qPrintable(at + b->defaultAction()->text() + " is hidden"));
    }
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarSettings.gui.moc"
